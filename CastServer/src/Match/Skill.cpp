/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#include "Combat/Validation.h"
#include "Match/Rules.h"
#include "Match/Validation.h"
#include "Structures/Match/MatchFlowPackets.h"

namespace Cast::Match
{
	namespace
	{
		Verdict request(const World& world, const Packet& packet)
		{
			if (const auto reason = checkPlayerRequest(world, packet); !reason.empty()) return Verdict::drop(reason);
			if (!Combat::hasEmptyHeader(packet) || packet.payload.size() != sizeof(Structures::SkillRequest)) return Verdict::drop("bad header or size");
			return Verdict::to(Combat::Route::Host, packet);
		}

		Verdict outcome(const World& world, const Packet& packet)
		{
			if (!Combat::hasEmptyHeader(packet) || packet.payload.size() != sizeof(Structures::SkillUse)) return Verdict::drop("bad header or size");
			const auto use = Combat::load<Structures::SkillUse>(packet.payload);
			if (use.player.unknown != 0) return Verdict::drop("bad player key");
			if (!Combat::resolve(world, use.player).isPlayer()) return Verdict::drop("player is not in the room");
			return Verdict::to(Combat::Route::Match, packet);
		}
	}

	Verdict skillUse(World& world, const Packet& packet)
	{
		return world.isHost(packet.sender) ? outcome(world, packet) : request(world, packet);
	}
}
