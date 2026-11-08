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
#include "Combat/Rules.h"
#include "Combat/Validation.h"
#include "Structures/Items/BombPackets.h"

namespace Cast::Combat
{
	namespace
	{
		std::string_view checkBombHeader(const Packet& packet)
		{
			if (packet.mission != Structures::bombDefuse && packet.mission != Structures::bombPlant) return "bad mission";
			const bool knownAction = packet.extra == Structures::bombActionCancel || packet.extra == Structures::bombActionStart ||
				packet.extra == Structures::bombActionComplete;
			if (!knownAction || packet.option != 0) return "bad extra or option";
			return {};
		}
	}

	Verdict bombActionRequest(World& world, const Packet& packet)
	{
		if (const auto reason = checkLiveRequester(world, packet); !reason.empty()) return Verdict::drop(reason);
		if (const auto reason = checkBombHeader(packet); !reason.empty()) return Verdict::drop(reason);
		if (packet.payload.size() != sizeof(Structures::BombActionRequest)) return Verdict::drop("bad size");
		if (load<Structures::BombActionRequest>(packet.payload).reserved != 0) return Verdict::drop("reserved field set");
		return Verdict::to(Route::Host, packet);
	}

	Verdict bombAction(World& world, const Packet& packet)
	{
		if (const auto reason = checkHostOutcome(world, packet); !reason.empty()) return Verdict::drop(reason);
		if (const auto reason = checkBombHeader(packet); !reason.empty()) return Verdict::drop(reason);
		if (packet.payload.size() != sizeof(Structures::BombActionPacket)) return Verdict::drop("bad size");
		const auto action = load<Structures::BombActionPacket>(packet.payload);
		if (action.request.reserved != 0 || !std::ranges::all_of(action.padding, [](std::uint8_t byte) { return byte == 0; })) return Verdict::drop("reserved field set");
		if (!isRoomPlayer(world, action.player)) return Verdict::drop("player is not in the room");
		return Verdict::to(Route::Match, packet);
	}

	Verdict bombDropRequest(World& world, const Packet& packet)
	{
		if (const auto reason = checkRequester(world, packet); !reason.empty()) return Verdict::drop(reason);
		if (!hasEmptyHeader(packet) || !packet.payload.empty()) return Verdict::drop("bad header or size");
		return Verdict::to(Route::Host, packet);
	}
}
