// alamo_fuzz_smoke - a seeded, deterministic mutation fuzzer over fuzz_targets.hpp (mcx2 t08, MOD_COMPAT_DESIGN.md
// 11.3). No third-party fuzzer: it runs in every tree (MSVC and clang, a slow-labelled ctest entry) and under the ASan
// build; build_fuzz.cmd's libFuzzer targets explore further with coverage feedback.
//
//   alamo_fuzz_smoke [--seconds S] [--iterations N] [--only a,b] [--seed U64] [--out DIR] [--no-data] [--corpus DIR]
//   alamo_fuzz_smoke --repro FILE --target NAME
//
// Seeds per target: its synthetic inputs, then (unless --no-data) real files: up to 12 of the smallest from the game
// (ALAMO_FOC_DATA through the VFS, MEGs included) and up to 12 from the installed Steam workshop mods' loose Data
// folders, each cut to 256 KB; --corpus DIR adds DIR\<target>\*. Each iteration takes a seed, applies 1-4 mutations
// (bit flips, interesting bytes and u32 size fields, truncation, slice duplication / deletion, random inserts, a splice
// with another seed) from a splitmix64 stream seeded by --seed, and runs the target. The same arguments give the same
// inputs. A crash or an escaping exception writes the input to <out>\crash_<target>.bin (reproduce with --repro); the
// process exits 1 on an escaping exception, and a crash ends it (ctest reports the failure).
#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "alamo/core/host_fs.hpp"
#include "fuzz_targets.hpp"

#if defined(ALAMO_FUZZ_HAVE_VFS)
#include "alamo/vfs/steam_workshop.hpp"
#include "alamo/vfs/vfs.hpp"
#endif

#if defined(_WIN32)
#include <windows.h>
#endif

namespace fs = std::filesystem;
using alamo::fuzz::Bytes;

