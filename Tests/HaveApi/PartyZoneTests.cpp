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
#include <set>
#include <utility>
#include "Rooms/PartyZone.h"

namespace CH = Common::HaveApi;
using Cast::Domain::PartyLava;
using Cast::Domain::PartyZone;
using namespace std::chrono_literals;

namespace
{
	CH::ZoneCommand zoneAt(float x, float y, std::initializer_list<std::pair<std::uint32_t, std::int32_t>> members)
	{
		CH::ZoneCommand c{};
		c.action = CH::ZoneAction::Set;
		c.x = x; c.y = y; c.radius = 900; c.keepOut = 3000;
		for (const auto& [session, team] : members) c.members[c.count++] = { session, team };
		return c;
	}
}

TEST(PartyZone, OccupantsAreThePlayersInsideTheCircle)
{
	PartyLava positions;
	PartyZone zone;
	const auto t0 = PartyLava::Clock::now();
	zone.set(zoneAt(0, 0, { { 1, 1 }, { 2, 2 }, { 3, 1 } }));
	positions.setPosition(1, 100, 100, 9999, t0);   // inside (z does not count)
	positions.setPosition(2, 1000, 0, 0, t0);       // outside
	positions.setPosition(3, 0, 0, 0, t0 - 10s);    // stale
	const auto answer = zone.occupants(positions, t0);
	ASSERT_EQ(answer.count, 1);
	EXPECT_EQ(answer.sessions[0], 1u);
}

TEST(PartyZone, ARespawnInTheZoneIsNotAnOccupantDuringTheSpawnGrace)
{
	PartyLava positions;
	PartyZone zone;
	const auto t0 = PartyZone::Clock::now();
	zone.set(zoneAt(0, 0, { { 1, 1 }, { 2, 2 } }));
	zone.onRespawn(1, 100, 0, t0);                      // the client respawned it in the zone: not moved, only noted
	CH::ZoneAnswer changed{};
	EXPECT_TRUE(zone.inSpawnGrace(1, t0 + 1s));
	EXPECT_FALSE(zone.updateOccupant(1, 100, 0, 0, changed, t0 + 1s)); // inside, but in grace: not an occupant
	positions.setPosition(1, 100, 0, 0, t0 + 1s);
	EXPECT_EQ(zone.occupants(positions, t0 + 1s).count, 0);
	EXPECT_FALSE(zone.inSpawnGrace(1, t0 + PartyZone::spawnGrace));
	EXPECT_TRUE(zone.updateOccupant(1, 100, 0, 0, changed, t0 + PartyZone::spawnGrace)); // grace over: counts
	EXPECT_EQ(changed.count, 1);
	positions.setPosition(1, 100, 0, 0, t0 + PartyZone::spawnGrace);
	EXPECT_EQ(zone.occupants(positions, t0 + PartyZone::spawnGrace).count, 1);
}

TEST(PartyZone, ARespawnOutOfTheKeepOutHasNoGrace)
{
	PartyZone zone;
	const auto t0 = PartyZone::Clock::now();
	zone.set(zoneAt(0, 0, { { 1, 1 } }));
	zone.onRespawn(1, 4000, 0, t0);                     // far from the zone
	EXPECT_FALSE(zone.inSpawnGrace(1, t0));
	CH::ZoneAnswer changed{};
	EXPECT_TRUE(zone.updateOccupant(1, 0, 0, 0, changed, t0 + 1s)); // ran in: counts at once
}

TEST(PartyZone, AZoneMovingOntoAFreshSpawnGivesTheGraceToo)
{
	PartyZone zone;
	const auto t0 = PartyZone::Clock::now();
	zone.onRespawn(1, 5000, 0, t0);                     // no zone yet
	EXPECT_FALSE(zone.inSpawnGrace(1, t0));
	zone.set(zoneAt(5000, 0, { { 1, 1 } }));             // the next zone comes online on that spawn
	EXPECT_TRUE(zone.inSpawnGrace(1, t0 + 1s));
	zone.clear();
	EXPECT_FALSE(zone.inSpawnGrace(1, t0 + 1s));
}

// A player joining the running match (D92: late joiners could not capture in Domination, whose zones are placed once)
TEST(PartyZone, ALateJoinerAddedToThePlacedZoneCounts)
{
	PartyZone zone;
	zone.set(zoneAt(0, 0, { { 1, 1 } }));
	CH::ZoneAnswer changed{};
	EXPECT_TRUE(zone.updateOccupant(1, 100, 0, 0, changed));  // a player of the placement inside
	EXPECT_FALSE(zone.updateOccupant(7, 0, 0, 0, changed));   // the late joiner: not counted yet
	CH::ZoneCommand join{};
	join.action = CH::ZoneAction::AddMembers;
	join.count = 1;
	join.members[0] = { 7, 2 };
	zone.addMembers(join);
	EXPECT_TRUE(zone.updateOccupant(7, 0, 0, 0, changed));    // counts now
	EXPECT_EQ(changed.count, 2);                               // the other occupant kept
	PartyZone cleared;
	cleared.addMembers(join);                                  // no zone placed: nothing
	EXPECT_FALSE(cleared.updateOccupant(7, 0, 0, 0, changed));
}

TEST(PartyZone, OccupantsAreReportedOnlyWhenTheyChange)
{
	PartyZone zone;
	zone.set(zoneAt(0, 0, { { 1, 1 }, { 2, 2 } }));
	CH::ZoneAnswer changed{};
	EXPECT_TRUE(zone.updateOccupant(1, 100, 0, 0, changed)); // entered
	EXPECT_EQ(changed.count, 1);
	EXPECT_FALSE(zone.updateOccupant(1, 200, 0, 0, changed)); // still inside: nothing to send
	EXPECT_FALSE(zone.updateOccupant(2, 5000, 0, 0, changed)); // outside, was outside
	EXPECT_TRUE(zone.updateOccupant(1, 5000, 0, 0, changed)); // left
	EXPECT_EQ(changed.count, 0);
	EXPECT_FALSE(zone.updateOccupant(9, 0, 0, 0, changed));   // not a player of the match
}

TEST(PartyZone, ASquareZoneCountsBothAxesAndItsHeightBand)
{
	CH::ZoneCommand c = zoneAt(0, 0, { { 1, 1 } });
	c.shape = CH::ZoneShape::Square;
	c.index = 2;
	c.radius = 500.0f; // half side
	c.zMin = -100.0f;
	c.zMax = 400.0f;
	PartyZone zone{ 2 };
	zone.set(c);
	CH::ZoneAnswer changed{};
	EXPECT_TRUE(zone.updateOccupant(1, 480, -480, 0, changed)); // a corner of the square: inside (a circle would not be)
	EXPECT_EQ(changed.index, 2);
	EXPECT_EQ(changed.count, 1);
	EXPECT_TRUE(zone.updateOccupant(1, 480, -480, 900, changed)); // above the band (a bridge): out
	EXPECT_EQ(changed.count, 0);
	EXPECT_FALSE(zone.updateOccupant(1, 520, 0, 0, changed)); // past the side: still out
}

TEST(PartyZone, ACircleIgnoresTheHeightWithoutABand)
{
	PartyZone zone;
	zone.set(zoneAt(0, 0, { { 1, 1 } }));
	CH::ZoneAnswer changed{};
	EXPECT_TRUE(zone.updateOccupant(1, 100, 0, 5000, changed));
	EXPECT_EQ(changed.index, 0);
}

