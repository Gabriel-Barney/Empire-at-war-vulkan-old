// alamo_crash_tests: each crash kind, run in a child process of this exe, leaves exactly one minidump ("MDMP" header) and its
// .txt note in a fresh folder, and the child exits non-zero; ALAMO_CRASHDUMP=0 writes nothing. Child mode:
//   alamo_crash_tests --child <dump dir> <kind>
#include "alamo/crash/crash_handler.hpp"
#include "alamo/test/support.hpp"

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace {

#ifdef _WIN32
// Runs this exe in child mode with one extra environment variable; returns its exit code (or -1).
long run_child(const fs::path& dir, const std::string& kind, const char* env_name = nullptr, const char* env_value = nullptr) {
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring cmd = L"\"" + std::wstring(exe) + L"\" --child \"" + dir.wstring() + L"\" " + fs::path(kind).wstring();
    if (env_name) SetEnvironmentVariableA(env_name, env_value);
    STARTUPINFOW si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(L'\0');
    const BOOL ok = CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi);
    if (env_name) SetEnvironmentVariableA(env_name, nullptr);
    if (!ok) return -1;
    WaitForSingleObject(pi.hProcess, 120000);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return static_cast<long>(code);
}

std::vector<fs::path> files_with(const fs::path& dir, const char* ext) {
    std::vector<fs::path> out;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir, ec))
        if (e.path().extension() == ext) out.push_back(e.path());
    return out;
}

fs::path fresh_dir(const std::string& kind) {
    const fs::path d = fs::temp_directory_path() / "alamo_crash_tests" / kind;
    std::error_code ec;
    fs::remove_all(d, ec);
    fs::create_directories(d, ec);
    return d;
}

void check_kind(const std::string& kind, const char* reason) {
    alamo::test::section(kind.c_str());
    const fs::path dir = fresh_dir(kind);
    const long code = run_child(dir, kind);
    CHECK(code != 0 && code != -1);
    const auto dumps = files_with(dir, ".dmp");
    const auto notes = files_with(dir, ".txt");
    CHECK(dumps.size() == 1);
    CHECK(notes.size() == 1);
    if (dumps.size() == 1) {
        const std::string d = alamo::test::read_file(dumps[0]);
        CHECK(d.size() > 1024);
        CHECK(d.compare(0, 4, "MDMP") == 0);
    }
    if (notes.size() == 1) {
        const std::string t = alamo::test::read_file(notes[0]);
        CHECK(t.find(reason) != std::string::npos);
        CHECK(t.find("(FAILED)") == std::string::npos);
        std::printf("%s", t.c_str());
    }
}
#endif

}  // namespace

int main(int argc, char** argv) {
    if (argc == 4 && std::string(argv[1]) == "--child") {
        alamo::crash::Options o;
        o.app_name = "crashtest";
        o.dump_dir = argv[2];
        alamo::crash::install(o);
        alamo::crash::crash_now(argv[3]);
        return 0;  // a kind that did not crash
    }
#ifdef _WIN32
    check_kind("access", "unhandled exception");
    check_kind("thread", "unhandled exception");
    check_kind("stack", "unhandled exception");
    check_kind("throw", "std::terminate");  // the CRT filter turns it into terminate (see crash_handler.cpp)
    check_kind("abort", "abort");
    check_kind("terminate", "std::terminate");
    check_kind("purecall", "pure virtual call");

    alamo::test::section("ALAMO_CRASHDUMP=0");
    const fs::path off = fresh_dir("off");
    CHECK(run_child(off, "access", "ALAMO_CRASHDUMP", "0") != 0);
    CHECK(files_with(off, ".dmp").empty());

    alamo::test::section("unknown kind does not crash");
    CHECK(run_child(fresh_dir("none"), "no-such-kind") == 0);
    CHECK(!alamo::crash::crash_now("no-such-kind"));
    CHECK(!alamo::crash::default_dump_dir().empty());
#else
    CHECK(alamo::crash::install({}).empty());
#endif
    return alamo::test::report("alamo_crash_tests");
}
