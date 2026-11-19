// Host path keys (ums4 t04 / t06). See include/alamo/vfs/host_path.hpp.
#include "alamo/vfs/host_path.hpp"

#include <system_error>

#include "alamo/core/host_fs.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace alamo::vfs {

namespace {

#ifdef _WIN32
// The final path of an existing folder (junctions, symbolic links and subst drives followed), "" when it cannot be
// opened. GetFinalPathNameByHandleW answers "\\?\C:\..." or "\\?\UNC\server\share\...": the prefix is dropped.
std::wstring final_path(const fs::path& p) {
    const HANDLE h = CreateFileW(long_path(p).c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                 OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h == INVALID_HANDLE_VALUE) return {};
    std::wstring out(512, L'\0');
    DWORD n = GetFinalPathNameByHandleW(h, out.data(), static_cast<DWORD>(out.size()), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (n >= out.size()) {
        out.resize(static_cast<size_t>(n) + 1);
        n = GetFinalPathNameByHandleW(h, out.data(), static_cast<DWORD>(out.size()), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    }
    CloseHandle(h);
    if (n == 0 || n >= out.size()) return {};
    out.resize(n);
    const std::wstring_view unc = L"\\\\?\\UNC\\", local = L"\\\\?\\";
    if (out.starts_with(unc)) return L"\\\\" + out.substr(unc.size());
    if (out.starts_with(local)) return out.substr(local.size());
    return out;
}
#endif

}  // namespace

std::string path_key(std::string_view path) {
    std::string k(path);
    for (char& c : k) {
        if (c == '/') c = '\\';
        else if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    while (k.size() > 1 && k.back() == '\\') k.pop_back();
    return k;
}

namespace {

fs::path absolute_normal(std::string_view path) {
    std::error_code ec;
    fs::path abs = fs::absolute(path_from_utf8(path), ec);
    if (ec) abs = path_from_utf8(path);
    return abs.lexically_normal();
}

}  // namespace

std::string lexical_folder_key(std::string_view path) {
    return path.empty() ? std::string() : path_key(native_utf8(absolute_normal(path)));
}

std::string folder_key(std::string_view path) {
    if (path.empty()) return {};
    std::error_code ec;
    fs::path abs = absolute_normal(path);
#ifdef _WIN32
    if (fs::is_directory(long_path(abs), ec)) {
        const std::wstring real = final_path(abs);
        if (!real.empty()) abs = fs::path(real);
    }
#endif
    return path_key(native_utf8(abs));
}

bool same_folder(std::string_view a, std::string_view b) { return folder_key(a) == folder_key(b); }

std::string native_utf8(const fs::path& p) {
    const std::u8string s = p.u8string();
    return std::string(reinterpret_cast<const char*>(s.data()), s.size());
}

}  // namespace alamo::vfs
