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
		constexpr std::uint8_t maxPickupExtra = 2;

		[[nodiscard]] constexpr bool isPositiveId(std::uint32_t id) noexcept { return id != 0 && id <= 0x7FFFFFFF; }
	}

	Verdict itemPickupRequest(World& world, const Packet& packet)
	{
		if (packet.mission != 0) return Verdict::drop("mission is not 0");
		if (const auto reason = checkMatchRequester(world, packet); !reason.empty()) return Verdict::drop(reason);
		if (packet.extra != 0 || packet.option != 0) return Verdict::drop("bad header");
		if (packet.payload.size() != sizeof(Structures::ItemPickupRequest)) return Verdict::drop("bad size");
		const auto request = load<Structures::ItemPickupRequest>(packet.payload);
		if (request.itemObjectId == 0 || request.capsuleItemId != 0) return Verdict::drop("bad item");
		return Verdict::to(Route::Host, packet);
	}

	Verdict itemPickup(World& world, const Packet& packet)
	{
		if (const auto reason = checkHostOutcome(world, packet); !reason.empty()) return Verdict::drop(reason);
		if (packet.mission != 0 || packet.option != 0 || packet.extra > maxPickupExtra) return Verdict::drop("bad header");
		if (packet.payload.size() != sizeof(Structures::ItemPickupPacket)) return Verdict::drop("bad size");
		const auto pickup = load<Structures::ItemPickupPacket>(packet.payload);
		if (!isPositiveId(pickup.itemObjectId) || !isPositiveId(pickup.itemId)) return Verdict::drop("bad item");
		if (!isRoomPlayer(world, pickup.player)) return Verdict::drop("player is not in the room");
		return Verdict::to(Route::Match, packet);
	}
}
