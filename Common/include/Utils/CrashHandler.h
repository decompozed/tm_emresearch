#ifndef COMMON_UTILS_CRASH_HANDLER_H
#define COMMON_UTILS_CRASH_HANDLER_H

namespace Utils
{
    // Crash diagnostics:
    //  - an exception nobody catches: its type and what(), the stack, the queued log lines; then abort();
    //  - SIGSEGV / SIGBUS / SIGILL / SIGFPE / SIGABRT: the signal and the stack (async-signal-safe writes);
    //  - both to stderr and to <TB_LOG_DIR>/crash-<server>-<pid>.txt; then the default action of the signal runs, so
    //    the kernel still writes the core dump (ulimit -c / core_pattern of the host).
    // Linux only (backtrace(3)); on Windows only the exception text is logged.
    class CrashHandler
    {
    public:
        static void install();
    };
}

#endif
