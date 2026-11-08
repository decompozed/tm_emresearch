/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#ifndef COMMON_HAVE_API_LAVA_H
#define COMMON_HAVE_API_LAVA_H

#include <cstdint>
#include "Utils/Macros.h"

// Lava Escape (party game 5). The DLL only draws the lava: a floor (0x23: absolute world height + rise speed) and four
// walls (0x24: absolute world x / y + speed each), moved every frame by the client. It has no kill logic. CastServer,
// which receives the player positions (order 281), owns the lava geometry and kills the players the lava reaches;
// MainServer (party rules and timing) drives it with M2C_partyLava and forwards the geometry to the clients.
namespace Common::HaveApi
{
	enum class LavaAction : std::uint8_t
	{
		Start = 1,    // place the lava from the current player positions and start moving it
		SetSpeed = 2, // new speeds from the current position (hot phase, sleep phase)
		Stop = 3,     // hide: no more kills
		Probe = 4,    // position of the player named by the IPC session (calibration command)
		ProtectPlayers = 5,   // for the whole match: a hit of another player never takes the last hit point
		UnprotectPlayers = 6, // also arms every disarmed player again
		DisarmPlayer = 7,     // Prop Hunt: the player named by the IPC session damages nobody (props; hunters while hiding);
		ArmPlayer = 8,        // CastServer drops that player's weapon / melee damage packets (the 0x14 flag only tells the DLL)
		KillPlayer = 9,       // Prop Hunt: the player named by the IPC session dies for everybody (as a lava death)
	};

PACK_PUSH(1)
	struct LavaCommand
	{
		LavaAction action{};
		std::uint8_t wallsOn{};
		std::uint16_t reserved{};
		float floorSpeed{};       // Start: world units per second of the rising floor
		float wallCloseSeconds{}; // Start: seconds for the walls to reach the map centre
		float speedFactor = 1.0f; // Start / SetSpeed: 1 normal, 2 hot phase, 0 sleep phase
	};

	// Geometry now (Cast's answer to every command). Probe: floorZ / wallX1 / wallY1 = the player's z / x / y.
	struct LavaState
	{
		std::uint8_t active{};
		std::uint8_t fromMap{};   // 1: placed from the map bounds (LavaMaps.h), 0: from the player positions
		std::uint8_t wallsOn{};
		std::uint8_t reserved{};
		float floorZ{};           // the lava surface: players below it die (0x23 needs lavaFloorNodeHeight)
		float floorSpeed{};
		float wallX1{}, wallX1Speed{}, wallX2{}, wallX2Speed{}; // kill when x < wallX1 or x > wallX2
		float wallY1{}, wallY1Speed{}, wallY2{}, wallY2Speed{}; // kill when y < wallY1 or y > wallY2
	};
PACK_POP()

	static_assert(sizeof(LavaCommand) == 16);
	static_assert(sizeof(LavaState) == 44);

	// party/Lava.nif (map.dat): the node "lava_floor" holds a box of 50000 x 50000 x 20000 whose top face is 20000
	// above the node (default node height -20000: top at 0). 0x23 moves the node, so the surface is at height + 20000.
	inline constexpr float lavaFloorModelDepth = 20000.0f;

	[[nodiscard]] constexpr float lavaFloorNodeHeight(float surface)
	{
		return surface - lavaFloorModelDepth;
	}

	// party/LavaWallX.nif / LavaWallY.nif: a plane 1 unit thick at the node's x (y for the Y walls, rotated 90 degrees),
	// 50000 long around 0 and 50000 high from the node's z (the DLL puts it at z 0 when the floor is shown). The floor
	// is 50000 x 50000 around (0, 0). Every map's collision mesh lies within +/-25000 around the origin (LavaMaps.h),
	// so the models cover every map: no map is excluded by the models.
	inline constexpr float lavaModelHalfSize = 25000.0f;
}

#endif
