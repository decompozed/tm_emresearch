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
	Verdict mapItemSpawn(World& world, const Packet& packet)
	{
		if (const auto reason = checkHostOutcome(world, packet); !reason.empty()) return Verdict::drop(reason);
		if (packet.mission > 1 || packet.extra != 0 || packet.option != 1) return Verdict::drop("bad header");
		if (packet.payload.size() != sizeof(Structures::MapItemEntry)) return Verdict::drop("bad size");
		if (load<Structures::MapItemEntry>(packet.payload).owner.unknown != 0) return Verdict::drop("bad owner key");
		return Verdict::to(Route::Match, packet);
	}
}
