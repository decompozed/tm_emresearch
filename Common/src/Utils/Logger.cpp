/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */


#include <iostream>
#include <fstream>
#include <iomanip>
#include <chrono>
#include <ctime>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <sstream>
#include <format>
#include <memory>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include "../../include/Utils/Logger.h"

#ifdef _WIN32
#include <Windows.h>
#endif

namespace Utils
{
    namespace
    {
        constexpr std::uintmax_t maxFileBytes = 50ull * 1024 * 1024;
        constexpr int keptFiles = 10;

        std::string& nameStorage()
        {
            static std::string name = "Server";
            return name;
        }

        // <TB_LOG_DIR>/<server>.log, rotated: <server>.log.1 is the newest old one, <server>.log.<keptFiles> the oldest
        class LogFile
        {
            std::ofstream m_file;
            std::filesystem::path m_path;
            std::uintmax_t m_bytes = 0;
            bool m_tried = false;

            void open()
            {
                m_tried = true;
                const char* dir = std::getenv("TB_LOG_DIR");
                if (dir == nullptr || *dir == '\0')
                {
                    return;
                }
                std::error_code ec;
                std::filesystem::create_directories(dir, ec);
                m_path = std::filesystem::path{ dir } / (nameStorage() + ".log");
                m_file.open(m_path, std::ios::app | std::ios::binary);
                m_bytes = std::filesystem::exists(m_path, ec) ? std::filesystem::file_size(m_path, ec) : 0;
            }

            void rotate()
            {
                m_file.close();
                std::error_code ec;
                for (int i = keptFiles - 1; i >= 1; --i)
                {
                    const auto from = std::filesystem::path{ m_path.string() + "." + std::to_string(i) };
                    if (std::filesystem::exists(from, ec))
                    {
                        std::filesystem::rename(from, m_path.string() + "." + std::to_string(i + 1), ec);
                    }
                }
                std::filesystem::rename(m_path, m_path.string() + ".1", ec);
                m_file.open(m_path, std::ios::trunc | std::ios::binary);
                m_bytes = 0;
            }

        public:
            void write(const std::string& line)
            {
                if (!m_tried)
                {
                    open();
                }
                if (!m_file.is_open())
                {
                    return;
                }
                if (m_bytes + line.size() > maxFileBytes)
                {
                    rotate();
                }
                m_file << line;
                m_bytes += line.size();
            }

            void flush()
            {
                if (m_file.is_open())
                {
                    m_file.flush();
                }
            }
        };

        // Without the colour codes (the file is read with less / grep)
        std::string plain(const std::string& line)
        {
            std::string out;
            out.reserve(line.size());
            for (std::size_t i = 0; i < line.size(); ++i)
            {
                if (line[i] == '\033')
                {
                    while (i < line.size() && line[i] != 'm')
                    {
                        ++i;
                    }
                    continue;
                }
                out += line[i];
            }
            return out;
        }

        // Set when the console's static instance is destroyed (exit()): a log line after that (a static's destructor,
        // std::terminate from a joinable std::thread) goes straight to stderr instead of the destroyed queue
        std::atomic<bool> g_consoleGone{ false };

        class AsyncConsole
        {
            std::queue<std::string> m_queue;
            std::mutex m_mutex;      // the queue
            std::mutex m_writeMutex; // the output (the writer thread and flushNow()); always taken before m_mutex
            std::condition_variable m_cv;
            std::atomic<bool> m_stop{ false };
            LogFile m_file;
            std::thread m_thread;

            // Takes the queued lines and writes them
            void drain()
            {
                std::lock_guard<std::mutex> write{ m_writeMutex };
                std::queue<std::string> batch;
                {
                    std::lock_guard<std::mutex> lock{ m_mutex };
                    std::swap(batch, m_queue);
                }
                for (; !batch.empty(); batch.pop())
                {
                    std::cout << batch.front();
                    m_file.write(plain(batch.front()));
                }
                std::cout.flush();
                m_file.flush();
            }

            void run()
            {
                while (true)
                {
                    {
                        std::unique_lock<std::mutex> lock(m_mutex);
                        m_cv.wait(lock, [this] { return m_stop.load() || !m_queue.empty(); });
                        if (m_stop.load() && m_queue.empty())
                            break;
                    }
                    drain();
                }
            }

        public:
            AsyncConsole() : m_thread([this] { run(); }) {}

            ~AsyncConsole()
            {
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    m_stop = true;
                }
                m_cv.notify_all();
                if (m_thread.joinable())
                    m_thread.join();
                g_consoleGone = true;
            }