namespace {

constexpr size_t kMaxSeedBytes = 256 * 1024;
constexpr size_t kMaxInputBytes = 1024 * 1024;

struct Rng {
    uint64_t s;
    uint64_t next() {
        uint64_t z = (s += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    size_t below(size_t n) { return n ? static_cast<size_t>(next() % n) : 0; }
};

// The input being run, written out when the process crashes.
const Bytes* g_current = nullptr;
std::string g_crash_path;

void dump_current() {
    if (!g_current || g_crash_path.empty()) return;
    if (FILE* f = std::fopen(g_crash_path.c_str(), "wb")) {
        if (!g_current->empty()) std::fwrite(g_current->data(), 1, g_current->size(), f);
        std::fclose(f);
        std::fprintf(stderr, "fuzz_smoke: input written to %s\n", g_crash_path.c_str());
    }
}

#if defined(_WIN32)
LONG WINAPI on_crash(EXCEPTION_POINTERS* e) {
    std::fprintf(stderr, "fuzz_smoke: CRASH (exception 0x%08lx)\n", e ? e->ExceptionRecord->ExceptionCode : 0ul);
    dump_current();
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif
void on_abort(int) {
    std::fprintf(stderr, "fuzz_smoke: CRASH (abort)\n");
    dump_current();
}

void mutate(Bytes& b, Rng& r, const std::vector<Bytes>& seeds) {
    static const uint8_t kBytes[] = {0x00, 0x01, 0x7F, 0x80, 0xFF, 0xFE, 0x40, 0x20};
    const int n = 1 + static_cast<int>(r.below(4));
    for (int k = 0; k < n; ++k) {
        switch (r.below(8)) {
            case 0:
                if (!b.empty()) b[r.below(b.size())] ^= static_cast<uint8_t>(1u << r.below(8));
                break;
            case 1:
                if (!b.empty()) b[r.below(b.size())] = kBytes[r.below(sizeof kBytes)];
                break;
            case 2:
                if (b.size() >= 4) {
                    const size_t at = r.below(b.size() - 3);
                    uint32_t v = 0;
                    std::memcpy(&v, b.data() + at, 4);
                    const uint32_t picks[] = {0u, 1u, 0x7FFFFFFFu, 0x80000000u, 0xFFFFFFFFu,
                                              static_cast<uint32_t>(b.size()), static_cast<uint32_t>(b.size() + 1), v + 1,
                                              v - 1, v * 2u, v | 0x80000000u};
                    v = picks[r.below(sizeof picks / sizeof picks[0])];
                    std::memcpy(b.data() + at, &v, 4);
                }
                break;
            case 3:
                if (!b.empty()) b.resize(r.below(b.size()));
                break;
            case 4:
                if (!b.empty() && b.size() < kMaxInputBytes / 2) {
                    const size_t from = r.below(b.size()), len = 1 + r.below(std::min<size_t>(256, b.size() - from));
                    const Bytes slice(b.begin() + static_cast<std::ptrdiff_t>(from),
                                      b.begin() + static_cast<std::ptrdiff_t>(from + len));
                    b.insert(b.begin() + static_cast<std::ptrdiff_t>(r.below(b.size() + 1)), slice.begin(), slice.end());
                }
                break;
            case 5:
                if (!b.empty()) {
                    const size_t from = r.below(b.size()), len = 1 + r.below(std::min<size_t>(64, b.size() - from));
                    b.erase(b.begin() + static_cast<std::ptrdiff_t>(from), b.begin() + static_cast<std::ptrdiff_t>(from + len));
                }
                break;
            case 6: {
                const size_t len = 1 + r.below(16), at = r.below(b.size() + 1);
                Bytes ins(len);
                for (auto& c : ins) c = static_cast<uint8_t>(r.next());
                b.insert(b.begin() + static_cast<std::ptrdiff_t>(at), ins.begin(), ins.end());
                break;
            }
            default: {
                const Bytes& o = seeds[r.below(seeds.size())];
                if (o.empty()) break;
                const size_t cut = r.below(b.size() + 1), from = r.below(o.size());
                b.resize(cut);
                b.insert(b.end(), o.begin() + static_cast<std::ptrdiff_t>(from), o.end());
                break;
            }
        }
    }
    if (b.size() > kMaxInputBytes) b.resize(kMaxInputBytes);
}

bool read_file(const fs::path& p, Bytes& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    size_t a = 0;
    while (a <= s.size()) {
        const size_t b = s.find(sep, a);
        out.push_back(s.substr(a, b == std::string::npos ? std::string::npos : b - a));
        if (b == std::string::npos) break;
        a = b + 1;
    }
    return out;
}

// '*' / '?' wildcard match of a file name, ASCII case-insensitive.
bool name_match(std::string_view pat, std::string_view s) {
    if (pat.empty()) return s.empty();
    if (pat[0] == '*') {
        for (size_t i = 0; i <= s.size(); ++i)
            if (name_match(pat.substr(1), s.substr(i))) return true;
        return false;
    }
    if (s.empty()) return false;
    const auto up = [](char c) { return static_cast<char>(c >= 'a' && c <= 'z' ? c - 32 : c); };
    return (pat[0] == '?' || up(pat[0]) == up(s[0])) && name_match(pat.substr(1), s.substr(1));
}

void keep_smallest(std::vector<std::pair<uint64_t, fs::path>>& v, size_t n) {
    std::sort(v.begin(), v.end());
    if (v.size() > n) v.resize(n);
}

// Real-file seeds: the game's (VFS, MEGs included) and the installed workshop mods' loose files.
void data_seeds(const alamo::fuzz::Target& t, std::vector<Bytes>& seeds, int& from_game, int& from_mods) {
#if defined(ALAMO_FUZZ_HAVE_VFS)
    const char* foc = std::getenv("ALAMO_FOC_DATA");
    if (!foc || !*foc) return;
    alamo::vfs::VfsConfig cfg;
    cfg.foc_root = foc;
    auto v = alamo::vfs::Vfs::create(cfg);
    const auto globs = split(t.globs, ';');
    if (v) {
        // Up to 48 names spread over each glob's listing, read once; the 12 smallest are kept.
        std::vector<std::pair<uint64_t, std::string>> found;
        for (const auto& g : globs) {
            const auto names = v->list(g);
            const size_t step = std::max<size_t>(1, names.size() / 48);
            for (size_t i = 0; i < names.size(); i += step) {
                Bytes b;
                if (v->read(names[i], b)) found.push_back({b.size(), names[i]});
            }
        }
        std::sort(found.begin(), found.end());
        for (size_t i = 0; i < found.size() && from_game < 12; ++i) {
            Bytes b;
            if (!v->read(found[i].second, b) || b.empty()) continue;
            if (b.size() > kMaxSeedBytes) b.resize(kMaxSeedBytes);
            seeds.push_back(std::move(b));
            ++from_game;
        }
    }
    // Workshop mods: <library>\steamapps\workshop\content\32470\<id>\Data\...
    const auto libs = alamo::vfs::find_steam_libraries(foc, alamo::vfs::default_steam_roots());
    std::vector<std::pair<uint64_t, fs::path>> files;
    for (const auto& lib : libs) {
        std::error_code ec;
        const fs::path content = alamo::path_from_utf8(lib) / "steamapps" / "workshop" / "content" / "32470";
        if (!fs::is_directory(content, ec)) continue;
        for (const auto& item : fs::directory_iterator(content, ec)) {
            for (const auto& g : globs) {
                const auto parts = split(g, '\\');
                if (parts.size() < 2) continue;
                fs::path dir = item.path();
                bool deep = false;
                for (size_t i = 0; i + 1 < parts.size(); ++i) {
                    if (parts[i] == "**") deep = true;
                    else dir /= parts[i];
                }
                if (!fs::is_directory(dir, ec)) continue;
                const std::string pat = parts.back();
                const auto consider = [&](const fs::directory_entry& e) {
                    if (files.size() < 4000 && e.is_regular_file(ec) && name_match(pat, e.path().filename().string()))
                        files.push_back({e.file_size(ec), e.path()});
                };
                if (deep)
                    for (const auto& e : fs::recursive_directory_iterator(dir, ec)) consider(e);
                else
                    for (const auto& e : fs::directory_iterator(dir, ec)) consider(e);
            }
        }
    }
    keep_smallest(files, 12);
    for (const auto& f : files) {
        Bytes b;
        if (!read_file(f.second, b) || b.empty()) continue;
        if (b.size() > kMaxSeedBytes) b.resize(kMaxSeedBytes);
        seeds.push_back(std::move(b));
        ++from_mods;
    }
#else
    (void)t, (void)seeds, (void)from_game, (void)from_mods;
#endif
}

}  // namespace

int main(int argc, char** argv) {
    double seconds = 6.0;
    long long iterations = 1000000;
    uint64_t seed = 0x6D63783274303830ull;  // "mcx2t080"
    std::string only, out_dir = ".", corpus, repro, repro_target;
    bool use_data = true;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        const auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : std::string(); };
        if (a == "--seconds") seconds = std::atof(next().c_str());
        else if (a == "--iterations") iterations = std::atoll(next().c_str());
        else if (a == "--only") only = next();
        else if (a == "--seed") seed = std::strtoull(next().c_str(), nullptr, 0);
        else if (a == "--out") out_dir = next();
        else if (a == "--corpus") corpus = next();
        else if (a == "--no-data") use_data = false;
        else if (a == "--repro") repro = next();
        else if (a == "--target") repro_target = next();
        else {
            std::fprintf(stderr, "unknown argument %s\n", a.c_str());
            return 2;
        }
    }
#if defined(_WIN32)
    SetUnhandledExceptionFilter(on_crash);
#endif
    std::signal(SIGABRT, on_abort);
    std::error_code ec;
    fs::create_directories(alamo::path_from_utf8(out_dir), ec);

