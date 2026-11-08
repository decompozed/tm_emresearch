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
#include "Enums/RoomEnums.h"

namespace Cast::Combat
{
	namespace
	{
		constexpr std::uint16_t meleeHit = 267;
		constexpr std::uint16_t rifleHit = 268;
		constexpr std::uint16_t shotgunHit = 269;
		constexpr std::uint8_t miss = 0;
		constexpr std::uint8_t hit = 1;

		std::string_view checkShooter(World& world, const Structures::ShotTrace& trace, Unit& attacker)
		{
			if (trace.tag != 0) return "trace tag is not 0";
			if (!isFinite(trace)) return "trace is not finite";
			if (!attacker.inMatch()) return "attacker is not in the match";
			if (attacker.isPlayer() && world.disarmed(attacker.session)) return "attacker is disarmed";
			reviveIfStale(world, attacker);
			return {};
		}

		bool validSingleHitHeader(const World& world, const Packet& packet)
		{
			const bool infection = packet.order == meleeHit && packet.mission == 1 && world.mode() == Common::Enums::ZombieMode;
			if (packet.mission != 0 && !infection) return false;
			return packet.order == rifleHit ? packet.option == 0 : packet.option <= 1;
		}

		Verdict shotgunMiss(World& world, const Packet& packet)
		{
			if (packet.order != shotgunHit || packet.option != 0 || packet.payload.size() != sizeof(Structures::MultiHitHeader)) return Verdict::drop("bad miss");
			const auto header = load<Structures::MultiHitHeader>(packet.payload);
			Unit attacker = resolve(world, header.attacker);
			if (const auto reason = checkShooter(world, header.trace, attacker); !reason.empty()) return Verdict::drop(reason);
			return Verdict::to(Route::Match, packet);
		}
	}

	Verdict multiHit(World& world, const Packet& packet)
	{
		if (const auto reason = checkHostOutcome(world, packet); !reason.empty()) return Verdict::drop(reason);
		if (packet.mission != 0) return Verdict::drop("mission is not 0");
		if (packet.extra == miss) return shotgunMiss(world, packet);
		if (packet.extra != hit || packet.option == 0 || packet.option > Structures::maxShotEntries || packet.payload.size() < sizeof(Structures::MultiHitHeader)) return Verdict::drop("bad extra, option or size");

		const auto header = load<Structures::MultiHitHeader>(packet.payload);
		Unit attacker = resolve(world, header.attacker);
		if (const auto reason = checkShooter(world, header.trace, attacker); !reason.empty()) return Verdict::drop(reason);
		return applyDamageEntries(world, packet, sizeof(header), { attacker.playerSession(), rawId(header.attacker), Cause::Weapon });
	}

	Verdict singleHit(World& world, const Packet& packet)
	{
		if (const auto reason = checkHostOutcome(world, packet); !reason.empty()) return Verdict::drop(reason);
		if (packet.extra != hit || packet.payload.size() != sizeof(Structures::SingleHitPacket)) return Verdict::drop("bad extra or size");
		if (!validSingleHitHeader(world, packet)) return Verdict::drop("bad mission or option");

		auto shot = load<Structures::SingleHitPacket>(packet.payload);
		if (shot.hp.reserved != 0) return Verdict::drop("reserved HP bits set");
		if (rawId(shot.attacker) == rawId(shot.target)) return Verdict::drop("attacker hits itself");
		Unit attacker = resolve(world, shot.attacker);
		if (const auto reason = checkShooter(world, shot.trace, attacker); !reason.empty()) return Verdict::drop(reason);
		const Unit target = resolve(world, shot.target);
		if (!target.inMatch()) return Verdict::drop("target is not in the match");

		if (const auto session = target.playerSession())
		{
			const bool spared = spareLethalHit(world, attacker.playerSession(), *session, shot.hp);
			if (spared)
			{
				store(packet.payload, 0, shot);
			}
			if (!target.deadPlayer())
			{
				reportDamage(world, *session, shot.hp, attacker.playerSession(), Cause::Weapon, spared);
			}
		}
		return Verdict::to(Route::Match, packet);
	}
}
