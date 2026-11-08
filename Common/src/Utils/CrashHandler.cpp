/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#include "../../include/Utils/CrashHandler.h"
#include "../../include/Utils/Logger.h"

#include <cstdlib>
#include <cstring>
#include <exception>
#include <string>
#include <typeinfo>

#ifndef _WIN32
#include <cxxabi.h>
#include <execinfo.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#endif

namespace Utils
{
    namespace
    {
#ifndef _WIN32
        constexpr int maxFrames = 128;
        char g_crashPath[512] = {}; // built at install: a signal handler must not allocate
        char g_altStack[64 * 1024];

        void writeAll(int fd, const char* text)
        {
            std::size_t left = std::strlen(text);
            while (left > 0)
            {
                const ssize_t written = ::write(fd, text, left);
                if (written <= 0)
                {
                    return;
                }
                text += written;
                left -= static_cast<std::size_t>(written);
            }
        }

        void writeBoth(int fileFd, const char* text)
        {
            writeAll(STDERR_FILENO, text);
            if (fileFd >= 0)
            {
                writeAll(fileFd, text);
            }
        }

        void writeStack(int fileFd)
        {
            void* frames[maxFrames];
            const int count = ::backtrace(frames, maxFrames);
            ::backtrace_symbols_fd(frames, count, STDERR_FILENO);
            if (fileFd >= 0)
            {
                ::backtrace_symbols_fd(frames, count, fileFd);
            }
        }

        int openCrashFile()
        {
            return g_crashPath[0] != '\0' ? ::open(g_crashPath, O_WRONLY | O_CREAT | O_APPEND, 0644) : -1;
        }

        void onSignal(int signal)
        {
            const int fd = openCrashFile();
            writeBoth(fd, "\n===== CRASH: signal ");
            writeBoth(fd, ::strsignal(signal));
            writeBoth(fd, " in ");
            writeBoth(fd, Logger::processName().c_str());
            writeBoth(fd, " =====\n");
            writeStack(fd);
            writeBoth(fd, "===== end of stack =====\n");
            if (fd >= 0)
            {
                ::close(fd);
            }
            // SA_RESETHAND restored the default action: the kernel writes the core dump
            ::raise(signal);
        }
#endif

        void onTerminate()
        {
            std::string what = "no active exception";
            if (const std::exception_ptr current = std::current_exception())
            {
                try
                {
                    std::rethrow_exception(current);
                }
                catch (const std::exception& e)
                {
                    what = std::string{ typeid(e).name() } + ": " + e.what();
                }
                catch (...)
                {
                    what = "exception of an unknown type";
                }
            }
            Logger::log("[CrashHandler] std::terminate: " + what, LogType::Error);
            Logger::flushNow();
#ifndef _WIN32
            const int fd = openCrashFile();
            writeBoth(fd, "\n===== CRASH: std::terminate in ");
            writeBoth(fd, Logger::processName().c_str());
            writeBoth(fd, ": ");
            writeBoth(fd, what.c_str());
            writeBoth(fd, " =====\n");
            writeStack(fd); // the throw site is in the stack: the exception unwound nothing before terminate
            writeBoth(fd, "===== end of stack =====\n");
            if (fd >= 0)
            {
                ::close(fd);
            }
#endif
            std::abort(); // SIGABRT: its handler is reset below, the core dump follows
        }
    }

    void CrashHandler::install()
    {
        std::set_terminate(onTerminate);
#ifndef _WIN32
        if (const char* dir = std::getenv("TB_LOG_DIR"); dir != nullptr && *dir != '\0')
        {
            const std::string path = std::string{ dir } + "/crash-" + Logger::processName() + "-" + std::to_string(::getpid()) + ".txt";
            std::strncpy(g_crashPath, path.c_str(), sizeof(g_crashPath) - 1);
        }
        // backtrace() loads libgcc on its first call: not inside a signal handler
        void* warmUp[1];
        ::backtrace(warmUp, 1);

        stack_t altStack{};
        altStack.ss_sp = g_altStack;
        altStack.ss_size = sizeof(g_altStack);
        ::sigaltstack(&altStack, nullptr); // a stack overflow still gets its report

        struct sigaction action{};
        action.sa_handler = onSignal;
        sigemptyset(&action.sa_mask);
        action.sa_flags = SA_RESETHAND | SA_ONSTACK;
        for (const int signal : { SIGSEGV, SIGBUS, SIGILL, SIGFPE })
        {
            ::sigaction(signal, &action, nullptr);
        }
        // SIGABRT: std::abort from onTerminate (already reported) or from glibc (heap corruption: "double free", ...)
        ::sigaction(SIGABRT, &action, nullptr);
#endif
    }
}
