/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#include "Combat/Rules.h"
#include "Combat/Validation.h"
#include "Structures/Respawn/RespawnPackets.h"

namespace Cast::Combat
{
	Verdict respawnRequest(World& world, const Packet& packet)
	{
		if (!world.isAuthorizedMember(packet.sender)) return Verdict::drop("sender is not a room member");
		if (world.isHost(packet.sender)) return Verdict::drop("request from the host itself");
		if (packet.extra != 0 || packet.option != 0) return Verdict::drop("bad header");
		const bool hostPicks = packet.mission == Structures::hostPickedSpawn && packet.payload.empty();
		const bool positionGiven = packet.mission == Structures::chosenSpawn && packet.payload.size() == sizeof(Structures::RespawnRequest);
		if (!hostPicks && !positionGiven) return Verdict::drop("bad mission or size");
		return Verdict::to(Route::Host, packet);
	}

	Verdict playerRespawn(World& world, const Packet& packet)
	{
		if (const auto reason = checkHostOutcome(world, packet); !reason.empty()) return Verdict::drop(reason);
		if (!hasEmptyHeader(packet)) return Verdict::drop("bad header");
		if (packet.payload.size() != sizeof(Structures::RespawnPacket)) return Verdict::drop("bad size");
		const auto respawn = load<Structures::RespawnPacket>(packet.payload);
		if (respawn.unit.unknown != 0) return Verdict::drop("bad unit key");
		const Unit player = resolve(world, respawn.unit);
		if (!player.isPlayer()) return Verdict::drop("unit is not a player of the room");
		world.respawn(packet.sender, player.session, respawn.position);
		return Verdict::to(Route::Match, packet);
	}
}
