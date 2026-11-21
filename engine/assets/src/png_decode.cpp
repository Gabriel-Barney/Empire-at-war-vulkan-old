// PNG reader (png_decode.hpp).
#include "png_decode.hpp"

#include <array>
#include <cstdlib>
#include <cstring>

namespace alamo::assets::png {
namespace {

bool fail(std::string* e, const char* m) {
    if (e) *e = m;
    return false;
}

// ---- inflate ----

struct Bits {
    std::span<const std::uint8_t> d;
    std::size_t pos = 0;
    std::uint32_t buf = 0;
    int cnt = 0;
    bool over = false;
    std::uint32_t get(int n) {
        while (cnt < n) {
            std::uint32_t b = 0;
            if (pos < d.size()) b = d[pos++];
            else over = true;
            buf |= b << cnt;
            cnt += 8;
        }
        const std::uint32_t v = n == 0 ? 0u : (buf & ((1u << n) - 1u));
        buf = n == 0 ? buf : (buf >> n);
        cnt -= n;
        return v;
    }
    void align() {
        buf = 0;
        cnt = 0;
    }
};

// Canonical Huffman code: counts per length, symbols in code order.
struct Huff {
    std::array<std::uint16_t, 16> count{};
    std::array<std::uint16_t, 320> sym{};
    bool build(const std::uint8_t* len, int n) {
        count.fill(0);
        for (int i = 0; i < n; ++i) ++count[len[i]];
        count[0] = 0;
        int left = 1;
        for (int l = 1; l < 16; ++l) {
            left = (left << 1) - count[static_cast<std::size_t>(l)];
            if (left < 0) return false;  // over-subscribed
        }
        std::array<std::uint16_t, 16> offs{};
        for (int l = 1; l < 15; ++l)
            offs[static_cast<std::size_t>(l + 1)] =
                static_cast<std::uint16_t>(offs[static_cast<std::size_t>(l)] + count[static_cast<std::size_t>(l)]);
        for (int i = 0; i < n; ++i)
            if (len[i]) sym[offs[len[i]]++] = static_cast<std::uint16_t>(i);
        return true;
    }
    int decode(Bits& b) const {
        int code = 0, first = 0, index = 0;
        for (int l = 1; l < 16; ++l) {
            code |= static_cast<int>(b.get(1));
            const int c = count[static_cast<std::size_t>(l)];
            if (code - c < first) return sym[static_cast<std::size_t>(index + (code - first))];
            index += c;
            first = (first + c) << 1;
            code <<= 1;
            if (b.over) return -1;
        }
        return -1;
    }
};

constexpr std::uint16_t kLenBase[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                        31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr std::uint8_t kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr std::uint16_t kDistBase[30] = {1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
                                         193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
constexpr std::uint8_t kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

bool codes(Bits& b, std::vector<std::uint8_t>& out, std::size_t cap, const Huff& lit, const Huff& dist) {
    for (;;) {
        const int s = lit.decode(b);
        if (s < 0 || b.over) return false;
        if (s < 256) {
            if (out.size() >= cap) return false;
            out.push_back(static_cast<std::uint8_t>(s));
        } else if (s == 256) {
            return true;
        } else {
            const int li = s - 257;
            if (li >= 29) return false;
            const std::size_t len = kLenBase[li] + b.get(kLenExtra[li]);
            const int di = dist.decode(b);
            if (di < 0 || di >= 30) return false;
            const std::size_t d = kDistBase[di] + b.get(kDistExtra[di]);
            if (b.over || d > out.size() || out.size() + len > cap) return false;
            const std::size_t start = out.size() - d;
            for (std::size_t i = 0; i < len; ++i) out.push_back(out[start + i]);
        }
    }
}

// zlib stream -> at most `cap` bytes.
bool inflate(std::span<const std::uint8_t> in, std::size_t cap, std::vector<std::uint8_t>& out) {
    out.clear();
    if (in.size() < 2 || (in[0] & 0x0F) != 8 || ((in[0] << 8) | in[1]) % 31 != 0 || (in[1] & 0x20)) return false;
    Bits b{in.subspan(2)};
    Huff flit, fdist;
    {
        std::uint8_t l[288];
        for (int i = 0; i < 288; ++i) l[i] = static_cast<std::uint8_t>(i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8);
        flit.build(l, 288);
        std::uint8_t d[30];
        std::memset(d, 5, sizeof d);
        fdist.build(d, 30);
    }
    std::uint32_t last = 0;
    do {
        last = b.get(1);
        const std::uint32_t type = b.get(2);
        if (b.over) return false;
        if (type == 0) {
            b.align();
            if (b.pos + 4 > b.d.size()) return false;
            const std::size_t len = b.d[b.pos] | (std::size_t(b.d[b.pos + 1]) << 8);
            const std::size_t nlen = b.d[b.pos + 2] | (std::size_t(b.d[b.pos + 3]) << 8);
            if ((len ^ 0xFFFFu) != nlen) return false;
            b.pos += 4;
            if (b.pos + len > b.d.size() || out.size() + len > cap) return false;
            out.insert(out.end(), b.d.begin() + static_cast<std::ptrdiff_t>(b.pos),
                       b.d.begin() + static_cast<std::ptrdiff_t>(b.pos + len));
            b.pos += len;
        } else if (type == 1) {
            if (!codes(b, out, cap, flit, fdist)) return false;
        } else if (type == 2) {
            const int nlen = static_cast<int>(b.get(5)) + 257, ndist = static_cast<int>(b.get(5)) + 1,
                      ncode = static_cast<int>(b.get(4)) + 4;
            if (nlen > 286 || ndist > 30) return false;
            static constexpr std::uint8_t kOrder[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
            std::uint8_t lengths[320] = {};
            for (int i = 0; i < ncode; ++i) lengths[kOrder[i]] = static_cast<std::uint8_t>(b.get(3));
            Huff lc;
            if (!lc.build(lengths, 19)) return false;
            std::memset(lengths, 0, sizeof lengths);
            int idx = 0;
            while (idx < nlen + ndist) {
                const int s = lc.decode(b);
                if (s < 0) return false;
                if (s < 16) {
                    lengths[idx++] = static_cast<std::uint8_t>(s);
                    continue;
                }
                std::uint8_t v = 0;
                int rep = 0;
                if (s == 16) {
                    if (idx == 0) return false;
                    v = lengths[idx - 1];
                    rep = 3 + static_cast<int>(b.get(2));
                } else if (s == 17) {
                    rep = 3 + static_cast<int>(b.get(3));
                } else {
                    rep = 11 + static_cast<int>(b.get(7));
                }
                if (idx + rep > nlen + ndist) return false;
                while (rep-- > 0) lengths[idx++] = v;
            }
            Huff lit, dist;
            if (!lit.build(lengths, nlen) || !dist.build(lengths + nlen, ndist)) return false;
            if (!codes(b, out, cap, lit, dist)) return false;
        } else {
            return false;
        }
    } while (!last);
    return true;
}

std::uint32_t be32(const std::uint8_t* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3];
}

int paeth(int a, int b, int c) {
    const int p = a + b - c, pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
    return (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
}

}  // namespace

bool decode(std::span<const std::uint8_t> f, std::uint32_t& width, std::uint32_t& height, std::vector<std::uint8_t>& bgra,
            std::uint64_t max_pixels, std::string* err) {
    if (!is_png(f)) return fail(err, "not a PNG file");
    std::size_t p = 8;
    std::uint32_t w = 0, h = 0;
    std::uint8_t depth = 0, ctype = 0, interlace = 0;
    bool have_hdr = false;
    std::vector<std::uint8_t> idat, plte, trns;
    while (p + 12 <= f.size()) {
        const std::uint32_t len = be32(&f[p]);
        if (len > f.size() - p - 12) return fail(err, "PNG chunk overruns the file");
        const std::uint8_t* type = &f[p + 4];
        const std::uint8_t* d = &f[p + 8];
        if (std::memcmp(type, "IHDR", 4) == 0) {
            if (len < 13) return fail(err, "PNG IHDR too short");
            w = be32(d);
            h = be32(d + 4);
            depth = d[8];
            ctype = d[9];
            interlace = d[12];
            if (d[10] != 0 || d[11] != 0) return fail(err, "unsupported PNG compression or filter method");
            have_hdr = true;
        } else if (std::memcmp(type, "PLTE", 4) == 0) {
            plte.assign(d, d + len);
        } else if (std::memcmp(type, "tRNS", 4) == 0) {
            trns.assign(d, d + len);
        } else if (std::memcmp(type, "IDAT", 4) == 0) {
            idat.insert(idat.end(), d, d + len);
        } else if (std::memcmp(type, "IEND", 4) == 0) {
            break;
        }
        p += 12 + std::size_t(len);
    }
    if (!have_hdr) return fail(err, "PNG without IHDR");
    if (w == 0 || h == 0 || w > 32768 || h > 32768) return fail(err, "bad PNG size");
    if (std::uint64_t(w) * h > max_pixels) return fail(err, "PNG too large");
    if (interlace > 1) return fail(err, "unknown PNG interlace method");
    int channels = 0;
    switch (ctype) {
        case 0: channels = 1; if (depth != 1 && depth != 2 && depth != 4 && depth != 8 && depth != 16) return fail(err, "bad PNG bit depth"); break;
        case 2: channels = 3; if (depth != 8 && depth != 16) return fail(err, "bad PNG bit depth"); break;
        case 3: channels = 1; if (depth != 1 && depth != 2 && depth != 4 && depth != 8) return fail(err, "bad PNG bit depth"); break;
        case 4: channels = 2; if (depth != 8 && depth != 16) return fail(err, "bad PNG bit depth"); break;
        case 6: channels = 4; if (depth != 8 && depth != 16) return fail(err, "bad PNG bit depth"); break;
        default: return fail(err, "bad PNG colour type");
    }
    if (ctype == 3 && (plte.empty() || plte.size() % 3 != 0)) return fail(err, "PNG palette missing");
    const std::size_t bits_pp = std::size_t(channels) * depth;
    const std::size_t bpp = (bits_pp + 7) / 8;  // filter distance in bytes

    // Passes: Adam7 or one full pass.
    struct Pass {
        std::uint32_t x0, y0, dx, dy;
    };
    static constexpr Pass kAdam7[7] = {{0, 0, 8, 8}, {4, 0, 8, 8}, {0, 4, 4, 8}, {2, 0, 4, 4},
                                       {0, 2, 2, 4}, {1, 0, 2, 2}, {0, 1, 1, 2}};
    static constexpr Pass kOne[1] = {{0, 0, 1, 1}};
    const Pass* passes = interlace ? kAdam7 : kOne;
    const int npass = interlace ? 7 : 1;
    std::uint64_t expect = 0;
    for (int i = 0; i < npass; ++i) {
        const Pass& ps = passes[i];
        if (ps.x0 >= w || ps.y0 >= h) continue;
        const std::uint64_t pw = (w - ps.x0 + ps.dx - 1) / ps.dx, ph = (h - ps.y0 + ps.dy - 1) / ps.dy;
        expect += ph * (1 + (pw * bits_pp + 7) / 8);
    }
    std::vector<std::uint8_t> raw;
    if (!inflate(idat, static_cast<std::size_t>(expect), raw)) return fail(err, "PNG image data does not inflate");
    if (raw.size() < expect) return fail(err, "PNG image data short");

    std::vector<std::uint8_t> out(std::size_t(w) * h * 4, 0);
    auto sample = [&](const std::uint8_t* row, std::uint64_t x, int c) -> std::uint32_t {  // native depth value
        if (depth == 16) return (std::uint32_t(row[(x * std::uint64_t(channels) + std::uint64_t(c)) * 2]) << 8) |
                                row[(x * std::uint64_t(channels) + std::uint64_t(c)) * 2 + 1];
        if (depth == 8) return row[x * std::uint64_t(channels) + std::uint64_t(c)];
        const std::uint64_t bit = x * depth;  // channels == 1 below 8 bits
        return (row[bit / 8] >> (8 - depth - (bit % 8))) & ((1u << depth) - 1u);
    };
    auto to8 = [&](std::uint32_t v) -> std::uint8_t {
        if (depth == 16) return static_cast<std::uint8_t>(v >> 8);
        if (depth == 8) return static_cast<std::uint8_t>(v);
        return static_cast<std::uint8_t>(v * 255u / ((1u << depth) - 1u));
    };
    const std::uint32_t key_g = trns.size() >= 2 ? (std::uint32_t(trns[0]) << 8) | trns[1] : 0xFFFFFFFFu;
    const bool key_rgb = ctype == 2 && trns.size() >= 6;

    std::size_t at = 0;
    for (int i = 0; i < npass; ++i) {
        const Pass& ps = passes[i];
        if (ps.x0 >= w || ps.y0 >= h) continue;
        const std::uint32_t pw = (w - ps.x0 + ps.dx - 1) / ps.dx, ph = (h - ps.y0 + ps.dy - 1) / ps.dy;
        const std::size_t stride = (std::size_t(pw) * bits_pp + 7) / 8;
        std::vector<std::uint8_t> cur(stride), prev(stride, 0);
        for (std::uint32_t y = 0; y < ph; ++y) {
            const std::uint8_t filter = raw[at];
            const std::uint8_t* line = &raw[at + 1];
            at += 1 + stride;
            for (std::size_t x = 0; x < stride; ++x) {
                const int a = x >= bpp ? cur[x - bpp] : 0, b = prev[x], c = x >= bpp ? prev[x - bpp] : 0;
                int v = line[x];
                switch (filter) {
                    case 0: break;
                    case 1: v += a; break;
                    case 2: v += b; break;
                    case 3: v += (a + b) / 2; break;
                    case 4: v += paeth(a, b, c); break;
                    default: return fail(err, "bad PNG filter type");
                }
                cur[x] = static_cast<std::uint8_t>(v);
            }
            const std::uint64_t oy = ps.y0 + std::uint64_t(y) * ps.dy;
            for (std::uint32_t x = 0; x < pw; ++x) {
                const std::uint64_t ox = ps.x0 + std::uint64_t(x) * ps.dx;
                std::uint8_t* o = &out[(oy * w + ox) * 4];
                std::uint8_t r = 0, g = 0, bl = 0, al = 255;
                switch (ctype) {
                    case 0: {
                        const std::uint32_t v = sample(cur.data(), x, 0);
                        r = g = bl = to8(v);
                        if (v == key_g) al = 0;
                        break;
                    }
                    case 2: {
                        const std::uint32_t vr = sample(cur.data(), x, 0), vg = sample(cur.data(), x, 1), vb = sample(cur.data(), x, 2);
                        r = to8(vr);
                        g = to8(vg);
                        bl = to8(vb);
                        if (key_rgb && vr == ((std::uint32_t(trns[0]) << 8) | trns[1]) && vg == ((std::uint32_t(trns[2]) << 8) | trns[3]) &&
                            vb == ((std::uint32_t(trns[4]) << 8) | trns[5]))
                            al = 0;
                        break;
                    }
                    case 3: {
                        const std::uint32_t idx = sample(cur.data(), x, 0);
                        if (idx * 3 + 2 < plte.size()) {  // an index past the palette is black (libpng warns, keeps going)
                            r = plte[idx * 3];
                            g = plte[idx * 3 + 1];
                            bl = plte[idx * 3 + 2];
                        }
                        if (idx < trns.size()) al = trns[idx];
                        break;
                    }
                    case 4:
                        r = g = bl = to8(sample(cur.data(), x, 0));
                        al = to8(sample(cur.data(), x, 1));
                        break;
                    default:
                        r = to8(sample(cur.data(), x, 0));
                        g = to8(sample(cur.data(), x, 1));
                        bl = to8(sample(cur.data(), x, 2));
                        al = to8(sample(cur.data(), x, 3));
                        break;
                }
                o[0] = bl;
                o[1] = g;
                o[2] = r;
                o[3] = al;
            }
            std::swap(cur, prev);
        }
    }
    width = w;
    height = h;
    bgra = std::move(out);
    return true;
}

}  // namespace alamo::assets::png
