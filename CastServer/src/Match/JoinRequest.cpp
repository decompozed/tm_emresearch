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
#include "Structures/Match/MatchFlowPackets.h"

namespace Cast::Match
{
	Verdict joinStateRequest(World& world, const Packet& packet)
	{
		if (const auto reason = checkPlayerRequest(world, packet); !reason.empty()) return Verdict::drop(reason);
		if (packet.mission != Structures::joinRequestMission || packet.extra != 0 || packet.option != 0 || !packet.payload.empty())
		{
			return Verdict::drop("bad header or size");
		}
		return Verdict::to(Combat::Route::Host, packet);
	}
}
