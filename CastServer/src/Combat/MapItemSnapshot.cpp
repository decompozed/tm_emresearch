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
	Verdict mapItemSnapshot(World& world, const Packet& packet)
	{
		if (const auto reason = checkSnapshot(world, packet); !reason.empty()) return Verdict::drop(reason);
		if (packet.mission != 0 || (packet.extra != 0 && packet.extra != Structures::mapItemSnapshotFirstPart)) return Verdict::drop("bad header");
		const std::size_t count = packet.option;
		if (count == 0 || count > Structures::maxMapItemSnapshotEntries) return Verdict::drop("entry count out of 1..51");
		if (packet.payload.size() != count * sizeof(Structures::MapItemEntry)) return Verdict::drop("size does not match the entry count");
		for (std::size_t i = 0; i < count; ++i)
		{
			if (load<Structures::MapItemEntry>(packet.payload, i * sizeof(Structures::MapItemEntry)).itemObjectId == 0) return Verdict::drop("bad item");
		}
		return Verdict::to(Route::Recipient, packet);
	}
}
