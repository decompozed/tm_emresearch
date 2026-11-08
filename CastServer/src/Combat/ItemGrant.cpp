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
		bool isGrantHeader(const Packet& packet) noexcept
		{
			const bool modeGrant = packet.mission == 1 && packet.extra == 1;
			const bool plainGrant = packet.mission == 0 && packet.extra == 0 && packet.option == 0;
			return modeGrant || plainGrant;
		}
	}

	Verdict itemGrant(World& world, const Packet& packet)
	{
		if (const auto reason = checkHostOutcome(world, packet); !reason.empty()) return Verdict::drop(reason);
		if (!isGrantHeader(packet)) return Verdict::drop("bad header");
		if (packet.payload.size() != sizeof(Structures::ItemGrantPacket)) return Verdict::drop("bad size");
		const auto grant = load<Structures::ItemGrantPacket>(packet.payload);
		if (grant.player.unknown != 0) return Verdict::drop("bad unit key");
		if (grant.item.granted != 1 || grant.item.reserved != 0) return Verdict::drop("bad item field");
		return Verdict::to(Route::Match, packet);
	}
}