            void enqueue(std::string line)
            {
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    m_queue.push(std::move(line));
                }
                m_cv.notify_one();
            }

            void flushNow()
            {
                drain();
            }
        };

        AsyncConsole& console()
        {
            static AsyncConsole instance;
            return instance;
        }

        // The same message back to back: written once, then "repeated N times" when another one comes (or every 10000)
        struct Repeats
        {
            std::mutex mutex;
            std::string last;
            std::uint64_t count = 0;
        };

        Repeats& repeats()
        {
            static Repeats instance;
            return instance;
        }

        std::string utcNow()
        {
            const auto now = std::chrono::system_clock::now();
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
            const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
            std::tm tm_time{};
#ifdef _WIN32
            gmtime_s(&tm_time, &seconds);
#else
            gmtime_r(&seconds, &tm_time);
#endif
            char text[32];
            std::strftime(text, sizeof(text), "%Y-%m-%d %H:%M:%S", &tm_time);
            return std::format("{}.{:03}", text, ms);
        }
    }

    void Logger::setProcessName(const std::string& name)
    {
        nameStorage() = name;
    }

    const std::string& Logger::processName()
    {
        return nameStorage();
    }

    void Logger::flushNow()
    {
        if (!g_consoleGone)
        {
            console().flushNow();
        }
    }

    void Logger::enableAnsiEscapeCodes()
    {
#ifdef _WIN32
        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        if (hOut == INVALID_HANDLE_VALUE) return;

        DWORD dwMode = 0;
        if (!GetConsoleMode(hOut, &dwMode)) return;
        SetConsoleMode(hOut, dwMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
#endif
    }

    void Logger::printToConsole(const std::string& message, LogType type)
    {
        std::call_once(initFlag, enableAnsiEscapeCodes);

        if (!m_loggingEnabled)
            return;

        if (!message.empty() && !g_consoleGone)
        {
            auto& state = repeats();
            std::lock_guard<std::mutex> lock{ state.mutex };
            if (message == state.last)
            {
                ++state.count;
                if (state.count % 10000 != 0)
                {
                    return;
                }
                console().enqueue(LogColors::Warning + utcNow() + " [" + nameStorage() + "] [Warning] last message repeated " +
                    std::to_string(state.count) + " times so far" + LogColors::Reset + "\n");
                return;
            }
            if (state.count > 0)
            {
                console().enqueue(LogColors::Warning + utcNow() + " [" + nameStorage() + "] [Warning] last message repeated " +
                    std::to_string(state.count) + " times" + LogColors::Reset + "\n");
            }
            state.last = message;
            state.count = 0;
        }

        const std::string prefix = utcNow() + " [" + nameStorage() + "] ";
        std::string line;
        switch (type)
        {
        case LogType::Info:
            line = LogColors::Info + prefix + "[Info] " + message + LogColors::Reset + "\n";
            break;
        case LogType::Error:
            line = LogColors::Error + prefix + "[Error] " + message + LogColors::Reset + "\n";
            break;
        case LogType::Normal:
            line = LogColors::Normal + (message.empty() ? std::string{} : prefix) + message + LogColors::Reset + "\n";
            break;
        case LogType::Warning:
            line = LogColors::Warning + prefix + "[Warning] " + message + LogColors::Reset + "\n";
            break;
        }

        if (g_consoleGone)
        {
            std::cerr << line << std::flush;
            return;
        }
        console().enqueue(std::move(line));
    }

    void Logger::log(const std::string& message, LogType type, const std::string& functionName)
    {
        std::string logMessage = message;
        if (!functionName.empty())
        {
            logMessage = "[" + functionName + "] " + logMessage;
        }
        printToConsole(logMessage, type);
    }

    std::string Logger::getCurrentDateTime()
    {
        auto now = std::chrono::system_clock::now();
        std::time_t now_c = std::chrono::system_clock::to_time_t(now);

        std::tm tm_time{};
#ifdef _WIN32
        localtime_s(&tm_time, &now_c);   // Windows
#else
        localtime_r(&now_c, &tm_time);   // Linux/Unix
#endif

        std::stringstream ss;
        ss << std::put_time(&tm_time, "%Y-%m-%d %X");
        return ss.str();
    }

    std::string Logger::logTypeToString(LogType type)
    {
        switch (type) {
        case LogType::Info: return "Info";
        case LogType::Error: return "Error";
        case LogType::Normal: return "Normal";
        case LogType::Warning: return "Warning";
        }
        return "Unknown";
    }
}