    if (!repro.empty()) {
        const alamo::fuzz::Target* t = alamo::fuzz::find_target(repro_target);
        Bytes b;
        if (!t || !read_file(alamo::path_from_utf8(repro), b)) {
            std::fprintf(stderr, "--repro needs a readable file and a --target (%s)\n", repro_target.c_str());
            return 2;
        }
        t->run(b);
        std::printf("repro: %s ran %zu bytes without a crash\n", t->name, b.size());
        return 0;
    }

    const auto wanted = split(only, ',');
    int failures = 0;
    for (const auto& t : alamo::fuzz::targets()) {
        if (!only.empty() && std::find(wanted.begin(), wanted.end(), t.name) == wanted.end()) continue;
        std::vector<Bytes> seeds = t.synthetic();
        int from_game = 0, from_mods = 0, from_corpus = 0;
        if (use_data) data_seeds(t, seeds, from_game, from_mods);
        if (!corpus.empty()) {
            const fs::path d = alamo::path_from_utf8(corpus) / t.name;
            if (fs::is_directory(d, ec))
                for (const auto& e : fs::directory_iterator(d, ec)) {
                    Bytes b;
                    if (e.is_regular_file(ec) && read_file(e.path(), b)) {
                        seeds.push_back(std::move(b));
                        ++from_corpus;
                    }
                }
        }
        g_crash_path = (alamo::path_from_utf8(out_dir) / ("crash_" + std::string(t.name) + ".bin")).string();
        Rng r{seed ^ std::hash<std::string>{}(t.name)};
        const auto t0 = std::chrono::steady_clock::now();
        long long n = 0, escaped = 0;
        Bytes input;
        g_current = &input;
        // Every seed as is first, then mutations until the time or iteration budget is used.
        for (const auto& s : seeds) {
            input = s;
            try {
                t.run(input);
            } catch (const std::exception& e) {
                ++escaped;
                std::fprintf(stderr, "%s: seed raised %s\n", t.name, e.what());
                dump_current();
            }
        }
        for (; n < iterations; ++n) {
            if ((n & 63) == 0 &&
                std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() >= seconds)
                break;
            input = seeds[r.below(seeds.size())];
            mutate(input, r, seeds);
            try {
                t.run(input);
            } catch (const std::exception& e) {
                if (escaped++ == 0) {
                    std::fprintf(stderr, "%s: iteration %lld raised %s\n", t.name, n, e.what());
                    dump_current();
                }
            } catch (...) {
                if (escaped++ == 0) {
                    std::fprintf(stderr, "%s: iteration %lld raised a non-std exception\n", t.name, n);
                    dump_current();
                }
            }
        }
        g_current = nullptr;
        std::printf("%-9s %8lld inputs  seeds %zu (synthetic %zu, game %d, mods %d, corpus %d)  escaped %lld\n", t.name, n,
                    seeds.size(), seeds.size() - static_cast<size_t>(from_game + from_mods + from_corpus), from_game,
                    from_mods, from_corpus, escaped);
        if (escaped) ++failures;
    }
    std::printf("FUZZ_SMOKE %s (%d target(s) with escaping exceptions)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
