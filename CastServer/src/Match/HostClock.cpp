/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#include "Match/Rules.h"
#include "Match/Validation.h"
#include "Structures/Match/JoinStatePackets.h"

namespace Cast::Match
{
	Verdict hostClock(World& world, const Packet& packet)
	{
		if (const auto reason = checkHostToPlayer(world, packet); !reason.empty()) return Verdict::drop(reason);
		if (packet.option != 0) return Verdict::drop("bad option");
		const bool clock = packet.extra == Structures::joinStateExtra && packet.payload.size() == sizeof(Structures::HostClock);
		const bool noMod = packet.extra == Structures::noModExtra && packet.payload.empty();
		if (!clock && !noMod) return Verdict::drop("bad extra or size");
		return Verdict::to(Combat::Route::Recipient, packet);
	}
}
