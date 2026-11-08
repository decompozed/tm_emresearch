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
	namespace
	{
		constexpr std::uint8_t lastScheduleKind = 1;
	}

	Verdict bossSchedule(World& world, const Packet& packet)
	{
		if (const auto reason = checkHostOutcome(world, packet); !reason.empty()) return Verdict::drop(reason);
		if (packet.mission != 0 || packet.extra != 0 || packet.option > lastScheduleKind) return Verdict::drop("bad header");
		if (packet.payload.size() != Structures::bossScheduleSize) return Verdict::drop("bad size");
		const auto header = load<Structures::BossScheduleHeader>(packet.payload);
		if (header.count > Structures::maxBossScheduleEntries || header.countCopy != header.count) return Verdict::drop("bad entry count");
		return Verdict::to(Route::Match, packet);
	}
}
