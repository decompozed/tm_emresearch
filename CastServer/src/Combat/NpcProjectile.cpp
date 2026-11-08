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

namespace Cast::Combat
{
	namespace
	{
		constexpr std::uint8_t maxAttackId = 1; // sub_B1FCC0 callers 0xA76010, 0xA76F9A, 0xA7912E, 0xA7915A
	}

	Verdict npcProjectile(World& world, const Packet& packet)
	{
		if (const auto reason = checkHostOutcome(world, packet); !reason.empty()) return Verdict::drop(reason);
		if (packet.mission != 0 || packet.extra != 0) return Verdict::drop("bad header");
		if (packet.option > maxAttackId) return Verdict::drop("attack id out of 0..1");
		if (packet.payload.size() != sizeof(Structures::NpcProjectilePacket)) return Verdict::drop("bad size");
		const auto projectile = load<Structures::NpcProjectilePacket>(packet.payload);
		if (projectile.unused != 0 || projectile.projectileId == 0) return Verdict::drop("bad projectile fields");
		return Verdict::to(Route::Match, packet);
	}
}
