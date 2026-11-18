// [port] Crash minidumps (crash_handler.hpp). The dump thread is created at install time and touches no heap or CRT lock of
// its own after a crash (fixed buffers, Win32 file writes), because the crashing thread may hold them; only DbgHelp's
// MiniDumpWriteDump allocates, which is the documented in-process trade-off.
#include "alamo/crash/crash_handler.hpp"

#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>

#ifdef _WIN32
#include <windows.h>
// DbgHelp after windows.h (it uses its types).
#include <dbghelp.h>

#include <csignal>
#include <cstdio>
#include <cwchar>
#include <exception>
#endif

namespace alamo::crash {

namespace {

std::string env_value(const char* name) {
#ifdef _MSC_VER
    char* v = nullptr;
    size_t len = 0;
    std::string out;
    if (_dupenv_s(&v, &len, name) == 0 && v) out = v;
    std::free(v);
    return out;
#else
    const char* v = std::getenv(name);  // NOLINT(concurrency-mt-unsafe): read once at start-up
    return v ? std::string(v) : std::string();
#endif
}

#ifdef _WIN32
// limit: guard crash dump directory path buffer (wchar_t)
constexpr size_t kPathMax = 1024;
wchar_t g_dir[kPathMax] = {};
wchar_t g_app[64] = {};
std::string g_dir_utf8;
HANDLE g_request = nullptr;
HANDLE g_done = nullptr;
volatile LONG g_installed = 0;
volatile LONG g_crashing = 0;
EXCEPTION_POINTERS* g_ep = nullptr;
DWORD g_thread = 0;
const char* g_reason = "";
LPTOP_LEVEL_EXCEPTION_FILTER g_prev_filter = nullptr;
constexpr DWORD kCxxException = 0xE06D7363u;

void write_stderr(const char* s, size_t n) {
    DWORD written = 0;
    const HANDLE h = GetStdHandle(STD_ERROR_HANDLE);
    if (h && h != INVALID_HANDLE_VALUE) WriteFile(h, s, static_cast<DWORD>(n), &written, nullptr);
}

const char* code_name(DWORD code) {
    switch (code) {
        case EXCEPTION_ACCESS_VIOLATION: return "access violation";
        case EXCEPTION_STACK_OVERFLOW: return "stack overflow";
        case EXCEPTION_ILLEGAL_INSTRUCTION: return "illegal instruction";
        case EXCEPTION_INT_DIVIDE_BY_ZERO: return "integer divide by zero";
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "array bounds exceeded";
        case EXCEPTION_IN_PAGE_ERROR: return "in-page error";
        case EXCEPTION_PRIV_INSTRUCTION: return "privileged instruction";
        case EXCEPTION_BREAKPOINT: return "breakpoint";
        case kCxxException: return "uncaught C++ exception";
        case 0xC0000409u: return "stack buffer overrun / fail fast";
        default: return "exception";
    }
}

// Runs on the dump thread: no heap use of its own (see the file comment).
void write_dump() {
    SYSTEMTIME t;
    GetLocalTime(&t);
    const DWORD pid = GetCurrentProcessId();
    wchar_t base[kPathMax];
    std::swprintf(base, kPathMax, L"%ls\\%ls_%04u%02u%02u-%02u%02u%02u_%lu", g_dir, g_app, t.wYear, t.wMonth, t.wDay, t.wHour,
                  t.wMinute, t.wSecond, static_cast<unsigned long>(pid));
    wchar_t dmp[kPathMax], txt[kPathMax];
    std::swprintf(dmp, kPathMax, L"%ls.dmp", base);
    std::swprintf(txt, kPathMax, L"%ls.txt", base);

    bool dumped = false;
    const HANDLE f = CreateFileW(dmp, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION mei{};
        mei.ThreadId = g_thread;
        mei.ExceptionPointers = g_ep;
        mei.ClientPointers = FALSE;
        const auto type = static_cast<MINIDUMP_TYPE>(MiniDumpWithThreadInfo | MiniDumpWithIndirectlyReferencedMemory |
                                                     MiniDumpWithUnloadedModules);
        dumped = MiniDumpWriteDump(GetCurrentProcess(), pid, f, type, g_ep ? &mei : nullptr, nullptr, nullptr) != FALSE;
        CloseHandle(f);
    }

    char text[2048];
    int n = std::snprintf(text, sizeof text, "reason: %s\nthread: %lu\n", g_reason, static_cast<unsigned long>(g_thread));
    if (g_ep && g_ep->ExceptionRecord && n > 0 && static_cast<size_t>(n) < sizeof text) {
        const EXCEPTION_RECORD& r = *g_ep->ExceptionRecord;
        const void* addr = r.ExceptionAddress;
        wchar_t mod_path[MAX_PATH] = L"?";
        HMODULE mod = nullptr;
        unsigned long long offset = 0;
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               static_cast<LPCWSTR>(addr), &mod) && mod) {
            GetModuleFileNameW(mod, mod_path, MAX_PATH);
            offset = reinterpret_cast<unsigned long long>(addr) - reinterpret_cast<unsigned long long>(mod);
        }
        const wchar_t* mod_name = std::wcsrchr(mod_path, L'\\');
        mod_name = mod_name ? mod_name + 1 : mod_path;
        n += std::snprintf(text + n, sizeof text - static_cast<size_t>(n),
                           "exception: 0x%08lX (%s)\naddress: %p (%ls+0x%llX)\n", static_cast<unsigned long>(r.ExceptionCode),
                           code_name(r.ExceptionCode), addr, mod_name, offset);
        if (r.ExceptionCode == EXCEPTION_ACCESS_VIOLATION && r.NumberParameters >= 2 && n > 0 &&
            static_cast<size_t>(n) < sizeof text)
            n += std::snprintf(text + n, sizeof text - static_cast<size_t>(n), "access: %s 0x%llX\n",
                               r.ExceptionInformation[0] == 0 ? "read" : (r.ExceptionInformation[0] == 1 ? "write" : "execute"),
                               static_cast<unsigned long long>(r.ExceptionInformation[1]));
    }
    if (n > 0 && static_cast<size_t>(n) < sizeof text)
        n += std::snprintf(text + n, sizeof text - static_cast<size_t>(n), "dump: %ls%s\n", dmp, dumped ? "" : " (FAILED)");
    const size_t len = n > 0 ? (static_cast<size_t>(n) < sizeof text ? static_cast<size_t>(n) : sizeof text - 1) : 0;
    const HANDLE tf = CreateFileW(txt, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (tf != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(tf, text, static_cast<DWORD>(len), &written, nullptr);
        CloseHandle(tf);
    }
    char line[kPathMax + 128];
    const int m = std::snprintf(line, sizeof line, "crash: %s; %s %ls\n", g_reason, dumped ? "dump written to" : "dump FAILED,",
                                dumped ? dmp : txt);
    if (m > 0) write_stderr(line, static_cast<size_t>(m) < sizeof line ? static_cast<size_t>(m) : sizeof line - 1);
}

DWORD WINAPI dump_thread(LPVOID) {
    WaitForSingleObject(g_request, INFINITE);
    write_dump();
    SetEvent(g_done);
    return 0;
}

// The first crashing thread gets the dump; a second one (or a crash inside the dump) just waits for it.
void request_dump(EXCEPTION_POINTERS* ep, const char* reason) {
    if (InterlockedExchange(&g_crashing, 1) != 0) {
        WaitForSingleObject(g_done, 60000);
        return;
    }
    g_ep = ep;
    g_thread = GetCurrentThreadId();
    g_reason = reason;
    SetEvent(g_request);
    WaitForSingleObject(g_done, 120000);
}

LONG WINAPI unhandled_filter(EXCEPTION_POINTERS* ep) {
    // An uncaught C++ exception goes to the CRT's filter, which calls std::terminate (where alamo_headless prints the
    // exception's message); the terminate / abort handler then writes the dump, with the throwing frames still on the stack.
    if (g_prev_filter && ep && ep->ExceptionRecord && ep->ExceptionRecord->ExceptionCode == kCxxException) return g_prev_filter(ep);
    request_dump(ep, "unhandled exception");
    return EXCEPTION_EXECUTE_HANDLER;  // the process ends with the exception code
}

[[noreturn]] void end_process() {
    TerminateProcess(GetCurrentProcess(), 3);
    for (;;) Sleep(INFINITE);
}

void on_terminate() {
    request_dump(nullptr, "std::terminate");
    end_process();
}

void on_abort(int) {
    request_dump(nullptr, "abort");
    end_process();
}

void on_invalid_parameter(const wchar_t*, const wchar_t*, const wchar_t*, unsigned int, uintptr_t) {
    request_dump(nullptr, "CRT invalid parameter");
    end_process();
}

void on_purecall() {
    request_dump(nullptr, "pure virtual call");
    end_process();
}

// ---- crash_now helpers -------------------------------------------------------------------------------------------------
volatile bool g_keep_recursing = true;

int recurse(int depth) {
    volatile char pad[4096];
    pad[0] = static_cast<char>(depth);
    if (g_keep_recursing) return recurse(depth + 1) + pad[0];
    return pad[0];
}

void null_write() {
    int* volatile p = nullptr;
    *p = 1;
}

struct PureBase {
    PureBase() { call(); }
    virtual ~PureBase() = default;
    void call() { f(); }
    virtual void f() = 0;
};
struct PureDerived final : PureBase {
    void f() override {}
};

[[noreturn]] void throw_uncaught() { throw std::runtime_error("alamo crash test: uncaught exception"); }
#endif

}  // namespace

