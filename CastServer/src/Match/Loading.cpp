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
	Verdict loadingProgress(World& world, const Packet& packet)
	{
		if (const auto reason = checkMember(world, packet); !reason.empty()) return Verdict::drop(reason);
		if (packet.mission != 0 || packet.extra != 0 || !packet.payload.empty()) return Verdict::drop("bad header or size");

		const Structures::LoadingProgress progress{ playerId(world, packet.sender) };
		world.send({ Combat::Route::Match, packet.sender, 0, Structures::loadingProgressOrder, 0, 0, packet.option,
			{ reinterpret_cast<const std::uint8_t*>(&progress), sizeof(progress) } });
		const auto state = world.player(packet.sender);
		if (packet.option == Structures::loadingDone && (!state || !state->inMatch || state->dead))
		{
			world.finishLoading(packet.sender);
		}
		return Verdict::to(Combat::Route::Handled, packet);
	}
}
