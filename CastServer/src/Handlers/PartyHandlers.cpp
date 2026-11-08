/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#include "Handlers/PartyHandlers.h"
#include <format>
#include <string>
#include <Utils/Constants.h>
#include <Utils/Logger.h>
#include "Handlers/PartyLogging.h"
#include "Handlers/RoomCombatWorld.h"
#include "HaveApi/CastConnect.h"
#include "Managers/RoomsManager.h"
#include "Network/SessionsManager.h"

namespace Cast::Handlers
{
    namespace
    {
        template<class T>
        void reply(const std::shared_ptr<Common::Network::Session>& link, const Common::Network::UnecryptedPacket& request, std::uint16_t order, const T& answer)
        {
            Common::Network::UnecryptedPacket response;
            response.setTcpHeader(request.getSession());
            response.setCommand(order, 0, 0, request.getOption());
            response.setData(reinterpret_cast<const std::uint8_t*>(&answer), sizeof(answer));
            link->asyncWrite(response);
        }

        std::string describeStart(const Common::HaveApi::LavaState& state)
        {
            if (!state.active)
            {
                return "no player position, spawn or map bounds, not started";
            }
            return std::string{ state.fromMap ? "(map spawns / bounds) " : "(player positions) " } + "floor " + std::to_string(state.floorZ) + " speed " +
                std::to_string(state.floorSpeed) + ", walls x [" + std::to_string(state.wallX1) + ", " + std::to_string(state.wallX2) + "] y [" +
                std::to_string(state.wallY1) + ", " + std::to_string(state.wallY2) + "]";
        }

        std::string optionalZ(const std::optional<float>& z)
        {
            return z ? std::format("{:.1f}", *z) : std::string{ "none" };
        }

        std::string_view lavaActionName(Common::HaveApi::LavaAction action)
        {
            using Common::HaveApi::LavaAction;
            switch (action)
            {
            case LavaAction::Start: return "start";
            case LavaAction::SetSpeed: return "set speed";
            case LavaAction::Stop: return "stop";
            case LavaAction::Probe: return "probe";
            case LavaAction::ProtectPlayers: return "protect players";
            case LavaAction::UnprotectPlayers: return "unprotect players";
            case LavaAction::DisarmPlayer: return "disarm player";
            case LavaAction::ArmPlayer: return "arm player";
            case LavaAction::KillPlayer: return "kill player";
            }
            return "unknown";
        }

        void logMissingRoom(std::string_view what, const Common::Network::UnecryptedPacket& request, bool roomFound)
        {
            ::Utils::Logger::log("[HaveApi] " + std::string{ what } + " command for session " + std::to_string(request.getSession()) +
                (roomFound ? ": malformed" : ": room not found"), ::Utils::LogType::Warning);
        }

        Common::HaveApi::LavaState done()
        {
            Common::HaveApi::LavaState state{};
            state.active = 1;
            return state;
        }
    }

    void PartyHandlers::registerRoutes(Network::PacketRouter<Common::Network::Session>& router)
    {
        router.on(Common::Constants::M2C_partyKillPlayer, [this](const Packet& request, SessionPtr) { onKillPlayer(request); });
        router.on(Common::Constants::M2C_partyLava, [this](const Packet& request, SessionPtr link) { onLava(request, link); });
        router.on(Common::Constants::M2C_partyZone, [this](const Packet& request, SessionPtr link) { onZone(request, link); });
        router.on(Common::Constants::M2C_partyStorm, [this](const Packet& request, SessionPtr link) { onStorm(request, link); });
        router.on(Common::Constants::M2C_partyMatchInfo, [this](const Packet& request, SessionPtr) { onMatchInfo(request); });
    }

    void PartyHandlers::onMatchInfo(const Packet& request)
    {
        const auto room = m_ctx.rooms.getRoom(request.getSession());
        const auto info = Common::HaveApi::decodeExact<Common::HaveApi::PartyMatchInfo>(request.getData(), request.getDataSize());
        if (!room || !info)
        {
            return;
        }
        auto& log = room->partyLog();
        if (log.running() && info->running && log.round() == info->round && log.modeId() == info->modeId)
        {
            return;
        }
        if (log.setMatch(*info))
        {
            writePartyHits(*room);
        }
        logParty(*room, std::format("cast: party game {} round {} {} (map {}, room mode {})", info->modeId, info->round,
            info->running ? "running" : "over", room->getMap(), room->getMode()));
    }

    void PartyHandlers::onKillPlayer(const Packet& request)
    {
        const auto room = m_ctx.rooms.getRoom(request.getSession());
        if (!room)
        {
            return;
        }
        if (const auto player = m_ctx.sessions.getSession(request.getSession()))
        {
            const bool killed = room->killOnlyForSelf(player);
            logParty(*room, "cast death camera (death packet to the player only) for " + describePlayer(*room, *player) +
                (killed ? ": sent" : ": not sent (cannot die: not in the match, dead or observer)"));
        }
    }

