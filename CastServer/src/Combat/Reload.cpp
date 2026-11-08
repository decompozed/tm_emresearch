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

namespace Cast::Combat
{
	Verdict reload(World& world, const Packet& packet)
	{
		if (const auto reason = checkHostOutcome(world, packet); !reason.empty()) return Verdict::drop(reason);
		if (!hasEmptyHeader(packet)) return Verdict::drop("bad header");
		if (packet.payload.size() != sizeof(Structures::ReloadPacket)) return Verdict::drop("bad size");
		const Unit player = resolve(world, load<Structures::ReloadPacket>(packet.payload).player);
		if (!player.isPlayer() || !player.inMatch()) return Verdict::drop("player is not in the match");
		return Verdict::to(Route::Match, packet);
	}

	Verdict reloadRequest(World& world, const Packet& packet)
	{
		if (const auto reason = checkRequester(world, packet); !reason.empty()) return Verdict::drop(reason);
		if (packet.extra != 0 || packet.option != 0) return Verdict::drop("bad header");
		if (packet.payload.size() != sizeof(Structures::ReloadRequest)) return Verdict::drop("bad size");
		return Verdict::to(Route::Host, packet);
	}
}
