/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#include "Handlers/PartyLogging.h"
#include <format>
#include "Enums/RoomEnums.h"

namespace Cast::Handlers
{
    std::string describePlayer(Domain::Room& room, const Network::Session& player)
    {
        const auto session = static_cast<std::uint32_t>(player.getId());
        std::string text = std::format("{} (session {}, account {}, team {}, {}, {}", player.nickname(), session, player.getAccountId(),
            static_cast<int>(player.getTeam()), player.isDead() ? "dead" : "alive", player.isInMatch() ? "in match" : "not in match");
        if (player.getTeam() == Common::Enums::TEAM_OBSERVER)
        {
            text += ", observer";
        }
        if (room.isHost(session))
        {
            text += ", host";
        }
        if (room.lava().disarmed(session))
        {
            text += ", disarmed";
        }
        return text + ")";
    }

    std::string describeSession(Domain::Room& room, std::uint32_t session)
    {
        const auto player = room.findPlayer(session);
        return player ? describePlayer(room, *player) : std::format("session {} (not in the room)", session);
    }

    std::string describeSessions(Domain::Room& room, std::span<const std::uint32_t> sessions)
    {
        std::string text;
        for (const std::uint32_t session : sessions)
        {
            const auto player = room.findPlayer(session);
            text += std::format("{}{} (session {})", text.empty() ? "" : ", ", player ? player->nickname() : std::string{ "?" }, session);
        }
        return text.empty() ? std::string{ "nobody" } : text;
    }

    std::string describeLava(const Common::HaveApi::LavaState& state)
    {
        return std::format("active {}, {}, floor {:.1f} speed {:.2f}, walls {} x [{:.1f}, {:.1f}] y [{:.1f}, {:.1f}] speeds {:.2f} {:.2f} {:.2f} {:.2f}",
            state.active, state.fromMap ? "from the map" : "from the players", state.floorZ, state.floorSpeed, state.wallsOn ? "on" : "off",
            state.wallX1, state.wallX2, state.wallY1, state.wallY2, state.wallX1Speed, state.wallX2Speed, state.wallY1Speed, state.wallY2Speed);
    }

    void logParty(Domain::Room& room, std::string_view event)
    {
        if (room.getMode() == Common::Enums::SquareMode)
        {
            room.partyLog().write(room.getRoomNumber(), event);
        }
    }

    void writePartyHits(Domain::Room& room)
    {
        for (const auto& entry : room.partyLog().hits().take())
        {
            const auto attacker = room.findPlayer(entry.attacker);
            const auto victim = room.findPlayer(entry.victim);
            logParty(room, std::format("cast hits {} (session {}) -> {} (session {}): {} (weapon {}, explosive {}, other {}), spared {}",
                attacker ? attacker->nickname() : std::string{ "?" }, entry.attacker, victim ? victim->nickname() : std::string{ "?" }, entry.victim,
                entry.total(), entry.weapon, entry.explosive, entry.other, entry.spared));
        }
    }
}
