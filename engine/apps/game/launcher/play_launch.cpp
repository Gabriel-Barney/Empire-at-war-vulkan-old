// Play.exe's testable half (play_launch.hpp).
#include "play_launch.hpp"

#include <cstdio>
#include <cwctype>
#include <exception>
#include <system_error>

#include "alamo/core/host_fs.hpp"
#include "alamo/vfs/host_path.hpp"
#include "alamo/vfs/install_locations.hpp"
#include "alamo/vfs/vfs.hpp"  // is_mod_arg

namespace alamo::play {
namespace fs = std::filesystem;

namespace {

bool dir_exists(const fs::path& p) {
    std::error_code ec;
    return fs::is_directory(p, ec);
}

bool path_exists(const fs::path& p) {
    std::error_code ec;
    return fs::exists(p, ec);
}

std::wstring trim(std::wstring s) {
    const auto space = [](wchar_t c) { return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n' || c == L'"'; };
    size_t b = 0;
    while (b < s.size() && space(s[b])) ++b;
    size_t e = s.size();
    while (e > b && space(s[e - 1])) --e;
    return s.substr(b, e - b);
}

bool iequals(const std::wstring& a, const wchar_t* b) {
    const std::wstring bb(b);
    if (a.size() != bb.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::towlower(a[i]) != std::towlower(bb[i])) return false;
    return true;
}

}  // namespace

bool is_game_dir(const fs::path& dir) { return vfs::is_foc_dir(vfs::native_utf8(dir)); }

fs::path normalize_game_dir(std::wstring text) { return path_from_utf8(vfs::normalize_foc_dir(vfs::native_utf8(fs::path(trim(std::move(text)))))); }

std::wstring first_line_utf8(std::string_view bytes) {
    if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xEF && static_cast<unsigned char>(bytes[1]) == 0xBB &&
        static_cast<unsigned char>(bytes[2]) == 0xBF)
        bytes.remove_prefix(3);
    const size_t eol = bytes.find_first_of("\r\n");
    if (eol != std::string_view::npos) bytes = bytes.substr(0, eol);
    std::u8string u8;
    u8.reserve(bytes.size());
    for (const char c : bytes) u8.push_back(static_cast<char8_t>(c));
    try {
        return fs::path(u8).wstring();
    } catch (const std::exception&) {  // not UTF-8
        return {};
    }
}

fs::path find_game(const FindInputs& in, std::vector<std::string>* steam_libraries) {
    // ums1 t01: one search (vfs::find_install) for every launch surface; this side only reads game_path.txt.
    vfs::InstallInputs v;
    if (!in.saved_file.empty() && path_exists(in.saved_file)) {
        std::string bytes;
        if (FILE* f = nullptr; _wfopen_s(&f, in.saved_file.c_str(), L"rb") == 0 && f) {
            char buf[4096];
            size_t n = 0;
            while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) bytes.append(buf, n);
            std::fclose(f);
        }
        v.user_folders.push_back(vfs::native_utf8(fs::path(first_line_utf8(bytes))));
    }
    v.user_folders.push_back(vfs::native_utf8(fs::path(in.env)));
    for (const fs::path& r : in.steam_roots) v.steam_roots.push_back(vfs::native_utf8(r));
    for (const fs::path& lib : in.steam) v.steam_libraries.push_back(vfs::native_utf8(lib));
    for (const fs::path& dir : in.fixed) v.fixed.push_back(vfs::native_utf8(dir));
    vfs::InstallLocations where = vfs::find_install(v);
    if (steam_libraries) *steam_libraries = std::move(where.steam_libraries);
    return path_from_utf8(where.foc);
}

std::vector<std::wstring> game_args(const fs::path& game, const fs::path& cache, const std::vector<std::wstring>& user) {
    std::vector<std::wstring> out{L"--game", game.wstring(), L"--cache-dir", cache.wstring()};
    for (const std::wstring& a : user) {
        fs::path p(trim(a));
        if (!a.empty() && dir_exists(p)) {
            std::error_code ec;
            fs::path abs = fs::absolute(p, ec);
            if (ec) abs = p;
            abs = abs.lexically_normal();
            if (!abs.has_filename()) abs = abs.parent_path();  // "...\Mod\" -> "...\Mod"
            if (iequals(abs.filename().wstring(), L"Data")) abs = abs.parent_path();
            if (dir_exists(abs / L"Data")) {
                out.push_back(L"MODPATH=" + abs.wstring());
                continue;
            }
        }
        out.push_back(a);
    }
    return out;
}

std::optional<std::vector<std::wstring>> stack_mod_args(const std::vector<std::wstring>& args, const ModStackResolver& resolve) {
    std::vector<std::wstring> out;
    std::vector<std::string> picked;
    for (const std::wstring& a : args) {
        std::string u = vfs::native_utf8(fs::path(a));
        if (vfs::is_mod_arg(u)) picked.push_back(std::move(u));  // ums4 t04: the one is_mod_arg
        else out.push_back(a);
    }
    if (picked.empty()) return args;
    const std::optional<std::vector<std::string>> launch = resolve(picked);
    if (!launch) return std::nullopt;
    for (const std::string& a : *launch) out.push_back(path_from_utf8(a).wstring());
    return out;
}

std::wstring quote_arg(const std::wstring& arg) {
    if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) return arg;
    std::wstring out = L"\"";
    for (size_t i = 0;; ++i) {
        size_t slashes = 0;
        while (i < arg.size() && arg[i] == L'\\') {
            ++i;
            ++slashes;
        }
        if (i == arg.size()) {  // before the closing quote: every backslash doubled
            out.append(slashes * 2, L'\\');
            break;
        }
        if (arg[i] == L'"') {
            out.append(slashes * 2 + 1, L'\\');
            out.push_back(L'"');
        } else {
            out.append(slashes, L'\\');
            out.push_back(arg[i]);
        }
    }
    out.push_back(L'"');
    return out;
}

std::wstring command_line(const fs::path& exe, const std::vector<std::wstring>& args) {
    std::wstring line = quote_arg(exe.wstring());
    for (const std::wstring& a : args) line += L" " + quote_arg(a);
    return line;
}

}  // namespace alamo::play
