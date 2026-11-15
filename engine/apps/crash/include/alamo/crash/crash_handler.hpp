#pragma once
// [port] Crash minidumps for the port's executables (alamo_game, alamo_headless). Not game behaviour: a crash in a playtest
// becomes a .dmp an agent can open in WinDbg (toolchain\re) instead of a description. docs/BUILD.md, "Crash dumps".
//
// install() sets the process's unhandled-exception filter plus the CRT's terminate / abort / invalid-parameter / pure-call
// handlers. On a crash the failing thread hands its exception record to a dump thread created at install time (Microsoft's
// advice for MiniDumpWriteDump when no separate process is available) and waits; the dump thread writes
//   <dir>\<app>_<YYYYMMDD-HHMMSS>_<pid>.dmp   (MiniDumpWithThreadInfo | IndirectlyReferencedMemory | UnloadedModules)
//   <dir>\<app>_<YYYYMMDD-HHMMSS>_<pid>.txt   (reason, exception code, faulting module + offset, thread id)
// An uncaught C++ exception is passed on to the CRT's filter (std::terminate, so a terminate handler set after install() still
// runs) and dumped from the terminate / abort handler.
// The dump thread prints one "crash: ..." line to stderr; the process then exits with the exception code (3 for abort / terminate).
// Reads no sim state and changes nothing the simulation computes. Windows only; elsewhere install() returns "" and does nothing.
#include <string>
#include <string_view>

namespace alamo::crash {

struct Options {
    std::string app_name = "alamo";  // file name prefix
    std::string dump_dir;            // empty: default_dump_dir()
};

// Installs the handlers once per process (later calls return the first folder). Returns the dump folder, or "" when dumps are
// off: ALAMO_CRASHDUMP=0, no folder could be made, or not Windows. ALAMO_CRASHDUMP_DIR overrides the folder.
std::string install(const Options& options);

// %LOCALAPPDATA%\alamo\crashes ("" when LOCALAPPDATA is unset).
std::string default_dump_dir();

// Test hook: crash now in the named way. Kinds: access (null write), thread (null write on another thread), abort, terminate,
// throw (an uncaught exception), stack (stack overflow), purecall. Unknown kinds return false.
bool crash_now(std::string_view kind);

// Calls crash_now(ALAMO_CRASH_TEST) when that variable is set (for checking a build's dumps by hand).
void crash_if_requested();

}  // namespace alamo::crash
