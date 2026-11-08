/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */


#include <future>
#include <memory>

#include "Persistence/OnlinePlayersLogger.h"

namespace Main
{
    namespace Persistence
    {
        OnlinePlayersLogger::OnlinePlayersLogger(asio::io_context& ioContext, Main::Network::SessionsManager& sessionsManager,
            PersistentDatabase& database, std::uint16_t serverId)
            : m_ioContext{ ioContext }
            , m_sessionsManager{ sessionsManager }
            , m_database{ database }
            , m_serverId{ serverId }
        {
            m_thread = std::thread(&OnlinePlayersLogger::loop, this);
        }

        OnlinePlayersLogger::~OnlinePlayersLogger()
        {
            {
                std::lock_guard<std::mutex> lock(m_wakeupMutex);
                m_stopRequested = true;
            }
            m_wakeupCv.notify_all();
            if (m_thread.joinable())
            {
                m_thread.join();
            }
        }

        void OnlinePlayersLogger::loop()
        {
            while (!m_stopRequested)
            {
                {
                    std::unique_lock<std::mutex> lock(m_wakeupMutex);
                    m_wakeupCv.wait_for(lock, interval, [this] { return m_stopRequested.load(); });
                }

                if (m_stopRequested) break;

                const auto onlinePlayers = readOnlinePlayers();
                if (!onlinePlayers)
                {
                    ::Utils::Logger::log("The network thread did not answer, online players not logged this time",
                        Utils::LogType::Warning, "OnlinePlayersLogger::loop");
                    continue;
                }

                try
                {
                    m_database.withGuard([&]() { m_database.logOnlinePlayers(m_serverId, *onlinePlayers); });
                }
                catch (const std::exception& e)
                {
                    ::Utils::Logger::log("Database failure, online players not logged this time: " + std::string(e.what()),
                        Utils::LogType::Error, "OnlinePlayersLogger::loop");
                }
            }
        }

        std::optional<std::uint32_t> OnlinePlayersLogger::readOnlinePlayers()
        {
            auto count = std::make_shared<std::promise<std::uint32_t>>();
            auto answer = count->get_future();
            asio::post(m_ioContext, [this, count]() { count->set_value(m_sessionsManager.getTotalSessions()); });

            // bounded: a stopped / stuck io_context must not keep the thread (and the shutdown) waiting
            while (answer.wait_for(std::chrono::milliseconds(200)) != std::future_status::ready)
            {
                if (m_stopRequested || m_ioContext.stopped()) return std::nullopt;
            }
            return answer.get();
        }
    }
}
