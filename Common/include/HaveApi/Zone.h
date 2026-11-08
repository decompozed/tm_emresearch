/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#ifndef COMMON_HAVE_API_ZONE_H
#define COMMON_HAVE_API_ZONE_H

#include <array>
#include <cstdint>
#include "Utils/Macros.h"

// Hardpoint zone, MainServer -> CastServer (M2C_partyZone; the answer to Set carries the occupants). CastServer has
// the positions (order 281) and the respawn packets (order 276): it sends the occupants to MainServer whenever they
// change (C2M_partyZoneOccupants). Respawns are never moved: a player who respawned within keepOut of the zone does not
// count as an occupant for PartyZone::spawnGrace (CastServer/include/Rooms/PartyZone.h), and MainServer puts the next
// zone away from the spawns the clients used (C2M_partyCombatEvent Respawn carries the position). Hardpoint /
// Headquarters: one zone (index 0), a vertical cylinder (only x / y count). Domination: three zones (index 0..2),
// squares of half side `radius`, a player counting only between zMin and zMax (zMin == zMax: no height check).
namespace Common::HaveApi
{
	enum class ZoneAction : std::uint8_t
	{
		Set = 1,   // zone + the team of every player
		Clear = 2, // no zone
		Query = 3, // who is in the zone now (unused: CastServer pushes the changes, C2M_partyZoneOccupants)
		AddMembers = 4, // players joining the running match (late joiners): they count too, the zone unchanged
	};

	inline constexpr std::size_t maxZonePlayers = 16;
	inline constexpr std::size_t maxZones = 3;

	enum class ZoneShape : std::uint8_t
	{
		Circle = 0,
		Square = 1, // axis-aligned, half side = radius
	};

PACK_PUSH(1)
	struct ZoneMember
	{
		std::uint32_t sessionId{};
		std::int32_t team{};
	};

	struct ZoneCommand
	{
		ZoneAction action{};
		std::uint8_t count{}; // members used
		std::uint8_t index{}; // zone 0..maxZones-1
		ZoneShape shape{};
		float x{};
		float y{};
		float radius{};
		float keepOut{};      // a respawn closer than this to the zone centre gets the spawn grace (not an occupant)
		float zMin{};
		float zMax{};
		std::array<ZoneMember, maxZonePlayers> members{};
	};

	struct ZoneAnswer
	{
		std::uint8_t done{};
		std::uint8_t count{}; // sessions in the zone
		std::uint8_t index{}; // the zone
		std::uint8_t reserved{};
		std::array<std::uint32_t, maxZonePlayers> sessions{};
	};
PACK_POP()

	static_assert(sizeof(ZoneCommand) == 28 + 8 * maxZonePlayers);
	static_assert(sizeof(ZoneAnswer) == 4 + 4 * maxZonePlayers);
}

#endif
