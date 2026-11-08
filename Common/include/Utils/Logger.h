#ifndef CUSTOM_LOGGER_H
#define CUSTOM_LOGGER_H

#include <iostream>
#include <fstream>
#include <iomanip>
#include <chrono>
#include <ctime>
#include <string>
#include <sstream>
#include <format>
#include <memory>
#include <mutex>

namespace Utils
{
    enum class LogType
    {
        Info, Error, Warning, Normal
    };

    namespace LogColors
    {
        const inline std::string Reset = "\033[0m";
        const inline std::string Info = "\033[32m";
        const inline std::string Error = "\033[31m";
        const inline std::string Normal = "\033[37m";
        const inline std::string Warning = "\033[33m";
    }

    // Every line: UTC date and time to the millisecond, the server's name (the three servers share one docker log), the
    // type and the message. Written by a background thread, flushed as soon as the queue is empty (a crash loses at most
    // the lines still queued; flushNow() drains them first). With the environment variable TB_LOG_DIR set, also written
    // to <TB_LOG_DIR>/<server>.log (rotated at 50 MB, 10 files kept). The same message repeated back to back is written
    // once with a "repeated N times" line (a loop must not fill the disk).
    class Logger
    {
    private:
        static inline bool m_loggingEnabled = true;
        static inline std::once_flag initFlag;

        static void enableAnsiEscapeCodes();

    public:
        static void enableLogging()
        {
            m_loggingEnabled = true;
        }

        static void disableLogging()
        {
            m_loggingEnabled = false;
        }

        // The server's name in every line ("Main", "Cast", "Auth"); call first thing in main()
        static void setProcessName(const std::string& name);
        [[nodiscard]] static const std::string& processName();

        static void log(const std::string& message, LogType type = LogType::Normal, const std::string& functionName = "");

        static void newline()
        {
            printToConsole("", LogType::Normal);
        }

        // Writes every queued line now (crash handler, shutdown)
        static void flushNow();

    private:
        static void printToConsole(const std::string& message, LogType type);
        static std::string getCurrentDateTime();
        static std::string logTypeToString(LogType type);
    };
}

#endif
