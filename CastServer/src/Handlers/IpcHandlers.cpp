/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#include "Handlers/IpcHandlers.h"
#include <algorithm>
#include <string>
#include <vector>
#include <Utils/Constants.h>
#include <Utils/Logger.h>
#include "HaveApi/CastConnect.h"
#include "HaveApi/RoomSync.h"
#include "HaveApi/TickRate.h"
#include "Managers/RoomsManager.h"
#include "Network/IpcClient.h"
#include "Network/SessionsManager.h"
#include "Structures/ClientData/Structures.h"

namespace Cast::Handlers
{
    namespace
    {
        constexpr std::uint8_t playerVisible = 0;
        constexpr std::uint8_t playerInvisible = 1;
        constexpr std::uint8_t roomInvisible = 2;
        constexpr std::uint8_t roomVisible = 3;
    }

    // MainServer commands are never echoed: on the persistent link an echo would be read as a new command.
    void IpcHandlers::registerRoutes(Network::PacketRouter<Common::Network::Session>& router)
    {
        on(router, Common::Constants::M2C_mapId, &IpcHandlers::onMapId);
        on(router, Common::Constants::M2C_playerTeamInfoBatch, &IpcHandlers::onPlayerTeamInfoBatch);
        on(router, Common::Constants::M2C_roomNumber, &IpcHandlers::onRoomNumber);
        on(router, Common::Constants::M2C_roomSync, &IpcHandlers::onRoomSync);
        on(router, Common::Constants::A2M_passIp, &IpcHandlers::onClientIp);
        on(router, Common::Constants::M2C_Invisibility, &IpcHandlers::onInvisibility);
        on(router, Common::Constants::C2M_CloseSocketReq, &IpcHandlers::onCloseSocket);
        on(router, Common::Constants::M2C_tickRate, &IpcHandlers::onTickRate);
        router.on(Common::Constants::C2M_castConnect, [this](const Packet& answer, SessionPtr link) { onConnectAnswer(answer, link); });
    }

    void IpcHandlers::on(Network::PacketRouter<Common::Network::Session>& router, std::uint16_t order, void (IpcHandlers::*handler)(const Packet&))
    {
        router.on(order, [this, handler](const Packet& request, SessionPtr) { (this->*handler)(request); });
    }

    void IpcHandlers::onMapId(const Packet& request)
    {
        if (const auto room = m_ctx.rooms.getRoom(request.getSession()))
        {
            room->setMap(request.getExtra());
            room->setMode(request.getOption());
        }
    }

    void IpcHandlers::onPlayerTeamInfoBatch(const Packet& request)
    {
        using Main::ClientData::PlayerTeamInfo;
        const std::size_t size = request.getDataSize();
        if (size % sizeof(PlayerTeamInfo) != 0)
        {
            ::Utils::Logger::log("Invalid player team info batch size", ::Utils::LogType::Error, "Cast::handlePlayerTeamInfoBatch");
            return;
        }

        std::vector<PlayerTeamInfo> players;
        players.reserve(size / sizeof(PlayerTeamInfo));
        for (std::size_t offset = 0; offset < size; offset += sizeof(PlayerTeamInfo))
        {
            if (const auto player = Common::HaveApi::decodeExact<PlayerTeamInfo>(request.getData() + offset, sizeof(PlayerTeamInfo)))
            {
                players.push_back(*player);
            }
        }
        if (const auto room = m_ctx.rooms.getRoom(request.getSession()))
        {
            room->setPlayerTeams(players);
        }
    }

    void IpcHandlers::onRoomNumber(const Packet& request)
    {
        const auto room = m_ctx.rooms.getRoom(request.getSession());
        if (room && room->getRoomNumber() != request.getExtra())
        {
            ::Utils::Logger::log("[RoomsManager] M2C_roomNumber " + std::to_string(request.getExtra()) + " for session " +
                std::to_string(request.getSession()) + ", attached to room " + std::to_string(room->getRoomNumber()), ::Utils::LogType::Warning);
        }
    }

    void IpcHandlers::onRoomSync(const Packet& request)
    {
        const auto message = Common::HaveApi::decodeRoomSync(request.getData(), request.getDataSize());
        if (!message)
        {
            ::Utils::Logger::log("Invalid room sync (" + std::to_string(request.getDataSize()) + " B)", ::Utils::LogType::Error, "Cast::onRoomSync");
            return;
        }
        m_ctx.rooms.applySync(*message);
    }

    void IpcHandlers::onClientIp(const Packet& request)
    {
        const auto* data = reinterpret_cast<const char*>(request.getData());
        const std::string ip{ data, std::find(data, data + request.getDataSize(), '\0') };

        auto& loggedIps = Common::Network::Session::s_loggedIps;
        auto& ipQueue = Common::Network::Session::ipQueue;
        if (loggedIps.contains(ip))
        {
            return;
        }
        if (loggedIps.size() >= Common::Constants::maxSessionsPerServer * 4)
        {
            loggedIps.erase(ipQueue.front());
            ipQueue.pop_front();
        }
        loggedIps.insert(ip);
        ipQueue.push_back(ip);
    }

    void IpcHandlers::onInvisibility(const Packet& request)
    {
        const auto room = m_ctx.rooms.getRoom(request.getSession());
        if (!room)
        {
            return;
        }
        switch (request.getExtra())
        {
        case playerVisible:
        case playerInvisible:
            if (const auto player = m_ctx.sessions.getSession(request.getSession()))
            {
                player->setInvisible(request.getExtra() == playerInvisible);
            }
            break;
        case roomInvisible:
            room->setInvisible(true);
            break;
        case roomVisible:
            room->setInvisible(false);
            break;
        default:
            break;
        }
    }

    void IpcHandlers::onCloseSocket(const Packet& request)
    {
        if (const auto player = m_ctx.sessions.getSession(request.getSession()))
        {
            player->closeSocket();
        }
    }

    void IpcHandlers::onTickRate(const Packet& request)
    {
        const std::uint32_t rate = request.getOption();
        if (Common::HaveApi::isValidTickRate(rate) && Common::HaveApi::currentTickRate.exchange(rate) != rate)
        {
            ::Utils::Logger::log("[HaveApi] tick rate " + std::to_string(rate), ::Utils::LogType::Info, "Cast::onTickRate");
        }
    }

    void IpcHandlers::onConnectAnswer(const Packet& answer, const SessionPtr& link)
    {
        if (link == m_ctx.ipc.session())
        {
            m_ctx.ipc.onAnswer(answer);
        }
    }
}
