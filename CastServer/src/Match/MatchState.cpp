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

namespace Cast::Match
{
	namespace
	{
		Verdict memberSignal(const World& world, const Packet& packet)
		{
			if (const auto reason = checkMember(world, packet); !reason.empty()) return Verdict::drop(reason);
			if (!isEmptyCommand(packet)) return Verdict::drop("bad header or size");
			return Verdict::to(Combat::Route::Handled, packet);
		}
	}

	Verdict matchEnd(World& world, const Packet& packet)
	{
		if (const auto reason = checkHostState(world, packet); !reason.empty()) return Verdict::drop(reason);
		world.endMatch();
		return Verdict::to(Combat::Route::Handled, packet);
	}

	Verdict matchStart(World& world, const Packet& packet)
	{
		if (const auto reason = checkHostState(world, packet); !reason.empty()) return Verdict::drop(reason);
		world.startMatch();
		return Verdict::to(Combat::Route::Handled, packet);
	}

	Verdict matchLeave(World& world, const Packet& packet)
	{
		const auto verdict = memberSignal(world, packet);
		if (verdict.route == Combat::Route::Handled)
		{
			world.leaveMatch(packet.sender);
		}
		return verdict;
	}

	Verdict guestReady(World& world, const Packet& packet)
	{
		return memberSignal(world, packet);
	}

	Verdict matchStop(World& world, const Packet& packet)
	{
		return memberSignal(world, packet);
	}
}
