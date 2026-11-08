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
	Verdict effectSnapshot(World& world, const Packet& packet)
	{
		if (const auto reason = checkSnapshot(world, packet); !reason.empty()) return Verdict::drop(reason);
		if (packet.mission != 0 || packet.extra != 0) return Verdict::drop("bad header");
		const std::size_t count = packet.option;
		if (count == 0 || count > Structures::maxEffectSnapshotEntries) return Verdict::drop("entry count out of 1..71");
		if (packet.payload.size() != count * sizeof(Structures::UnitEffectEntry)) return Verdict::drop("size does not match the entry count");
		for (std::size_t i = 0; i < count; ++i)
		{
			const auto entry = load<Structures::UnitEffectEntry>(packet.payload, i * sizeof(Structures::UnitEffectEntry));
			if (entry.unit.unknown != 0 || entry.reserved != 0) return Verdict::drop("reserved bits set");
			if (entry.unit.session == packet.target && entry.unit.server == world.serverId()) return Verdict::drop("effect of the recipient itself");
		}
		return Verdict::to(Route::Recipient, packet);
	}
}
