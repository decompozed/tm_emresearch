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
#include "Structures/Items/ItemPackets.h"

namespace Cast::Combat
{
	namespace
	{
		constexpr std::uint8_t itemUsed = 1;
		constexpr std::uint8_t itemUseFailed = 2;
	}

	Verdict itemUseRequest(World& world, const Packet& packet)
	{
		if (const auto reason = checkRequester(world, packet); !reason.empty()) return Verdict::drop(reason);
		if (packet.extra != 0 || packet.option != 0) return Verdict::drop("bad header");
		if (packet.payload.size() != sizeof(Structures::ItemUseRequest)) return Verdict::drop("bad size");
		return Verdict::to(Route::Host, packet);
	}

	Verdict itemUse(World& world, const Packet& packet)
	{
		if (const auto reason = checkHostOutcome(world, packet); !reason.empty()) return Verdict::drop(reason);
		if (packet.mission != 0 || packet.option != 0 || (packet.extra != itemUsed && packet.extra != itemUseFailed)) return Verdict::drop("bad header");
		if (packet.payload.size() != sizeof(Structures::ItemUsePacket)) return Verdict::drop("bad size");
		if (!isRoomPlayer(world, load<Structures::ItemUsePacket>(packet.payload).player)) return Verdict::drop("player is not in the room");
		return Verdict::to(Route::Match, packet);
	}
}
