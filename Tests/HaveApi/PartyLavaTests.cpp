/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
#include "Rooms/PartyLava.h"

namespace CH = Common::HaveApi;
using Cast::Domain::PartyLava;
using namespace std::chrono_literals;

namespace
{
	CH::LavaCommand start(float floorSpeed = 100.0f, float closeSeconds = 100.0f, bool walls = true)
	{
		return { CH::LavaAction::Start, static_cast<std::uint8_t>(walls ? 1 : 0), 0, floorSpeed, closeSeconds, 1.0f };
	}

	// the floor of a start without player positions on a map with spawns and bounds
	float fallbackFloor(std::uint32_t map)
	{
		float spawn = 1e9f;
		for (const CH::PartySpawn& s : CH::findPartySpawns(map))
		{
			spawn = (std::min)(spawn, s.z);
		}
		const float ground = (std::min)(spawn, (std::max)(CH::findMapBounds(map)->minZ, spawn - PartyLava::maxDepthBelowSpawns));
		return ground - PartyLava::startMargin;
	}
}

TEST(PartyLava, WithoutPositionsKnownMapsStartBelowTheLowestSpawn)
{
	PartyLava lava;
	const auto t0 = PartyLava::Clock::now();
	const auto bitmap = CH::findMapBounds(6);
	ASSERT_TRUE(bitmap);
	const auto s = lava.start(start(), 6, t0);
	EXPECT_TRUE(s.active);
	EXPECT_EQ(s.fromMap, 1);
	ASSERT_TRUE(lava.lastStart().lowestSpawnZ);
	EXPECT_FLOAT_EQ(s.floorZ, fallbackFloor(6));
	EXPECT_LE(s.floorZ, *lava.lastStart().lowestSpawnZ - PartyLava::startMargin);
	EXPECT_LE(s.floorZ, bitmap->minZ);
	EXPECT_FLOAT_EQ(s.floorSpeed, 100.0f);
	EXPECT_FLOAT_EQ(s.wallX1, bitmap->minX - PartyLava::mapWallMargin);
	EXPECT_FLOAT_EQ(s.wallY2, bitmap->maxY + PartyLava::mapWallMargin);
	const float centreX = (s.wallX1 + s.wallX2) / 2;
	EXPECT_FLOAT_EQ(s.wallX1Speed, (centreX - s.wallX1) / 100.0f);  // reaches the centre in 100 s
	EXPECT_FLOAT_EQ(s.wallX2Speed, (centreX - s.wallX2) / 100.0f);
	EXPECT_LT(s.wallX2Speed, 0.0f);
}

// Production, map 46 (WildWest), 6 players at z ~6: the lava started from the map bounds at the lowest collision point
// (-55.3) rising 24 u/s and killed everybody 2-3 s later.
TEST(PartyLava, WildWestPlayersAreNotKilledAtTheStart)
{
	constexpr std::uint32_t wildWest = 46;
	const auto bounds = CH::findMapBounds(wildWest);
	ASSERT_TRUE(bounds);
	ASSERT_GT(6.0f - bounds->minZ, 0.0f);
	ASSERT_LT(6.0f - bounds->minZ, PartyLava::startMargin);           // the old start: 61 units below the players

	for (const bool withPositions : { true, false })
	{
		PartyLava lava;
		const auto t0 = PartyLava::Clock::now();
		if (withPositions)
		{
			for (std::uint32_t session = 1; session <= 6; ++session)
			{
				lava.setPosition(session, -1000.0f + 300.0f * static_cast<float>(session), 2000.0f, 6.0f + static_cast<float>(session), t0 - 1s);
			}
		}
		const auto s = lava.start(start(24.0f), wildWest, t0);
		ASSERT_TRUE(s.active) << withPositions;
		EXPECT_EQ(s.fromMap, withPositions ? 0 : 1);
		EXPECT_LE(s.floorZ, 7.0f - PartyLava::startMargin) << withPositions;
		for (std::uint32_t session = 1; session <= 6; ++session)
		{
			const char* reason = nullptr;
			EXPECT_FALSE(lava.reaches(session, -1000.0f + 300.0f * static_cast<float>(session), 2000.0f, 6.0f + static_cast<float>(session), reason, t0 + 15s))
				<< withPositions << " " << session;
		}
	}
}

