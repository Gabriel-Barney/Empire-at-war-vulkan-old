#include "alamo/assets/texture.hpp"

#include "png_decode.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <exception>

#include "alamo/core/file_provider.hpp"
#include "alamo/core/hash.hpp"

namespace alamo::assets {

namespace {

constexpr std::uint32_t kMaxDim = 16384;
constexpr std::size_t kMaxRlePixels = std::size_t(8192) * 8192;

std::uint32_t rd32(const std::vector<std::uint8_t>& b, std::size_t off) {
    std::uint32_t v = 0;
    std::memcpy(&v, b.data() + off, 4);
    return v;
}
std::uint16_t rd16(const std::vector<std::uint8_t>& b, std::size_t off) {
    std::uint16_t v = 0;
    std::memcpy(&v, b.data() + off, 2);
    return v;
}
constexpr std::uint32_t fourcc(char a, char b, char c, char d) {
    return std::uint32_t(std::uint8_t(a)) | (std::uint32_t(std::uint8_t(b)) << 8) |
           (std::uint32_t(std::uint8_t(c)) << 16) | (std::uint32_t(std::uint8_t(d)) << 24);
}
bool fail(std::string* error, const std::string& msg) {
    if (error) *error = msg;
    return false;
}
std::uint32_t mip_dim(std::uint32_t d, std::uint32_t mip) { return std::max<std::uint32_t>(1, d >> mip); }
std::uint32_t max_mips(std::uint32_t w, std::uint32_t h) {
    std::uint32_t n = 1;
    for (std::uint32_t d = std::max(w, h); d > 1; d >>= 1) ++n;
    return n;
}

// Uncompressed source pixel description (DDS masks). A colour channel the format lacks reads 0, or 1.0 when
// absent_one is set (D3D9 G16R16: (R, G, 1, 1)).
struct MaskFormat {
    std::uint32_t bits = 0;
    std::uint32_t r = 0, g = 0, b = 0, a = 0;
    bool luminance = false;
    bool absent_one = false;
};

struct Channel {
    std::uint32_t mask = 0, shift = 0, max = 0;
    explicit Channel(std::uint32_t m) : mask(m) {
        if (!m) return;
        while (!((m >> shift) & 1u)) ++shift;
        max = m >> shift;
    }
    // Returns -1 when the channel is absent.
    int get(std::uint32_t px) const {
        if (!mask || !max) return -1;
        const std::uint64_t v = (px & mask) >> shift;
        return static_cast<int>((v * 255u + max / 2) / max);
    }
};

// Convert one tightly packed level of `mf` pixels to RGBA8.
void convert_masked(const std::uint8_t* src, std::uint32_t w, std::uint32_t h, const MaskFormat& mf, std::uint8_t* dst) {
    const std::uint32_t bpp = mf.bits / 8;
    const Channel cr(mf.r), cg(mf.g), cb(mf.b), ca(mf.a);
    const std::size_t count = std::size_t(w) * h;
    for (std::size_t i = 0; i < count; ++i) {
        std::uint32_t px = 0;
        std::memcpy(&px, src + i * bpp, bpp);
        int r = cr.get(px), g = cg.get(px), b = cb.get(px);
        const int a = ca.get(px);
        if (mf.luminance) g = b = r;
        const int none = mf.absent_one ? 255 : 0;
        dst[i * 4 + 0] = static_cast<std::uint8_t>(r < 0 ? none : r);
        dst[i * 4 + 1] = static_cast<std::uint8_t>(g < 0 ? none : g);
        dst[i * 4 + 2] = static_cast<std::uint8_t>(b < 0 ? none : b);
        dst[i * 4 + 3] = static_cast<std::uint8_t>(a < 0 ? 255 : a);
    }
}

// ---- conversions for the formats the GPU path has no direct equivalent for (mcx2 t04, MOD_COMPAT_DESIGN 10.3) ----

// How a DDS level is turned into upload data. Masked: convert_masked to RGBA8. The others are named after the source.
enum class Source : std::uint8_t {
    Native, Masked, BC4, BC5, V8U8, Q8W8V8U8, CxV8U8, Unorm16x4, Half, Float, P8, A8P8
};

// One BC4 block (8 bytes) -> 16 values, written with stride `step` into the 4x4 area of `dst` (row pitch `pitch`).
// Palette as the D3D10 BC4_UNORM rule: r0 > r1 -> 6 interpolated values, else 4 plus 0 and 255; rounding to nearest.
void decode_bc4_block(const std::uint8_t* b, std::uint8_t* dst, std::size_t pitch, std::size_t step, std::uint32_t bw,
                      std::uint32_t bh) {
    const unsigned r0 = b[0], r1 = b[1];
    std::uint8_t pal[8];
    pal[0] = static_cast<std::uint8_t>(r0);
    pal[1] = static_cast<std::uint8_t>(r1);
    if (r0 > r1) {
        for (unsigned i = 1; i <= 6; ++i) pal[i + 1] = static_cast<std::uint8_t>(((7 - i) * r0 + i * r1 + 3) / 7);
    } else {
        for (unsigned i = 1; i <= 4; ++i) pal[i + 1] = static_cast<std::uint8_t>(((5 - i) * r0 + i * r1 + 2) / 5);
        pal[6] = 0;
        pal[7] = 255;
    }
    std::uint64_t bits = 0;
    for (int i = 0; i < 6; ++i) bits |= std::uint64_t(b[2 + i]) << (8 * i);
    for (std::uint32_t y = 0; y < bh; ++y)
        for (std::uint32_t x = 0; x < bw; ++x)
            dst[y * pitch + x * step] = pal[(bits >> (3 * (y * 4 + x))) & 7u];
}

// BC4 (ATI1) -> R8, BC5 (ATI2) -> R8G8 (first block = red, second = green, as the D3D10 BC5 layout).
void convert_bc45(const std::uint8_t* src, std::uint32_t w, std::uint32_t h, bool two, std::uint8_t* dst) {
    const std::size_t ch = two ? 2 : 1, pitch = std::size_t(w) * ch;
    const std::uint32_t bw = (w + 3) / 4, bh = (h + 3) / 4;
    for (std::uint32_t by = 0; by < bh; ++by)
        for (std::uint32_t bx = 0; bx < bw; ++bx) {
            const std::uint32_t cw = std::min(4u, w - bx * 4), chh = std::min(4u, h - by * 4);
            std::uint8_t* d = dst + (std::size_t(by) * 4 * w + std::size_t(bx) * 4) * ch;
            for (std::size_t c = 0; c < ch; ++c) decode_bc4_block(src + c * 8, d + c, pitch, ch, cw, chh);
            src += 8 * ch;
        }
}

// IEEE half bits of 1.0.
constexpr std::uint16_t kHalfOne = 0x3C00;

// Signed bump and the wider formats: one level of `w * h` pixels.
void convert_level(Source s, const std::uint8_t* src, std::uint32_t w, std::uint32_t h, std::uint32_t channels,
                   const std::uint8_t* palette, std::uint8_t* dst) {
    const std::size_t n = std::size_t(w) * h;
    switch (s) {
        case Source::BC4: convert_bc45(src, w, h, false, dst); return;
        case Source::BC5: convert_bc45(src, w, h, true, dst); return;
        case Source::V8U8:  // byte 0 = U, byte 1 = V -> RGBA8_SNORM (U, V, 1, 1), as D3D9 sampled it
            for (std::size_t i = 0; i < n; ++i) {
                dst[i * 4 + 0] = src[i * 2];
                dst[i * 4 + 1] = src[i * 2 + 1];
                dst[i * 4 + 2] = dst[i * 4 + 3] = 127;
            }
            return;
        case Source::Q8W8V8U8:  // bytes U V W Q -> RGBA8_SNORM as stored
            std::memcpy(dst, src, n * 4);
            return;
        case Source::CxV8U8:  // (U, V, sqrt(1 - U^2 - V^2), 1): D3D9 computes the third component
            for (std::size_t i = 0; i < n; ++i) {
                const double u = std::max(-1.0, static_cast<std::int8_t>(src[i * 2]) / 127.0);
                const double v = std::max(-1.0, static_cast<std::int8_t>(src[i * 2 + 1]) / 127.0);
                const double c = std::sqrt(std::max(0.0, 1.0 - u * u - v * v));
                dst[i * 4 + 0] = src[i * 2];
                dst[i * 4 + 1] = src[i * 2 + 1];
                dst[i * 4 + 2] = static_cast<std::uint8_t>(static_cast<std::int8_t>(std::lround(c * 127.0)));
                dst[i * 4 + 3] = 127;
            }
            return;
        case Source::Unorm16x4:  // A16B16G16R16: R G B A as 16-bit -> RGBA8, rounded
            for (std::size_t i = 0; i < n * 4; ++i) {
                std::uint16_t v = 0;
                std::memcpy(&v, src + i * 2, 2);
                dst[i] = static_cast<std::uint8_t>((std::uint32_t(v) * 255u + 32767u) / 65535u);
            }
            return;
        case Source::Half:  // 1, 2 or 4 halves -> RGBA16F, missing components 1.0 (D3D9 R16F = (R, 1, 1, 1))
            for (std::size_t i = 0; i < n; ++i)
                for (std::uint32_t c = 0; c < 4; ++c) {
                    std::uint16_t v = kHalfOne;
                    if (c < channels) std::memcpy(&v, src + (i * channels + c) * 2, 2);
                    std::memcpy(dst + (i * 4 + c) * 2, &v, 2);
                }
            return;
        case Source::Float:  // 1, 2 or 4 floats -> RGBA32F, missing components 1.0
            for (std::size_t i = 0; i < n; ++i)
                for (std::uint32_t c = 0; c < 4; ++c) {
                    float v = 1.0f;
                    if (c < channels) std::memcpy(&v, src + (i * channels + c) * 4, 4);
                    std::memcpy(dst + (i * 4 + c) * 4, &v, 4);
                }
            return;
        case Source::P8:
        case Source::A8P8:  // PALETTEENTRY = R, G, B, flags (flags as alpha) -> RGBA8; A8P8: index, alpha
            for (std::size_t i = 0; i < n; ++i) {
                const std::uint8_t idx = s == Source::P8 ? src[i] : src[i * 2];
                std::memcpy(dst + i * 4, palette + std::size_t(idx) * 4, 4);
                if (s == Source::A8P8) dst[i * 4 + 3] = src[i * 2 + 1];
            }
            return;
        case Source::Native:
        case Source::Masked: return;
    }
}

// Source bytes per pixel of the per-pixel sources (0 for the block formats).
std::uint32_t source_bytes(Source s, std::uint32_t channels) {
    switch (s) {
        case Source::V8U8: return 2;
        case Source::Q8W8V8U8: return 4;
        case Source::CxV8U8: return 2;
        case Source::Unorm16x4: return 8;
        case Source::Half: return 2 * channels;
        case Source::Float: return 4 * channels;
        case Source::P8: return 1;
        case Source::A8P8: return 2;
        default: return 0;
    }
}

// Legacy D3DFMT values used as a DDS FourCC.
bool d3dfmt_masks(std::uint32_t f, MaskFormat& mf) {
    switch (f) {
        case 20: mf = {24, 0xFF0000, 0xFF00, 0xFF, 0, false}; return true;           // R8G8B8
        case 21: mf = {32, 0xFF0000, 0xFF00, 0xFF, 0xFF000000u, false}; return true; // A8R8G8B8
        case 22: mf = {32, 0xFF0000, 0xFF00, 0xFF, 0, false}; return true;           // X8R8G8B8
        case 23: mf = {16, 0xF800, 0x7E0, 0x1F, 0, false}; return true;              // R5G6B5
        case 24: mf = {16, 0x7C00, 0x3E0, 0x1F, 0, false}; return true;              // X1R5G5B5
        case 25: mf = {16, 0x7C00, 0x3E0, 0x1F, 0x8000, false}; return true;         // A1R5G5B5
        case 26: mf = {16, 0xF00, 0xF0, 0xF, 0xF000, false}; return true;            // A4R4G4B4
        case 28: mf = {8, 0, 0, 0, 0xFF, false}; return true;                        // A8
        case 30: mf = {16, 0xF00, 0xF0, 0xF, 0, false}; return true;                 // X4R4G4B4
        case 32: mf = {32, 0xFF, 0xFF00, 0xFF0000, 0xFF000000u, false}; return true; // A8B8G8R8
        case 33: mf = {32, 0xFF, 0xFF00, 0xFF0000, 0, false}; return true;           // X8B8G8R8
        case 50: mf = {8, 0xFF, 0, 0, 0, true}; return true;                         // L8
        case 51: mf = {16, 0xFF, 0, 0, 0xFF00, true}; return true;                   // A8L8
        case 31: mf = {32, 0x3FF, 0xFFC00, 0x3FF00000, 0xC0000000u, false}; return true;  // A2B10G10R10
        case 35: mf = {32, 0x3FF00000, 0xFFC00, 0x3FF, 0xC0000000u, false}; return true;  // A2R10G10B10
        case 34: mf = {32, 0xFFFF, 0xFFFF0000u, 0, 0, false, true}; return true;         // G16R16: (R, G, 1, 1)
        case 81: mf = {16, 0xFFFF, 0, 0, 0, true}; return true;                          // L16
        default: return false;
    }
}

enum class Plan { Native, Convert };

// Bytes of `mips` levels x `layers` in format f.
std::uint64_t chain_bytes(TextureFormat f, std::uint32_t w, std::uint32_t h, std::uint32_t mips, std::uint32_t layers) {
    std::uint64_t per_layer = 0;
    for (std::uint32_t k = 0; k < mips; ++k) per_layer += texture_level_size(f, mip_dim(w, k), mip_dim(h, k));
    return per_layer * layers;
}

// Final decoded size of a texture: a single uncompressed level gets a full generated chain.
bool within_budget(TextureFormat f, std::uint32_t w, std::uint32_t h, std::uint32_t mips, std::uint32_t layers,
                   std::string* error) {
    const bool gen = mips == 1 && f != TextureFormat::BC1_RGBA && f != TextureFormat::BC2 && f != TextureFormat::BC3;
    const std::uint64_t total = chain_bytes(f, w, h, gen ? max_mips(w, h) : mips, layers);
    if (total <= kMaxDecodedTextureBytes) return true;
    return fail(error, "decoded texture would need " + std::to_string(total >> 20) + " MiB (budget " +
                           std::to_string(kMaxDecodedTextureBytes >> 20) + " MiB)");
}

}  // namespace

const char* texture_format_name(TextureFormat f) {
    switch (f) {
        case TextureFormat::BC1_RGBA: return "BC1_RGBA";
        case TextureFormat::BC2: return "BC2";
        case TextureFormat::BC3: return "BC3";
        case TextureFormat::RGBA8: return "RGBA8";
        case TextureFormat::BGRA8: return "BGRA8";
        case TextureFormat::R8: return "R8";
        case TextureFormat::R8G8: return "R8G8";
        case TextureFormat::R8G8_SNORM: return "R8G8_SNORM";
        case TextureFormat::RGBA8_SNORM: return "RGBA8_SNORM";
        case TextureFormat::RGBA16F: return "RGBA16F";
        case TextureFormat::RGBA32F: return "RGBA32F";
        default: return "Unknown";
    }
}

const char* texture_format_vk_name(TextureFormat f) {
    switch (f) {
        case TextureFormat::BC1_RGBA: return "VK_FORMAT_BC1_RGBA_UNORM_BLOCK";
        case TextureFormat::BC2: return "VK_FORMAT_BC2_UNORM_BLOCK";
        case TextureFormat::BC3: return "VK_FORMAT_BC3_UNORM_BLOCK";
        case TextureFormat::RGBA8: return "VK_FORMAT_R8G8B8A8_UNORM";
        case TextureFormat::BGRA8: return "VK_FORMAT_B8G8R8A8_UNORM";
        case TextureFormat::R8: return "VK_FORMAT_R8_UNORM";
        case TextureFormat::R8G8: return "VK_FORMAT_R8G8_UNORM";
        case TextureFormat::R8G8_SNORM: return "VK_FORMAT_R8G8_SNORM";
        case TextureFormat::RGBA8_SNORM: return "VK_FORMAT_R8G8B8A8_SNORM";
        case TextureFormat::RGBA16F: return "VK_FORMAT_R16G16B16A16_SFLOAT";
        case TextureFormat::RGBA32F: return "VK_FORMAT_R32G32B32A32_SFLOAT";
        default: return "VK_FORMAT_UNDEFINED";
    }
}

std::uint64_t texture_level_size(TextureFormat f, std::uint32_t w, std::uint32_t h) {
    const std::uint64_t bw = (std::uint64_t(w) + 3) / 4, bh = (std::uint64_t(h) + 3) / 4;
    switch (f) {
        case TextureFormat::BC1_RGBA: return bw * bh * 8;
        case TextureFormat::BC2:
        case TextureFormat::BC3: return bw * bh * 16;
        case TextureFormat::RGBA8:
        case TextureFormat::BGRA8:
        case TextureFormat::RGBA8_SNORM: return std::uint64_t(w) * h * 4;
        case TextureFormat::R8: return std::uint64_t(w) * h;
        case TextureFormat::R8G8:
        case TextureFormat::R8G8_SNORM: return std::uint64_t(w) * h * 2;
        case TextureFormat::RGBA16F: return std::uint64_t(w) * h * 8;
        case TextureFormat::RGBA32F: return std::uint64_t(w) * h * 16;
        default: return 0;
    }
}

bool generate_mips(TextureAsset& t) {
    std::size_t bpp = 0;
    bool snorm = false;
    switch (t.format) {
        case TextureFormat::RGBA8:
        case TextureFormat::BGRA8: bpp = 4; break;
        case TextureFormat::R8G8: bpp = 2; break;
        case TextureFormat::R8: bpp = 1; break;
        case TextureFormat::R8G8_SNORM: bpp = 2; snorm = true; break;
        case TextureFormat::RGBA8_SNORM: bpp = 4; snorm = true; break;
        default: return false;  // block-compressed and float: the levels the file has
    }
    if (t.mip_levels != 1 || t.layers == 0 || t.levels.size() != t.layers || t.width == 0 || t.height == 0) return false;
    const std::uint32_t mips = max_mips(t.width, t.height);
    if (mips == 1) return false;
    std::uint64_t per_layer = 0;
    for (std::uint32_t k = 0; k < mips; ++k) per_layer += texture_level_size(t.format, mip_dim(t.width, k), mip_dim(t.height, k));
    std::vector<std::uint8_t> data(static_cast<std::size_t>(per_layer * t.layers));
    std::vector<TextureLevel> levels;
    levels.reserve(std::size_t(t.layers) * mips);
    for (std::uint32_t l = 0; l < t.layers; ++l) {
        const TextureLevel& base = t.levels[l];
        if (base.layer != l || base.mip != 0 || base.width != t.width || base.height != t.height ||
            base.offset + base.size > t.data.size() || base.size != texture_level_size(t.format, t.width, t.height))
            return false;
        std::uint64_t off = std::uint64_t(l) * per_layer;
        std::memcpy(data.data() + off, t.data.data() + base.offset, static_cast<std::size_t>(base.size));
        levels.push_back({l, 0, t.width, t.height, off, base.size});
        for (std::uint32_t k = 1; k < mips; ++k) {
            const TextureLevel& src = levels.back();
            const std::uint32_t sw = src.width, sh = src.height;
            const std::uint32_t dw = mip_dim(t.width, k), dh = mip_dim(t.height, k);
            const std::uint64_t doff = src.offset + src.size;
            const std::uint64_t dsz = texture_level_size(t.format, dw, dh);
            const std::uint8_t* s = data.data() + src.offset;
            std::uint8_t* d = data.data() + doff;
            const std::uint32_t nx = sw > 1 ? 2u : 1u, ny = sh > 1 ? 2u : 1u;
            const std::uint32_t n = nx * ny;
            for (std::uint32_t y = 0; y < dh; ++y)
                for (std::uint32_t x = 0; x < dw; ++x)
                    for (std::size_t c = 0; c < bpp; ++c) {
                        std::uint32_t sum = 0;
                        std::int32_t ssum = 0;
                        for (std::uint32_t j = 0; j < ny; ++j)
                            for (std::uint32_t i = 0; i < nx; ++i) {
                                const std::uint8_t v = s[(std::size_t(y * ny + j) * sw + (x * nx + i)) * bpp + c];
                                sum += v;
                                ssum += static_cast<std::int8_t>(v);
                            }
                        std::uint8_t out = static_cast<std::uint8_t>((sum + n / 2) / n);
                        if (snorm) {  // signed mean, rounded half away from zero
                            const std::int32_t sn = static_cast<std::int32_t>(n);
                            const std::int32_t q = ssum >= 0 ? (ssum + sn / 2) / sn : -((-ssum + sn / 2) / sn);
                            out = static_cast<std::uint8_t>(static_cast<std::int8_t>(q));
                        }
                        d[(std::size_t(y) * dw + x) * bpp + c] = out;
                    }
            levels.push_back({l, k, dw, dh, doff, dsz});
        }
    }
    t.data = std::move(data);
    t.levels = std::move(levels);
    t.mip_levels = mips;
    return true;
}

const TextureLevel* TextureAsset::level(std::uint32_t layer, std::uint32_t mip) const {
    if (layer >= layers || mip >= mip_levels) return nullptr;
    const std::size_t i = std::size_t(layer) * mip_levels + mip;
    return i < levels.size() ? &levels[i] : nullptr;
}

// ---- DDS -------------------------------------------------------------------------------------

bool decode_dds(std::vector<std::uint8_t> file, TextureAsset& out, std::string* error) {
    if (file.size() < 128 || rd32(file, 0) != fourcc('D', 'D', 'S', ' ')) return fail(error, "not a DDS file");
    const std::size_t H = 4;
    if (rd32(file, H + 0) != 124) return fail(error, "DDS header size is not 124");
    const std::uint32_t height = rd32(file, H + 8), width = rd32(file, H + 12);
    const std::uint32_t depth_field = rd32(file, H + 20), mip_field = rd32(file, H + 24);
    const std::uint32_t pf_flags = rd32(file, H + 76), pf_fourcc = rd32(file, H + 80);
    const std::uint32_t caps2 = rd32(file, H + 108);
    if (width == 0 || height == 0 || width > kMaxDim || height > kMaxDim)
        return fail(error, "DDS dimensions out of range (" + std::to_string(width) + "x" + std::to_string(height) + ")");
    // A volume texture keeps its first slice (the port samples every texture as 2D) [port]; each level of the file
    // holds `depth >> mip` slices.
    const std::uint32_t depth = (caps2 & 0x200000u) && depth_field > 1 ? std::min(depth_field, kMaxDim) : 1;

    TextureAsset t;
    std::size_t data_off = 128;
    std::uint32_t layers = 1;
    std::uint32_t faces_in_file = 1;
    std::uint32_t face_mask = 0x3Fu;  // cube faces present, bit i = Vulkan layer i
    if (caps2 & 0x200u) {
        face_mask = (caps2 >> 10) & 0x3Fu;
        if (!face_mask) return fail(error, "cubemap without faces");
        layers = 6;
        t.cubemap = true;
        faces_in_file = static_cast<std::uint32_t>(std::popcount(face_mask));
        if (faces_in_file != 6)
            t.warnings.push_back("partial cubemap: " + std::to_string(faces_in_file) +
                                 " faces present, the others are black");
    }
    if (depth > 1) t.warnings.push_back("volume texture: slice 0 of " + std::to_string(depth) + " used");

    Source src = Source::Native;
    std::uint32_t channels = 0;
    MaskFormat mf;
    TextureFormat fmt = TextureFormat::Unknown;
    ComponentMapping sw;
    const std::uint8_t* palette = nullptr;
    auto use = [&](Source s, TextureFormat f, std::uint32_t ch = 0) {
        src = s;
        fmt = f;
        channels = ch;
    };

    if (pf_flags & 0x4u) {  // FourCC
        switch (pf_fourcc) {
            case fourcc('D', 'X', 'T', '1'): fmt = TextureFormat::BC1_RGBA; break;
            case fourcc('D', 'X', 'T', '2'): fmt = TextureFormat::BC2; t.premultiplied = true; break;
            case fourcc('D', 'X', 'T', '3'): fmt = TextureFormat::BC2; break;
            case fourcc('D', 'X', 'T', '4'): fmt = TextureFormat::BC3; t.premultiplied = true; break;
            case fourcc('D', 'X', 'T', '5'): fmt = TextureFormat::BC3; break;
            // ATI1 / BC4: one channel; R8 with the identity mapping samples (R, 0, 0, 1) [port: inferred, the D3D10
            // BC4 rule]. ATI2 / BC5: two channels, the first block red; R8G8 samples (R, G, 0, 1) [port: inferred].
            case fourcc('A', 'T', 'I', '1'):
            case fourcc('B', 'C', '4', 'U'): use(Source::BC4, TextureFormat::R8); break;
            case fourcc('A', 'T', 'I', '2'):
            case fourcc('B', 'C', '5', 'U'): use(Source::BC5, TextureFormat::R8G8); break;
            case 60: use(Source::V8U8, TextureFormat::RGBA8_SNORM); break;  // V8U8: (U, V, 1, 1), signed
            case 63: use(Source::Q8W8V8U8, TextureFormat::RGBA8_SNORM); break;  // Q8W8V8U8: (U, V, W, Q)
            case 117: use(Source::CxV8U8, TextureFormat::RGBA8_SNORM); break;   // CxV8U8: (U, V, C, 1)
            case 36: use(Source::Unorm16x4, TextureFormat::RGBA8); break;       // A16B16G16R16
            case 111: use(Source::Half, TextureFormat::RGBA16F, 1); break;     // R16F
            case 112: use(Source::Half, TextureFormat::RGBA16F, 2); break;     // G16R16F
            case 113: use(Source::Half, TextureFormat::RGBA16F, 4); break;     // A16B16G16R16F
            case 114: use(Source::Float, TextureFormat::RGBA32F, 1); break;    // R32F
            case 115: use(Source::Float, TextureFormat::RGBA32F, 2); break;    // G32R32F
            case 116: use(Source::Float, TextureFormat::RGBA32F, 4); break;    // A32B32G32R32F
            case 40: use(Source::A8P8, TextureFormat::RGBA8); break;           // A8P8
            case 41: use(Source::P8, TextureFormat::RGBA8); break;             // P8
            case fourcc('D', 'X', '1', '0'):
                // mcx2 t11: retail loads textures through d3dx9_43, which has no reader for the DX10 extended header
                // (D3DXGetImageInfoFromFileInMemory: D3DXERR_INVALIDDATA, docs/re/MCX2_T02_ASSET_SALVAGE.md 4), so
                // retail shows such a texture as missing; the port does the same.
                return fail(error, "DX10-header DDS: retail's D3DX9 cannot read it (the texture is missing in retail too)");
            default:
                if (!d3dfmt_masks(pf_fourcc, mf)) return fail(error, "unsupported DDS FourCC " + std::to_string(pf_fourcc));
                break;
        }
    } else if (pf_flags & 0x20u) {  // PALETTEINDEXED8: 256 PALETTEENTRYs after the header
        use(Source::P8, TextureFormat::RGBA8);
    } else if (pf_flags & (0x40u | 0x20000u | 0x2u)) {  // RGB / LUMINANCE / ALPHA
        mf.bits = rd32(file, H + 84);
        mf.r = rd32(file, H + 88);
        mf.g = rd32(file, H + 92);
        mf.b = rd32(file, H + 96);
        mf.a = (pf_flags & (0x1u | 0x2u)) ? rd32(file, H + 100) : 0;
        mf.luminance = (pf_flags & 0x20000u) != 0;
        if (pf_flags & 0x2u) mf.r = mf.g = mf.b = 0;  // alpha-only
        if (!mf.luminance && mf.r && mf.r == mf.g && mf.g == mf.b) mf.luminance = true;
        if (mf.luminance) mf.g = mf.b = 0;
    } else {
        return fail(error, "DDS pixel format has neither FourCC nor RGB/luminance/alpha flags");
    }
    if (src == Source::P8 || src == Source::A8P8) {
        if (file.size() < data_off + 1024) return fail(error, "DDS palette truncated");
        palette = file.data() + data_off;
        data_off += 1024;
    }

    if (fmt == TextureFormat::Unknown) {
        if (mf.bits != 8 && mf.bits != 16 && mf.bits != 24 && mf.bits != 32)
            return fail(error, "unsupported DDS bit count " + std::to_string(mf.bits));
        // Native layouts where possible, otherwise expand to RGBA8.
        if (mf.luminance && mf.bits == 8 && mf.r == 0xFF && !mf.a) {
            fmt = TextureFormat::R8;
            sw = {Swizzle::R, Swizzle::R, Swizzle::R, Swizzle::One};
        } else if (mf.luminance && mf.bits == 16 && mf.r == 0xFF && mf.a == 0xFF00) {
            fmt = TextureFormat::R8G8;
            sw = {Swizzle::R, Swizzle::R, Swizzle::R, Swizzle::G};
        } else if (!mf.luminance && mf.bits == 8 && !mf.r && !mf.g && !mf.b && mf.a == 0xFF) {
            fmt = TextureFormat::R8;
            sw = {Swizzle::Zero, Swizzle::Zero, Swizzle::Zero, Swizzle::R};
        } else if (mf.bits == 32 && mf.r == 0xFF0000 && mf.g == 0xFF00 && mf.b == 0xFF &&
                   (mf.a == 0xFF000000u || mf.a == 0)) {
            fmt = TextureFormat::BGRA8;
            if (!mf.a) sw.a = Swizzle::One;
        } else if (mf.bits == 32 && mf.r == 0xFF && mf.g == 0xFF00 && mf.b == 0xFF0000 &&
                   (mf.a == 0xFF000000u || mf.a == 0)) {
            fmt = TextureFormat::RGBA8;
            if (!mf.a) sw.a = Swizzle::One;
        } else {
            fmt = TextureFormat::RGBA8;
            src = Source::Masked;
        }
    }

    // Bytes one slice of a level takes in the file.
    auto src_slice = [&](std::uint32_t w, std::uint32_t h) -> std::uint64_t {
        const std::uint64_t bw = (std::uint64_t(w) + 3) / 4, bh = (std::uint64_t(h) + 3) / 4;
        switch (src) {
            case Source::Native: return texture_level_size(fmt, w, h);
            case Source::Masked: return std::uint64_t(w) * h * (mf.bits / 8);
            case Source::BC4: return bw * bh * 8;
            case Source::BC5: return bw * bh * 16;
            default: return std::uint64_t(w) * h * source_bytes(src, channels);
        }
    };
    auto src_size = [&](std::uint32_t k) -> std::uint64_t {
        return src_slice(mip_dim(width, k), mip_dim(height, k)) * mip_dim(depth, k);
    };
    std::uint32_t mips = std::max<std::uint32_t>(1, mip_field);
    mips = std::min(mips, max_mips(std::max(width, depth), height));
    const std::uint64_t avail = file.size() - data_off;
    std::uint32_t fit = 0;
    for (std::uint32_t m = 1; m <= mips; ++m) {
        std::uint64_t per_layer = 0;
        for (std::uint32_t k = 0; k < m; ++k) per_layer += src_size(k);
        if (per_layer * faces_in_file <= avail) fit = m;
        else break;
    }
    if (fit == 0) return fail(error, "DDS data truncated (first mip level incomplete)");
    if (fit < mips) {
        t.warnings.push_back("mip chain truncated: " + std::to_string(mips) + " declared, " + std::to_string(fit) + " present");
        // Layer strides in the file still use the declared chain length for cubemaps; only a
        // single-layer texture can be cut safely.
        if (faces_in_file > 1) return fail(error, "cubemap DDS data truncated");
    }
    std::uint64_t layer_stride = 0;  // in the file
    for (std::uint32_t k = 0; k < (faces_in_file > 1 ? mips : fit); ++k) layer_stride += src_size(k);
    mips = std::min(fit, max_mips(width, height));
    if (!within_budget(fmt, width, height, mips, layers, error)) return false;

    t.format = fmt;
    t.swizzle = sw;
    t.width = width;
    t.height = height;
    t.mip_levels = mips;
    t.layers = layers;
    t.levels.reserve(std::size_t(layers) * mips);

    if (src == Source::Native && depth == 1 && faces_in_file == layers) {
        for (std::uint32_t l = 0; l < layers; ++l) {
            std::uint64_t off = data_off + l * layer_stride;
            for (std::uint32_t k = 0; k < mips; ++k) {
                const std::uint32_t w = mip_dim(width, k), h = mip_dim(height, k);
                const std::uint64_t sz = texture_level_size(fmt, w, h);
                t.levels.push_back({l, k, w, h, off, sz});
                off += sz;
            }
        }
        t.data = std::move(file);
    } else {
        std::uint64_t total = 0;
        for (std::uint32_t k = 0; k < mips; ++k) total += texture_level_size(fmt, mip_dim(width, k), mip_dim(height, k));
        t.data.assign(static_cast<std::size_t>(total * layers), 0);  // a missing cube face stays black
        std::uint64_t dst_off = 0;
        std::uint32_t file_face = 0;
        for (std::uint32_t l = 0; l < layers; ++l) {
            const bool present = (face_mask >> l) & 1u;
            std::uint64_t src_off = data_off + std::uint64_t(file_face) * layer_stride;
            if (present) ++file_face;
            for (std::uint32_t k = 0; k < mips; ++k) {
                const std::uint32_t w = mip_dim(width, k), h = mip_dim(height, k);
                const std::uint64_t sz = texture_level_size(fmt, w, h);
                if (present) {
                    const std::uint8_t* s = file.data() + src_off;  // slice 0 of the level
                    std::uint8_t* d = t.data.data() + dst_off;
                    if (src == Source::Native) std::memcpy(d, s, static_cast<std::size_t>(sz));
                    else if (src == Source::Masked) convert_masked(s, w, h, mf, d);
                    else convert_level(src, s, w, h, channels, palette, d);
                }
                t.levels.push_back({l, k, w, h, dst_off, sz});
                src_off += src_size(k);
                dst_off += sz;
            }
        }
        if (src == Source::P8 || src == Source::A8P8) {  // a palette whose flags byte is 0 everywhere is opaque
            bool any_alpha = false;
            for (std::size_t i = 0; i < 256 && !any_alpha; ++i) any_alpha = palette[i * 4 + 3] != 0;
            if (!any_alpha && src == Source::P8)
                for (std::size_t i = 3; i < t.data.size(); i += 4) t.data[i] = 255;
        }
        std::vector<std::uint8_t>().swap(file);  // release the source before mips are generated
    }
    if (t.mip_levels == 1) generate_mips(t);  // no-op for BC and float formats
    out = std::move(t);
    return true;
}

// ---- TGA -------------------------------------------------------------------------------------

bool decode_tga(std::vector<std::uint8_t> file, TextureAsset& out, std::string* error) {
    if (file.size() < 18) return fail(error, "TGA too small");
    const std::uint8_t id_len = file[0], cmap_type = file[1], type = file[2];
    const std::uint16_t cmap_len = rd16(file, 5);
    const std::uint8_t cmap_bits = file[7];
    const std::uint32_t width = rd16(file, 12), height = rd16(file, 14);
    const std::uint8_t bpp = file[16], desc = file[17];
    const bool rle = type == 9 || type == 10 || type == 11;
    const bool grey = type == 3 || type == 11;
    const bool mapped = type == 1 || type == 9;  // colour-mapped: pixels are indices into the colour map
    if (!(type == 1 || type == 2 || type == 3 || type == 9 || type == 10 || type == 11))
        return fail(error, "unsupported TGA image type " + std::to_string(type));
    if (width == 0 || height == 0 || width > kMaxDim || height > kMaxDim) return fail(error, "TGA dimensions out of range");
    if (mapped) {
        if (cmap_type != 1 || cmap_len == 0) return fail(error, "colour-mapped TGA without a colour map");
        if (bpp != 8 && bpp != 16) return fail(error, "unsupported TGA index depth " + std::to_string(bpp));
        if (cmap_bits != 15 && cmap_bits != 16 && cmap_bits != 24 && cmap_bits != 32)
            return fail(error, "unsupported TGA colour-map entry size " + std::to_string(cmap_bits));
    } else if (grey ? (bpp != 8 && bpp != 16) : (bpp != 15 && bpp != 16 && bpp != 24 && bpp != 32)) {
        return fail(error, "unsupported TGA bit depth " + std::to_string(bpp));
    }
    std::size_t pos = 18 + std::size_t(id_len);
    const std::size_t cmap_pos = pos, cmap_spp = (cmap_bits + 7u) / 8u;
    const std::uint16_t cmap_first = rd16(file, 3);
    if (cmap_type == 1) pos += std::size_t(cmap_len) * cmap_spp;  // entries are whole bytes (15 bits: 2)
    if (pos > file.size()) return fail(error, "TGA truncated header");
    std::vector<std::uint8_t> cmap;
    if (mapped) cmap.assign(file.begin() + static_cast<std::ptrdiff_t>(cmap_pos), file.begin() + static_cast<std::ptrdiff_t>(pos));

    const std::size_t spp = (bpp + 7u) / 8u;  // source bytes per pixel
    const std::size_t count = std::size_t(width) * height;
    // Never allocate for a size the file cannot back: uncompressed data must be fully present; an
    // RLE packet (1 header byte + >= 1 pixel) expands to at most 128 pixels, and RLE output is
    // capped at kMaxRlePixels (8192^2; the largest shipped TGA is 2048^2).
    const std::size_t remaining = file.size() - pos;
    if (!rle && remaining < count * spp) return fail(error, "TGA pixel data truncated");
    if (!within_budget(grey && !mapped ? (bpp == 8 ? TextureFormat::R8 : TextureFormat::R8G8) : TextureFormat::BGRA8, width,
                       height, 1, 1, error))
        return false;
    if (rle) {
        if (count > kMaxRlePixels) return fail(error, "RLE TGA larger than 8192x8192 is not supported");
        if (count > remaining / (1 + spp) * 128 + 128) return fail(error, "TGA RLE data too short for the image size");
    }
    std::vector<std::uint8_t> raw(count * spp);
    if (!rle) {
        if (file.size() - pos < raw.size()) return fail(error, "TGA pixel data truncated");
        std::memcpy(raw.data(), file.data() + pos, raw.size());
    } else {
        std::size_t px = 0;
        while (px < count) {
            if (pos >= file.size()) return fail(error, "TGA RLE data truncated");
            const std::uint8_t hdr = file[pos++];
            const std::size_t n = std::size_t(hdr & 0x7Fu) + 1;
            if (n > count - px) return fail(error, "TGA RLE packet overruns the image");
            if (hdr & 0x80u) {
                if (file.size() - pos < spp) return fail(error, "TGA RLE data truncated");
                for (std::size_t k = 0; k < n; ++k) std::memcpy(raw.data() + (px + k) * spp, file.data() + pos, spp);
                pos += spp;
            } else {
                if (file.size() - pos < n * spp) return fail(error, "TGA RLE data truncated");
                std::memcpy(raw.data() + px * spp, file.data() + pos, n * spp);
                pos += n * spp;
            }
            px += n;
        }
    }
    std::vector<std::uint8_t>().swap(file);  // only `raw` is needed from here on

    TextureAsset t;
    const unsigned alpha_bits = desc & 0x0Fu;
    std::size_t dpp = 4;
    if (grey) {
        dpp = bpp == 8 ? 1 : 2;
        t.format = bpp == 8 ? TextureFormat::R8 : TextureFormat::R8G8;
        t.swizzle = bpp == 8 ? ComponentMapping{Swizzle::R, Swizzle::R, Swizzle::R, Swizzle::One}
                             : ComponentMapping{Swizzle::R, Swizzle::R, Swizzle::R, Swizzle::G};
    } else {
        // 32 bpp is A8R8G8B8 whatever the descriptor's alpha-bit count says (D3DX behaviour):
        // shipped MT_COMMANDBAR.TGA declares 0 alpha bits but carries a real alpha channel.
        t.format = TextureFormat::BGRA8;
    }
    t.data.resize(count * dpp);
    const bool top = (desc & 0x20u) != 0, right = (desc & 0x10u) != 0;
    for (std::uint32_t y = 0; y < height; ++y) {
        const std::uint32_t sy = top ? y : height - 1 - y;
        for (std::uint32_t x = 0; x < width; ++x) {
            const std::uint32_t sx = right ? width - 1 - x : x;
            const std::uint8_t* s = raw.data() + (std::size_t(sy) * width + sx) * spp;
            std::uint8_t* d = t.data.data() + (std::size_t(y) * width + x) * dpp;
            if (mapped) {
                const std::uint32_t idx = spp == 2 ? std::uint32_t(s[0] | (s[1] << 8)) : s[0];
                if (idx < cmap_first || idx - cmap_first >= cmap_len) {  // outside the map: opaque black [port]
                    d[0] = d[1] = d[2] = 0;
                    d[3] = 255;
                    continue;
                }
                const std::uint8_t* e = cmap.data() + std::size_t(idx - cmap_first) * cmap_spp;
                if (cmap_spp == 2) {
                    const std::uint16_t v = static_cast<std::uint16_t>(e[0] | (e[1] << 8));
                    const auto x5 = [](unsigned c) { return static_cast<std::uint8_t>((c << 3) | (c >> 2)); };
                    d[0] = x5(v & 0x1Fu);
                    d[1] = x5((v >> 5) & 0x1Fu);
                    d[2] = x5((v >> 10) & 0x1Fu);
                    d[3] = (cmap_bits == 16 && alpha_bits > 0) ? ((v & 0x8000u) ? 255 : 0) : 255;
                } else {
                    d[0] = e[0];
                    d[1] = e[1];
                    d[2] = e[2];
                    d[3] = cmap_spp == 4 ? e[3] : 255;
                }
            } else if (grey) {
                d[0] = s[0];
                if (dpp == 2) d[1] = s[1];
            } else if (spp == 2) {
                const std::uint16_t v = static_cast<std::uint16_t>(s[0] | (s[1] << 8));
                const auto x5 = [](unsigned c) { return static_cast<std::uint8_t>((c << 3) | (c >> 2)); };
                d[0] = x5(v & 0x1Fu);
                d[1] = x5((v >> 5) & 0x1Fu);
                d[2] = x5((v >> 10) & 0x1Fu);
                d[3] = (bpp == 16 && alpha_bits > 0) ? ((v & 0x8000u) ? 255 : 0) : 255;
            } else {
                d[0] = s[0];
                d[1] = s[1];
                d[2] = s[2];
                d[3] = spp == 4 ? s[3] : 255;
            }
        }
    }
    t.width = width;
    t.height = height;
    t.mip_levels = 1;
    t.layers = 1;
    t.levels.push_back({0, 0, width, height, 0, t.data.size()});
    std::vector<std::uint8_t>().swap(raw);  // release the pre-expansion pixels before mips
    generate_mips(t);
    out = std::move(t);
    return true;
}

// ---- BMP -------------------------------------------------------------------------------------

bool decode_bmp(std::vector<std::uint8_t> file, TextureAsset& out, std::string* error) {
    if (file.size() < 26 || file[0] != 'B' || file[1] != 'M') return fail(error, "not a BMP file");
    const std::uint32_t pix_off = rd32(file, 10);
    const std::uint32_t hsz = rd32(file, 14);
    std::int64_t w = 0, h = 0;
    std::uint32_t bpp = 0, comp = 0;
    if (hsz == 12) {  // BITMAPCOREHEADER: 16-bit unsigned dimensions, always bottom-up
        w = rd16(file, 18);
        h = rd16(file, 20);
        bpp = rd16(file, 24);
    } else if (hsz >= 40) {  // BITMAPINFOHEADER / V2..V5: signed 32-bit dimensions, height < 0 = top-down
        if (file.size() < 54) return fail(error, "BMP header truncated");
        w = static_cast<std::int32_t>(rd32(file, 18));
        h = static_cast<std::int32_t>(rd32(file, 22));
        bpp = rd16(file, 28);
        comp = rd32(file, 30);
    } else {
        return fail(error, "unsupported BMP header size " + std::to_string(hsz));
    }
    const bool top_down = h < 0;
    if (top_down) h = -h;
    if (w <= 0 || h <= 0 || w > kMaxDim || h > kMaxDim)
        return fail(error, "BMP dimensions out of range (" + std::to_string(w) + "x" + std::to_string(h) + ")");
    const std::uint32_t width = static_cast<std::uint32_t>(w), height = static_cast<std::uint32_t>(h);

    MaskFormat mf;
    mf.bits = bpp;
    bool all_zero_alpha_is_opaque = false;
    if (comp == 0) {  // BI_RGB
        switch (bpp) {
            case 16: mf.r = 0x7C00; mf.g = 0x3E0; mf.b = 0x1F; break;
            case 24: mf.r = 0xFF0000; mf.g = 0xFF00; mf.b = 0xFF; break;
            case 32:
                // The fourth byte is "reserved" in the format, but files converted from 32-bit TGAs carry their alpha
                // there (Thrawn's Revenge MT_Commandbar.tga is such a BMP), so it is kept; a file whose fourth byte is
                // zero everywhere is opaque [port: inferred, as most BMP readers do].
                mf.r = 0xFF0000; mf.g = 0xFF00; mf.b = 0xFF; mf.a = 0xFF000000u;
                all_zero_alpha_is_opaque = true;
                break;
            default: return fail(error, "unsupported BMP bit depth " + std::to_string(bpp) + " (BI_RGB)");
        }
    } else if (comp == 3 || comp == 6) {  // BI_BITFIELDS / BI_ALPHABITFIELDS
        if (bpp != 16 && bpp != 32) return fail(error, "unsupported BMP bit depth " + std::to_string(bpp) + " (bit fields)");
        // Masks sit inside a V2+ header (offset 40 of the DIB header), else right after a 40-byte one.
        const std::size_t m = 14 + 40;
        const bool alpha = comp == 6 || hsz >= 56;
        if (file.size() < m + (alpha ? 16u : 12u)) return fail(error, "BMP bit-field masks truncated");
        mf.r = rd32(file, m);
        mf.g = rd32(file, m + 4);
        mf.b = rd32(file, m + 8);
        mf.a = alpha ? rd32(file, m + 12) : 0;
        if (bpp == 16) {
            mf.r &= 0xFFFFu; mf.g &= 0xFFFFu; mf.b &= 0xFFFFu; mf.a &= 0xFFFFu;
        }
    } else {
        return fail(error, "unsupported BMP compression " + std::to_string(comp));
    }

    const std::uint32_t spp = bpp / 8;
    const std::uint64_t stride = (std::uint64_t(width) * bpp + 31) / 32 * 4;
    const std::uint64_t need = stride * (height - 1) + std::uint64_t(width) * spp;  // last row may lack its padding
    if (pix_off < 26 || pix_off > file.size() || file.size() - pix_off < need) return fail(error, "BMP pixel data truncated");
    if (!within_budget(TextureFormat::BGRA8, width, height, 1, 1, error)) return false;

    TextureAsset t;
    t.format = TextureFormat::BGRA8;
    t.data.resize(std::size_t(width) * height * 4);
    const Channel cr(mf.r), cg(mf.g), cb(mf.b), ca(mf.a);
    bool any_alpha = false;
    for (std::uint32_t y = 0; y < height; ++y) {
        const std::uint32_t sy = top_down ? y : height - 1 - y;
        const std::uint8_t* s = file.data() + pix_off + sy * stride;
        std::uint8_t* d = t.data.data() + std::size_t(y) * width * 4;
        for (std::uint32_t x = 0; x < width; ++x, s += spp, d += 4) {
            std::uint32_t px = 0;
            std::memcpy(&px, s, spp);
            const int r = cr.get(px), g = cg.get(px), b = cb.get(px), a = ca.get(px);
            d[0] = static_cast<std::uint8_t>(b < 0 ? 0 : b);
            d[1] = static_cast<std::uint8_t>(g < 0 ? 0 : g);
            d[2] = static_cast<std::uint8_t>(r < 0 ? 0 : r);
            d[3] = static_cast<std::uint8_t>(a < 0 ? 255 : a);
            any_alpha = any_alpha || a > 0;
        }
    }
    std::vector<std::uint8_t>().swap(file);
    if (all_zero_alpha_is_opaque && !any_alpha)
        for (std::size_t i = 3; i < t.data.size(); i += 4) t.data[i] = 255;
    t.width = width;
    t.height = height;
    t.mip_levels = 1;
    t.layers = 1;
    t.levels.push_back({0, 0, width, height, 0, t.data.size()});
    generate_mips(t);
    out = std::move(t);
    return true;
}

bool decode_png(std::vector<std::uint8_t> file, TextureAsset& out, std::string* error) {
    std::uint32_t w = 0, h = 0;
    std::vector<std::uint8_t> px;
    if (!png::decode(file, w, h, px, kMaxDecodedTextureBytes / 4, error)) return false;
    std::vector<std::uint8_t>().swap(file);
    if (!within_budget(TextureFormat::BGRA8, w, h, 1, 1, error)) return false;
    TextureAsset t;
    t.format = TextureFormat::BGRA8;
    t.data = std::move(px);
    t.width = w;
    t.height = h;
    t.mip_levels = 1;
    t.layers = 1;
    t.levels.push_back({0, 0, w, h, 0, t.data.size()});
    generate_mips(t);
    out = std::move(t);
    return true;
}

TextureFileKind sniff_texture_file(std::span<const std::uint8_t> bytes) {
    if (bytes.size() >= 4 && bytes[0] == 'D' && bytes[1] == 'D' && bytes[2] == 'S' && bytes[3] == ' ') return TextureFileKind::Dds;
    if (png::is_png(bytes)) return TextureFileKind::Png;
    // A TGA cannot start with "BM": its second byte is the colour-map type (0 or 1), never 'M'.
    if (bytes.size() >= 2 && bytes[0] == 'B' && bytes[1] == 'M') return TextureFileKind::Bmp;
    return TextureFileKind::Tga;
}

bool decode_texture(std::vector<std::uint8_t> file, TextureAsset& out, std::string* error) {
    switch (sniff_texture_file(file)) {
        case TextureFileKind::Dds: return decode_dds(std::move(file), out, error);
        case TextureFileKind::Bmp: return decode_bmp(std::move(file), out, error);
        case TextureFileKind::Png: return decode_png(std::move(file), out, error);
        case TextureFileKind::Tga: break;
    }
    return decode_tga(std::move(file), out, error);
}

// ---- lookup ----------------------------------------------------------------------------------

const char* texture_lookup_step_name(TextureLookupStep s) {
    switch (s) {
        case TextureLookupStep::AsNamed: return "as named";
        case TextureLookupStep::TextureDir: return "Data\\Art\\Textures\\ + name";
        case TextureLookupStep::DdsAsNamed: return ".dds as named";
        case TextureLookupStep::DdsTextureDir: return "Data\\Art\\Textures\\ + .dds name";
    }
    return "?";
}

std::vector<TextureCandidate> texture_lookup(std::string_view name) {
    std::vector<TextureCandidate> out;
    const std::string s = normalize_path(name);
    if (s.empty()) return out;
    auto add = [&](std::string c, TextureLookupStep step) {
        for (const auto& o : out)
            if (o.path == c) return;
        out.push_back({std::move(c), step});
    };
    auto in_dir = [](const std::string& p) {
        const auto slash = p.find_last_of('\\');
        return normalize_path("Data\\Art\\Textures\\" + (slash == std::string::npos ? p : p.substr(slash + 1)));
    };
    add(s, TextureLookupStep::AsNamed);
    add(in_dir(s), TextureLookupStep::TextureDir);
    const std::string dds = s.substr(0, s.find('.')) + ".DDS";  // npos -> whole name
    add(dds, TextureLookupStep::DdsAsNamed);
    add(in_dir(dds), TextureLookupStep::DdsTextureDir);
    return out;
}

std::vector<std::string> texture_candidates(std::string_view name) {
    std::vector<std::string> out;
    for (auto& c : texture_lookup(name)) out.push_back(std::move(c.path));
    return out;
}

bool load_texture(const FileProvider& files, std::string_view name, TextureAsset& out, std::string* error,
                  std::string* resolved_path, TextureLookupStep* resolved_step) {
    for (const auto& c : texture_lookup(name)) {
        std::vector<std::uint8_t> bytes;
        if (!files.read(c.path, bytes)) continue;
        std::string e;
        TextureAsset t;
        if (!decode_texture(std::move(bytes), t, &e)) return fail(error, c.path + ": " + e);
        t.name = c.path;
        out = std::move(t);
        if (resolved_path) *resolved_path = c.path;
        if (resolved_step) *resolved_step = c.step;
        return true;
    }
    return fail(error, "not found: " + std::string(name));
}

TextureAsset make_fallback_texture(TextureRole role) {
    TextureAsset t;
    t.name = "<fallback>";
    t.is_fallback = true;
    t.format = TextureFormat::RGBA8;
    const std::uint32_t n = role == TextureRole::Color ? 256 : 4;
    t.width = t.height = n;
    t.mip_levels = 1;
    t.layers = 1;
    t.data.resize(std::size_t(n) * n * 4);
    for (std::uint32_t y = 0; y < n; ++y) {
        for (std::uint32_t x = 0; x < n; ++x) {
            std::uint8_t* d = t.data.data() + (std::size_t(y) * n + x) * 4;
            if (role == TextureRole::Normal) {
                d[0] = 128; d[1] = 128; d[2] = 255; d[3] = 0;
            } else if (role == TextureRole::White) {
                d[0] = d[1] = d[2] = d[3] = 255;
            } else {
                const bool on = ((x / 32) + (y / 32)) % 2 == 0;
                d[0] = on ? 255 : 0; d[1] = 0; d[2] = on ? 255 : 0; d[3] = 255;
            }
        }
    }
    t.levels.push_back({0, 0, n, n, 0, t.data.size()});
    if (role == TextureRole::Color) generate_mips(t);
    return t;
}

TextureRole texture_role_for_param(std::string_view p) {
    const std::string u = normalize_name(p);
    if (u.find("NORMAL") != std::string::npos || u.find("BUMP") != std::string::npos) return TextureRole::Normal;
    return TextureRole::Color;
}

}  // namespace alamo::assets