    void PartyHandlers::onLava(const Packet& request, const SessionPtr& link)
    {
        Common::HaveApi::LavaState state{};
        const auto room = m_ctx.rooms.getRoom(request.getSession());
        const auto command = Common::HaveApi::decodeExact<Common::HaveApi::LavaCommand>(request.getData(), request.getDataSize());
        if (room && command)
        {
            state = runLava(*room, *command, static_cast<std::uint32_t>(request.getSession()));
            if (command->action != Common::HaveApi::LavaAction::Probe)
            {
                logParty(*room, std::format("cast lava {} (session {}, walls {}, floor speed {:.2f}, close {:.1f} s, factor {:.2f}) -> {}",
                    lavaActionName(command->action), request.getSession(), command->wallsOn, command->floorSpeed, command->wallCloseSeconds,
                    command->speedFactor, describeLava(state)));
            }
        }
        else
        {
            logMissingRoom("lava", request, room != nullptr);
        }
        reply(link, request, Common::Constants::M2C_partyLava, state);
    }

    Common::HaveApi::LavaState PartyHandlers::runLava(Domain::Room& room, const Common::HaveApi::LavaCommand& command, std::uint32_t sessionId)
    {
        using Common::HaveApi::LavaAction;
        switch (command.action)
        {
        case LavaAction::Start:
            return startLava(room, command);
        case LavaAction::SetSpeed:
            return room.lava().setSpeed(command);
        case LavaAction::Stop:
            return room.lava().stop();
        case LavaAction::ProtectPlayers:
        case LavaAction::UnprotectPlayers:
            return protectPlayers(room, command.action == LavaAction::ProtectPlayers);
        case LavaAction::DisarmPlayer:
        case LavaAction::ArmPlayer:
            room.lava().setDisarmed(sessionId, command.action == LavaAction::DisarmPlayer);
            logParty(room, std::string{ command.action == LavaAction::DisarmPlayer ? "cast disarmed " : "cast armed " } + describeSession(room, sessionId));
            return done();
        case LavaAction::KillPlayer:
            return killPlayer(room, sessionId);
        case LavaAction::Probe:
            return probe(room, sessionId);
        }
        return {};
    }

    Common::HaveApi::LavaState PartyHandlers::startLava(Domain::Room& room, const Common::HaveApi::LavaCommand& command)
    {
        const auto state = room.lava().start(command, room.getMap());
        ::Utils::Logger::log("[HaveApi] lava start in map " + std::to_string(room.getMap()) + ": " + describeStart(state));
        const auto& report = room.lava().lastStart();
        logParty(room, std::format("cast lava start in map {}: {}; positions fresh {} known {}, lowest fresh z {}, lowest known z {}, "
            "lowest spawn z {}, map min z {}", room.getMap(), describeStart(state), report.freshPositions, report.knownPositions,
            optionalZ(report.lowestFreshZ), optionalZ(report.lowestKnownZ), optionalZ(report.lowestSpawnZ), optionalZ(report.mapMinZ)));
        const auto now = Domain::PartyLava::Clock::now();
        for (const auto& [session, p] : room.lava().positions())
        {
            logParty(room, std::format("cast lava start: position of {} ({:.1f}, {:.1f}, {:.1f}), {} ms old", describeSession(room, session),
                p.x, p.y, p.z, std::chrono::duration_cast<std::chrono::milliseconds>(now - p.at).count()));
        }
        return state;
    }

    Common::HaveApi::LavaState PartyHandlers::protectPlayers(Domain::Room& room, bool protect)
    {
        room.lava().setProtectPlayers(protect);
        if (!protect)
        {
            room.lava().armEveryone();
        }
        ::Utils::Logger::log(std::string{ "[HaveApi] players " } + (room.lava().protectsPlayers() ? "protected" : "not protected") +
            " from each other in room " + std::to_string(room.getRoomNumber()));
        logParty(room, std::string{ "cast players " } + (room.lava().protectsPlayers() ? "protected" : "not protected (every player armed again)") +
            " from each other");
        return done();
    }

    Common::HaveApi::LavaState PartyHandlers::killPlayer(Domain::Room& room, std::uint32_t sessionId)
    {
        const auto victim = m_ctx.sessions.getSession(sessionId);
        const bool killed = victim && room.killForAll(sessionId);
        if (killed)
        {
            reportServerDeath(m_ctx, victim->getAccountId());
        }
        logParty(room, "cast kill by the party rules of " + describeSession(room, sessionId) +
            (killed ? ": killed for everybody" : ": not killed (not in the room or the match, dead or observer)"));
        return done();
    }