TEST(PartyLava, FreshPositionsPlaceTheFloorUnderTheLowestPlayerAndSpawn)
{
	constexpr std::uint32_t wildWest = 46;
	PartyLava lava;
	const auto t0 = PartyLava::Clock::now();
	lava.setPosition(1, 0, 0, 900, t0);
	lava.setPosition(2, 100, 100, 1500, t0);
	const auto high = lava.start(start(), wildWest, t0);
	EXPECT_EQ(high.fromMap, 0);
	ASSERT_TRUE(lava.lastStart().lowestSpawnZ);
	EXPECT_FLOAT_EQ(high.floorZ, *lava.lastStart().lowestSpawnZ - PartyLava::startMargin); // the spawns are lower
	EXPECT_EQ(lava.lastStart().freshPositions, 2u);

	lava.setPosition(3, 0, 0, -300, t0);
	const auto low = lava.start(start(), wildWest, t0);
	EXPECT_FLOAT_EQ(low.floorZ, -300 - PartyLava::startMargin);
}

TEST(PartyLava, StalePositionsSinceTheLastStopKeepTheFloorBelowThem)
{
	PartyLava lava;
	const auto t0 = PartyLava::Clock::now();
	lava.stop(t0);
	lava.setPosition(1, 500, 500, -2000, t0 + 1s);                      // idle player: no position for 20 s
	const auto s = lava.start(start(), 46, t0 + 21s);
	EXPECT_TRUE(s.active);
	EXPECT_EQ(s.fromMap, 1);
	EXPECT_EQ(lava.lastStart().freshPositions, 0u);
	EXPECT_EQ(lava.lastStart().knownPositions, 1u);
	EXPECT_FLOAT_EQ(s.floorZ, -2000 - PartyLava::startMargin);
}

TEST(PartyLava, PositionsOlderThanTheLastStopAreIgnored)
{
	PartyLava lava;
	const auto t0 = PartyLava::Clock::now();
	lava.setPosition(1, 0, 0, -9000, t0);                               // previous round, far below the map
	lava.stop(t0 + 1s);
	const auto s = lava.start(start(), 46, t0 + 30s);
	EXPECT_EQ(lava.lastStart().knownPositions, 0u);
	EXPECT_FLOAT_EQ(s.floorZ, fallbackFloor(46));
}

TEST(PartyLava, WallsStartOutsideEveryKnownPosition)
{
	PartyLava lava;
	const auto t0 = PartyLava::Clock::now();
	const auto bounds = CH::findMapBounds(46);
	ASSERT_TRUE(bounds);
	lava.setPosition(1, bounds->maxX + 1000, bounds->minY - 1000, 50, t0);  // outside the collision bounds
	const auto s = lava.start(start(), 46, t0);
	EXPECT_FLOAT_EQ(s.wallX2, bounds->maxX + 1000 + PartyLava::mapWallMargin);
	EXPECT_FLOAT_EQ(s.wallY1, bounds->minY - 1000 - PartyLava::mapWallMargin);
	EXPECT_FLOAT_EQ(s.wallX1, bounds->minX - PartyLava::mapWallMargin);
}

TEST(PartyLava, ForgottenPositionsDoNotPlaceTheLava)
{
	PartyLava lava;
	const auto t0 = PartyLava::Clock::now();
	lava.setPosition(1, 0, 0, 100, t0);
	lava.forgetPositions();
	EXPECT_FALSE(lava.position(1));
	EXPECT_FALSE(lava.start(start(), 999, t0).active);                  // unknown map, no spawn, no position
}

TEST(PartyLava, UnknownMapsStartFromThePlayers)
{
	PartyLava lava;
	const auto t0 = PartyLava::Clock::now();
	lava.setPosition(1, 1000, 2000, 21000, t0);
	lava.setPosition(2, 3000, 5000, 22000, t0);
	const auto s = lava.start(start(), 999, t0);
	EXPECT_TRUE(s.active);
	EXPECT_EQ(s.fromMap, 0);
	EXPECT_FLOAT_EQ(s.floorZ, 21000 - PartyLava::startMargin);
	EXPECT_FLOAT_EQ(s.wallX1, 1000 - PartyLava::wallMargin);
	EXPECT_FLOAT_EQ(s.wallY2, 5000 + PartyLava::wallMargin);
}