std::string default_dump_dir() {
    const std::string la = env_value("LOCALAPPDATA");
    if (la.empty()) return {};
    return (std::filesystem::path(la) / "alamo" / "crashes").string();
}

std::string install(const Options& options) {
#ifdef _WIN32
    if (InterlockedExchange(&g_installed, 1) != 0) return g_dir_utf8;
    if (env_value("ALAMO_CRASHDUMP") == "0") return {};
    std::string dir = env_value("ALAMO_CRASHDUMP_DIR");
    if (dir.empty()) dir = options.dump_dir.empty() ? default_dump_dir() : options.dump_dir;
    if (dir.empty()) return {};
    std::error_code ec;
    const std::filesystem::path p = std::filesystem::absolute(std::filesystem::path(dir), ec);
    if (ec) return {};
    std::filesystem::create_directories(p, ec);
    if (!std::filesystem::is_directory(p, ec)) return {};
    const std::wstring wdir = p.wstring();
    const std::wstring wapp = std::filesystem::path(options.app_name.empty() ? std::string("alamo") : options.app_name).wstring();
    if (wdir.size() + 64 >= kPathMax || wapp.size() >= std::size(g_app)) return {};
    std::wcsncpy(g_dir, wdir.c_str(), kPathMax - 1);
    std::wcsncpy(g_app, wapp.c_str(), std::size(g_app) - 1);

    g_request = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!g_request || !g_done) return {};
    const HANDLE th = CreateThread(nullptr, 256 * 1024, dump_thread, nullptr, 0, nullptr);
    if (!th) return {};
    CloseHandle(th);

    // Room for the filter to run on a thread whose stack just overflowed (this thread: the main thread).
    ULONG guarantee = 64 * 1024;
    SetThreadStackGuarantee(&guarantee);
    g_prev_filter = SetUnhandledExceptionFilter(unhandled_filter);
    std::set_terminate(on_terminate);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);  // no message box / Windows Error Reporting on abort()
    std::signal(SIGABRT, on_abort);
    _set_invalid_parameter_handler(on_invalid_parameter);
    _set_purecall_handler(on_purecall);
    g_dir_utf8 = p.string();
    return g_dir_utf8;
#else
    (void)options;
    return {};
#endif
}

bool crash_now(std::string_view kind) {
#ifdef _WIN32
    if (kind == "access") null_write();
    else if (kind == "thread") std::thread([] { null_write(); }).join();
    else if (kind == "abort") std::abort();
    else if (kind == "terminate") std::terminate();
    else if (kind == "throw") throw_uncaught();
    else if (kind == "stack") return recurse(0) != 0;
    else if (kind == "purecall") {
        PureDerived d;
        d.f();
    }
    else return false;
    return true;
#else
    (void)kind;
    return false;
#endif
}

void crash_if_requested() {
    const std::string kind = env_value("ALAMO_CRASH_TEST");
    if (!kind.empty()) crash_now(kind);
}

}  // namespace alamo::crash
