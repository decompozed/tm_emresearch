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
#include "Structures/Npc/NpcPackets.h"

namespace Cast::Combat
{
	Verdict npcMoves(World& world, const Packet& packet)
	{
		if (const auto reason = checkHostOutcome(world, packet); !reason.empty()) return Verdict::drop(reason);
		if (!hasEmptyHeader(packet)) return Verdict::drop("bad header");
		if (packet.payload.size() < sizeof(std::uint32_t)) return Verdict::drop("bad size");
		const auto count = load<std::uint32_t>(packet.payload);
		if (count == 0 || count > Structures::maxNpcMoveEntries) return Verdict::drop("entry count out of 1..45");
		if (packet.payload.size() != sizeof(std::uint32_t) + count * sizeof(Structures::NpcMoveEntry)) return Verdict::drop("size does not match the entry count");
		return Verdict::to(Route::MatchExceptSender, packet);
	}
}
