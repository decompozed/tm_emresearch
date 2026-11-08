/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#ifndef MAIN_HAVE_API_COMBAT_EVENTS_H
#define MAIN_HAVE_API_COMBAT_EVENTS_H

#include <memory>
#include "HaveApi/CombatEvent.h"
#include "HaveApi/PartyMatch.h"
#include "HaveApi/RoomState.h"
#include "Managers/RoomsManager.h"
#include "Network/MainSessionManager.h"
#include "Network/Packet.h"
#include "HaveApi/Storm.h"
#include "HaveApi/Zone.h"
#include "Rooms/Room.h"
#include "Detail/Utilities.h"

namespace Main::HaveApi
{
	// C2M_partyZoneOccupants (Hardpoint): the players in the zone changed. The TCP header names one of the room's players.
	inline void onZoneOccupants(const Common::Network::UnecryptedPacket& request, Main::Network::SessionsManager& sessionsManager,
		Main::Classes::RoomsManager& roomsManager)
	{
		if (request.getDataSize() != sizeof(Common::HaveApi::ZoneAnswer))
		{
			return;
		}
		const auto player = sessionsManager.getSessionBySessionId(request.getSession());
		if (!player)
		{
			return;
		}
		Main::Classes::Room* room = roomsManager.getRoomByNumber(player->getPlayer().matchContext.roomNumber);
		if (PartyMatch* match = room != nullptr ? room->haveApi().match() : nullptr)
		{
			match->onZoneOccupants(Main::Details::parseData<Common::HaveApi::ZoneAnswer>(request));
		}
	}

	inline void onStormOutside(const Common::Network::UnecryptedPacket& request, Main::Network::SessionsManager& sessionsManager,
		Main::Classes::RoomsManager& roomsManager)
	{
		if (request.getDataSize() != sizeof(Common::HaveApi::StormAnswer))
		{
			return;
		}
		const auto player = sessionsManager.getSessionBySessionId(request.getSession());
		if (!player)
		{
			return;
		}
		Main::Classes::Room* room = roomsManager.getRoomByNumber(player->getPlayer().matchContext.roomNumber);
		if (PartyMatch* match = room != nullptr ? room->haveApi().match() : nullptr)
		{
			match->onStormOutside(Main::Details::parseData<Common::HaveApi::StormAnswer>(request));
		}
	}

	// C2M_partyCombatEvent: a death, a respawn or a hit in a SquareMode room, reported by CastServer.
	inline void onCombatEvent(const Common::Network::UnecryptedPacket& request, Main::Network::SessionsManager& sessionsManager,
		Main::Classes::RoomsManager& roomsManager)
	{
		if (request.getDataSize() != sizeof(Common::HaveApi::CombatEvent))
		{
			return;
		}
		const auto event = Main::Details::parseData<Common::HaveApi::CombatEvent>(request);
		const auto victim = sessionsManager.getSessionByAccountId(event.victimAccountId);
		if (!victim)
		{
			return;
		}
		Main::Classes::Room* room = roomsManager.getRoomByNumber(victim->getPlayer().matchContext.roomNumber);
		PartyMatch* match = room != nullptr ? room->haveApi().match() : nullptr;
		if (match == nullptr)
		{
			// a normal Scrimmage match: its kills and deaths for the end-of-match scoreboard (handleScrimmageEnding)
			if (room != nullptr && room->getRoomSettings().mode == Common::Enums::Scrimmage
				&& event.kind == Common::HaveApi::CombatEventKind::Death)
			{
				room->haveApi().recordDeath(event.victimAccountId, event.attackerAccountId);
			}
			return;
		}
		if (event.kind == Common::HaveApi::CombatEventKind::Respawn)
		{
			match->onRespawn(*victim, event.hasPosition != 0 ? std::optional{ SpawnPoint{ event.x, event.y } } : std::nullopt);
			return;
		}
		const auto attacker = event.attackerAccountId != 0 ? sessionsManager.getSessionByAccountId(event.attackerAccountId) : nullptr;
		if (event.kind == Common::HaveApi::CombatEventKind::Hit || event.kind == Common::HaveApi::CombatEventKind::SparedHit)
		{
			if (attacker)
			{
				match->onHit(*victim, *attacker, event.cause);
			}
			if (event.kind == Common::HaveApi::CombatEventKind::SparedHit)
			{
				match->onSparedHit(*victim);
			}
			return;
		}
		match->onDeath(*victim, attacker.get(), event.cause);
	}
}

#endif
