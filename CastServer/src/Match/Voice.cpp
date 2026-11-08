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
	Verdict voiceMessage(World& world, const Packet& packet)
	{
		if (const auto reason = checkMember(world, packet); !reason.empty()) return Verdict::drop(reason);
		if (!Combat::hasEmptyHeader(packet) || packet.payload.size() != sizeof(Structures::VoiceRequest))
		{
			return Verdict::drop("bad header or size");
		}

		const auto request = Combat::load<Structures::VoiceRequest>(packet.payload);
		const Structures::VoiceMessage message{ playerId(world, packet.sender), request.voiceId };
		world.send({ Combat::Route::MatchExceptSender, packet.sender, 0, Structures::voiceMessageOrder, 0, 0, 0,
			{ reinterpret_cast<const std::uint8_t*>(&message), sizeof(message) } });
		return Verdict::to(Combat::Route::Handled, packet);
	}
}
