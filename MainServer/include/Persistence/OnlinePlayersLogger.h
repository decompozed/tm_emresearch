#ifndef ONLINE_PLAYERS_LOGGER_H
#define ONLINE_PLAYERS_LOGGER_H

#include <asio.hpp>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <thread>

#include "Network/MainSessionManager.h"
#include "Persistence/MainDatabaseManager.h"

namespace Main
{
    namespace Persistence
    {
        // Every two minutes updates this server's row of the OnlinePlayers table with the number of players online
        // (SessionsManager::getTotalSessions, the count the AuthServer's server list shows); no history is kept. Own thread:
        // the sessions are only touched on the io_context's thread, so the count is read there (posted) and the thread waits.
        class OnlinePlayersLogger
        {
        public:
            static constexpr std::chrono::minutes interval{ 2 };

            OnlinePlayersLogger(asio::io_context& ioContext, Main::Network::SessionsManager& sessionsManager,
                PersistentDatabase& database, std::uint16_t serverId);
            ~OnlinePlayersLogger();

            OnlinePlayersLogger(const OnlinePlayersLogger&) = delete;
            OnlinePlayersLogger& operator=(const OnlinePlayersLogger&) = delete;

        private:
            void loop();
            std::optional<std::uint32_t> readOnlinePlayers();

            asio::io_context& m_ioContext;
            Main::Network::SessionsManager& m_sessionsManager;
            PersistentDatabase& m_database;
            std::uint16_t m_serverId;

            std::atomic<bool> m_stopRequested{ false };
            std::mutex m_wakeupMutex;
            std::condition_variable m_wakeupCv;
            std::thread m_thread;
        };
    }
}

#endif