TEST(PartyLava, EveryMapFitsInTheLavaModels)
{
	for (const CH::MapBounds& map : CH::mapBounds)
	{
		// PVCFactory, HobbyShop and BattleMine have far geometry; the others must fit in the 50000 x 50000 models
		if (map.map == 17 || map.map == 26) continue;
		EXPECT_LE(std::abs(map.minX), CH::lavaModelHalfSize) << map.map;
		EXPECT_LE(std::abs(map.maxX), CH::lavaModelHalfSize) << map.map;
		EXPECT_LE(std::abs(map.minY), CH::lavaModelHalfSize) << map.map;
		EXPECT_LE(std::abs(map.maxY), CH::lavaModelHalfSize) << map.map;
	}
	EXPECT_FLOAT_EQ(CH::lavaFloorNodeHeight(0.0f), -20000.0f);          // the model's default: surface at 0
}

TEST(PartyLava, NoPositionNoLava)
{
	PartyLava lava;
	const auto t0 = PartyLava::Clock::now();
	lava.setPosition(1, 0, 0, 20000, t0 - 30s);                        // too old
	EXPECT_FALSE(lava.start(start(), 999, t0).active);
	EXPECT_FALSE(lava.active());
}

TEST(PartyLava, TheRisingFloorKillsOncePerStart)
{
	PartyLava lava;
	const auto t0 = PartyLava::Clock::now();
	lava.setPosition(1, 0, 0, 20000, t0);
	lava.start(start(100.0f), 999, t0);                                  // floor 19600, +100/s
	const char* reason = nullptr;
	EXPECT_FALSE(lava.reaches(1, 0, 0, 20000, reason, t0 + 3s));      // 19900
	EXPECT_TRUE(lava.reaches(1, 0, 0, 20000, reason, t0 + 5s));       // 20100
	EXPECT_FALSE(lava.reaches(1, 0, 0, 20000, reason, t0 + 6s));      // already killed
	lava.stop();
	EXPECT_FALSE(lava.reaches(1, 0, 0, 0, reason, t0 + 7s));          // stopped: no kills
	lava.start(start(100.0f), 999, t0 + 8s);
	EXPECT_TRUE(lava.reaches(1, 0, 0, 0, reason, t0 + 8s));           // new start: kills again
}

TEST(PartyLava, WallsKillOnTouchOnlyWhenOn)
{
	PartyLava lava;
	const auto t0 = PartyLava::Clock::now();
	lava.setPosition(1, 0, 0, 20000, t0);
	lava.start(start(0.0f, 20.0f, true), 999, t0);                      // walls at -2000 / +2000, centre in 20 s: 100/s
	const char* reason = nullptr;
	EXPECT_FALSE(lava.reaches(1, -1500, 0, 20000, reason, t0 + 4s));  // wall x1 at -1600
	EXPECT_TRUE(lava.reaches(1, -1500, 0, 20000, reason, t0 + 6s));   // wall x1 at -1400
	EXPECT_STREQ(reason, "wall x1");

	PartyLava noWalls;
	noWalls.setPosition(1, 0, 0, 20000, t0);
	noWalls.start(start(0.0f, 20.0f, false), 999, t0);
	EXPECT_FALSE(noWalls.reaches(1, -1900, 0, 20000, reason, t0 + 60s));
}

TEST(PartyLava, SetSpeedContinuesFromTheCurrentPosition)
{
	PartyLava lava;
	const auto t0 = PartyLava::Clock::now();
	lava.setPosition(1, 0, 0, 20000, t0);
	lava.start(start(100.0f), 999, t0);                                  // 19600
	const auto paused = lava.setSpeed({ CH::LavaAction::SetSpeed, 1, 0, 0.0f, 0.0f, 0.0f }, t0 + 2s);
	EXPECT_FLOAT_EQ(paused.floorZ, 19800.0f);
	EXPECT_FLOAT_EQ(lava.state(t0 + 60s).floorZ, 19800.0f);           // sleeping
	lava.setSpeed({ CH::LavaAction::SetSpeed, 1, 0, 0.0f, 0.0f, 2.0f }, t0 + 60s);  // hot: x2 = 200/s
	EXPECT_FLOAT_EQ(lava.state(t0 + 61s).floorZ, 20000.0f);
}
