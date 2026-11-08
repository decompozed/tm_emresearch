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
	namespace
	{
		constexpr std::uint32_t lethalEffectId = 130; // sub_A6ED50 -> sub_E1D550, the only sender

		std::string_view checkHeader(const World& world, const Packet& packet, std::size_t size)
		{
			if (const auto reason = checkHostOutcome(world, packet); !reason.empty()) return reason;
			if (packet.mission != 0 || packet.option != 0) return "mission or option is not 0";
			if (packet.payload.size() != size) return "bad size";
			return {};
		}
	}

	Verdict itemEffect(World& world, const Packet& packet)
	{
		if (const auto reason = checkHeader(world, packet, sizeof(Structures::ItemEffectPacket)); !reason.empty()) return Verdict::drop(reason);
		const auto effect = load<Structures::ItemEffectPacket>(packet.payload);
		const Unit target = resolve(world, effect.target);
		if (!target.inMatch()) return Verdict::drop("target is not in the match");
		if (!resolve(world, effect.user).inMatch()) return Verdict::drop("user is not in the match");
		return Verdict::to(Route::Match, packet);
	}

	Verdict stateKill(World& world, const Packet& packet)
	{
		if (const auto reason = checkHeader(world, packet, sizeof(Structures::StateKillPacket)); !reason.empty()) return Verdict::drop(reason);
		const auto kill = load<Structures::StateKillPacket>(packet.payload);
		if (kill.effectId != lethalEffectId) return Verdict::drop("unknown effect");
		const Unit attacker = resolve(world, kill.attacker);
		if (attacker.kind == UnitKind::Invalid || attacker.state.observer) return Verdict::drop("attacker is not in the room");
		const Unit victim = resolve(world, kill.victim);
		if (!victim.inMatch()) return Verdict::drop("victim is not in the match");
		if (const auto session = victim.playerSession(); session && !victim.deadPlayer())
		{
			world.death(*session, attacker.playerSession(), Cause::Weapon);
		}
		return Verdict::to(Route::Match, packet);
	}

	Verdict effectOutcome(World& world, const Packet& packet)
	{
		switch (packet.extra)
		{
		case 0:
			return itemEffect(world, packet);
		case 1:
			return stateKill(world, packet);
		default:
			return Verdict::drop("unknown extra");
		}
	}
}
