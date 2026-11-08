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
		enum ExplosionKind : std::uint8_t
		{
			Fall = 0,
			Projectile = 1,
			Area = 2,
			SuicideBomb = 13,
		};

		Verdict fallDeath(World& world, const Packet& packet)
		{
			if (packet.payload.size() != sizeof(Structures::FallDeathPacket) || packet.option != 1) return Verdict::drop("bad size or option");
			const auto fall = load<Structures::FallDeathPacket>(packet.payload);
			const auto& hp = fall.victim.hp;
			if (fall.header.sourceId != rawId(fall.victim.target)) return Verdict::drop("source is not the victim");
			if (hp.hp != 0 || hp.hitPart != 0 || hp.reserved != 0) return Verdict::drop("fall death with HP");

			const Unit victim = resolve(world, fall.victim.target);
			if (!victim.inMatch()) return Verdict::drop("victim is not in the match");
			if (const auto session = victim.playerSession(); session && !victim.deadPlayer())
			{
				world.death(*session, std::nullopt, Cause::Explosive);
			}
			return Verdict::to(Route::Match, packet);
		}

		Verdict explosion(World& world, const Packet& packet)
		{
			if (packet.payload.size() < sizeof(Structures::ExplosionDamageHeader)) return Verdict::drop("bad size");
			const auto header = load<Structures::ExplosionDamageHeader>(packet.payload);
			if (!isFinite(header.position)) return Verdict::drop("position is not finite");

			std::optional<std::uint32_t> attacker;
			if (packet.extra == Projectile)
			{
				attacker = world.explosiveOwner(header.sourceId);
			}
			else
			{
				const Unit source = resolve(world, std::bit_cast<Structures::UniqueId>(header.sourceId));
				if (!source.inMatch()) return Verdict::drop("attacker is not in the match");
				attacker = source.playerSession();
			}
			return applyDamageEntries(world, packet, sizeof(header), { attacker, std::nullopt, Cause::Explosive });
		}
	}

	Verdict explosionDamage(World& world, const Packet& packet)
	{
		if (const auto reason = checkHostOutcome(world, packet); !reason.empty()) return Verdict::drop(reason);
		if (packet.mission != 0) return Verdict::drop("mission is not 0");
		switch (packet.extra)
		{
		case Fall:
			return fallDeath(world, packet);
		case Projectile:
		case Area:
		case SuicideBomb:
			return explosion(world, packet);
		default:
			return Verdict::drop("unknown extra");
		}
	}
}
