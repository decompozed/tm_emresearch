/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#ifndef COMMON_HAVE_API_COMBAT_EVENT_H
#define COMMON_HAVE_API_COMBAT_EVENT_H

#include <cstdint>
#include "Utils/Macros.h"

// Deaths and respawns of the players of a party match, reported by CastServer (which sees the in-match combat)
// to MainServer (which runs the party rules). IPC id: Common::Constants::C2M_partyCombatEvent.
namespace Common::HaveApi
{
	enum class CombatEventKind : std::uint8_t
	{
		Death = 1,
		Respawn = 2,
		Hit = 3,        // damage that did not kill (Chicken Run: the chicken passes its role by hitting)
		SparedHit = 4,  // a hit of another player that would have killed, left at 1 hit point (Lava Escape)
	};

	enum class DeathCause : std::uint8_t
	{
		None = 0,       // respawn
		Weapon = 1,     // orders 265..270
		Explosive = 2,  // order 264: explosions, falls and self kills (no attacker in the packet)
		Server = 3,     // killed by the server (party rules)
	};

PACK_PUSH(1)
	struct CombatEvent
	{
		std::uint32_t victimAccountId{};
		std::uint32_t attackerAccountId{}; // 0 = no attacker known
		CombatEventKind kind{};
		DeathCause cause{};
		std::uint8_t hasPosition{};        // Respawn: x / y are where the client respawned the player (untouched)
		float x{};
		float y{};
	};
PACK_POP()

	static_assert(sizeof(CombatEvent) == 19);
}

#endif