    Common::HaveApi::LavaState PartyHandlers::probe(Domain::Room& room, std::uint32_t sessionId)
    {
        Common::HaveApi::LavaState state{};
        if (const auto position = room.lava().position(sessionId))
        {
            state.active = 1;
            state.wallX1 = position->x;
            state.wallY1 = position->y;
            state.floorZ = position->z;
        }
        return state;
    }

    void PartyHandlers::onZone(const Packet& request, const SessionPtr& link)
    {
        Common::HaveApi::ZoneAnswer answer{};
        const auto room = m_ctx.rooms.getRoom(request.getSession());
        const auto command = Common::HaveApi::decodeExact<Common::HaveApi::ZoneCommand>(request.getData(), request.getDataSize());
        if (room && command)
        {
            answer = runZone(*room, *command);
            if (command->action != Common::HaveApi::ZoneAction::Query)
            {
                logParty(*room, std::format("cast zone {} action {} ({} at ({:.1f}, {:.1f}) size {:.1f}, keep out {:.1f}, z [{:.1f}, {:.1f}], {} members) "
                    "-> done {}, occupants {}", command->index, static_cast<int>(command->action),
                    command->shape == Common::HaveApi::ZoneShape::Square ? "square" : "circle", command->x, command->y, command->radius,
                    command->keepOut, command->zMin, command->zMax, command->count, answer.done, describeSessions(*room, listedSessions(answer))));
            }
        }
        else
        {
            logMissingRoom("zone", request, room != nullptr);
        }
        reply(link, request, Common::Constants::M2C_partyZone, answer);
    }

    Common::HaveApi::ZoneAnswer PartyHandlers::runZone(Domain::Room& room, const Common::HaveApi::ZoneCommand& command)
    {
        using Common::HaveApi::ZoneAction;
        auto& zone = room.zone(command.index);
        Common::HaveApi::ZoneAnswer answer{};
        answer.index = command.index;
        switch (command.action)
        {
        case ZoneAction::Set:
            zone.set(command);
            answer = zone.resetOccupants(room.lava());
            ::Utils::Logger::log("[HaveApi] zone " + std::to_string(command.index) + " at (" + std::to_string(command.x) + ", " +
                std::to_string(command.y) + ") " + (command.shape == Common::HaveApi::ZoneShape::Square ? "half side " : "radius ") +
                std::to_string(command.radius));
            break;
        case ZoneAction::Clear:
            zone.clear();
            answer.done = 1;
            break;
        case ZoneAction::Query:
            answer = zone.occupants(room.lava());
            break;
        case ZoneAction::AddMembers:
            zone.addMembers(command);
            answer.done = 1;
            break;
        }
        return answer;
    }

    void PartyHandlers::onStorm(const Packet& request, const SessionPtr& link)
    {
        Common::HaveApi::StormAnswer answer{};
        const auto room = m_ctx.rooms.getRoom(request.getSession());
        const auto command = Common::HaveApi::decodeExact<Common::HaveApi::StormCommand>(request.getData(), request.getDataSize());
        if (room && command)
        {
            answer = runStorm(*room, *command);
            if (command->action != Common::HaveApi::StormAction::Query)
            {
                logParty(*room, std::format("cast storm action {} ({} boxes, {} players) -> done {}, outside {}", static_cast<int>(command->action),
                    command->boxCount, command->memberCount, answer.done, describeSessions(*room, listedSessions(answer))));
            }
        }
        else
        {
            logMissingRoom("storm", request, room != nullptr);
        }
        reply(link, request, Common::Constants::M2C_partyStorm, answer);
    }

    Common::HaveApi::StormAnswer PartyHandlers::runStorm(Domain::Room& room, const Common::HaveApi::StormCommand& command)
    {
        using Common::HaveApi::StormAction;
        auto& storm = room.storm();
        switch (command.action)
        {
        case StormAction::Set:
            storm.set(command);
            ::Utils::Logger::log("[HaveApi] storm in room " + std::to_string(room.getRoomNumber()) + ": " +
                std::to_string(command.boxCount) + " boxes, " + std::to_string(command.memberCount) + " players");
            return storm.evaluate(room.lava());
        case StormAction::Clear:
        {
            storm.clear();
            Common::HaveApi::StormAnswer answer{};
            answer.done = 1;
            return answer;
        }
        case StormAction::Query:
            for (const std::uint32_t session : storm.takeStale(room.lava()))
            {
                const auto player = m_ctx.sessions.getSession(session);
                ::Utils::Logger::log("[AntiCheat] storm: no position for " + std::to_string(Domain::PartyStorm::staleAfter.count()) +
                    " s from " + (player ? player->nickname() : std::to_string(session)) + " in room " + std::to_string(room.getRoomNumber()),
                    ::Utils::LogType::Warning);
                logParty(room, "cast storm: no position for " + std::to_string(Domain::PartyStorm::staleAfter.count()) + " s from " +
                    describeSession(room, session) + ", counted outside");
            }
            return storm.evaluate(room.lava());
        }
        return {};
    }
}
