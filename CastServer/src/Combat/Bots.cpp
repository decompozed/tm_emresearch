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
#include "Structures/Respawn/RespawnPackets.h"

namespace Cast::Combat
{
	namespace
	{
		std::string_view checkBot(const World& world, const Structures::UniqueId& bot)
		{
			if (bot.unknown != 0) return "bad bot key";
			if (resolve(world, bot).isPlayer()) return "bot key is a player";
			return {};
		}
	}

	Verdict botMove(World& world, const Packet& packet)
	{
		if (const auto reason = checkHostOutcome(world, packet); !reason.empty()) return Verdict::drop(reason);
		if (packet.mission != 0) return Verdict::drop("bad mission");
		if (packet.payload.size() != sizeof(Structures::BotMovePacket)) return Verdict::drop("bad size");
		const auto move = load<Structures::BotMovePacket>(packet.payload);
		if (move.state.reserved != 0) return Verdict::drop("reserved state bits set");
		if (const auto reason = checkBot(world, move.bot); !reason.empty()) return Verdict::drop(reason);
		return Verdict::to(Route::Match, packet);
	}

	Verdict botRespawn(World& world, const Packet& packet)
	{
		if (const auto reason = checkHostOutcome(world, packet); !reason.empty()) return Verdict::drop(reason);
		if (!hasEmptyHeader(packet)) return Verdict::drop("bad header");
		if (packet.payload.size() != sizeof(Structures::RespawnPacket)) return Verdict::drop("bad size");
		if (const auto reason = checkBot(world, load<Structures::RespawnPacket>(packet.payload).unit); !reason.empty()) return Verdict::drop(reason);
		return Verdict::to(Route::Match, packet);
	}
}
