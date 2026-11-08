/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#include <algorithm>
#include "Combat/Validation.h"
#include "Match/Rules.h"
#include "Match/Validation.h"
#include "Structures/Match/MatchFlowPackets.h"

namespace Cast::Match
{
	Verdict nameHeartbeat(World& world, const Packet& packet)
	{
		if (const auto reason = checkMember(world, packet); !reason.empty()) return Verdict::drop(reason);
		if (packet.mission != 0 || packet.extra != Structures::nameHeartbeatExtra || packet.option != 0
			|| packet.payload.size() != sizeof(Structures::NameHeartbeat))
		{
			return Verdict::drop("bad header or size");
		}
		const auto nickname = world.nickname(packet.sender);
		if (nickname.empty()) return Verdict::drop("nickname unknown");

		Structures::NameHeartbeat heartbeat{};
		std::copy_n(nickname.begin(), std::min(nickname.size(), heartbeat.name.size()), heartbeat.name.begin());
		Combat::store(packet.payload, 0, heartbeat);
		return Verdict::to(Combat::Route::Host, packet);
	}
}
