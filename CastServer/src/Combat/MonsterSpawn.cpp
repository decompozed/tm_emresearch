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
	Verdict monsterSpawn(World& world, const Packet& packet)
	{
		if (const auto reason = checkHostOutcome(world, packet); !reason.empty()) return Verdict::drop(reason);
		if (!hasEmptyHeader(packet)) return Verdict::drop("bad header");
		if (packet.payload.size() != sizeof(Structures::MonsterSpawnPacket)) return Verdict::drop("bad size");
		return Verdict::to(Route::Match, packet);
	}
}
