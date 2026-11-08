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
#include "Rooms/PartyStorm.h"

namespace CH = Common::HaveApi;
using Cast::Domain::PartyLava;
using Cast::Domain::PartyStorm;
using namespace std::chrono_literals;

namespace
{
	CH::StormCommand command(std::initializer_list<CH::StormBox> boxes, std::initializer_list<std::uint32_t> members)
	{
		CH::StormCommand c{};
		c.action = CH::StormAction::Set;
		for (const auto& b : boxes)
		{
			c.boxes[c.boxCount++] = b;
		}
		for (const auto m : members)
		{
			c.members[c.memberCount++] = m;
		}
		return c;
	}

	CH::StormBox still(float x1, float x2, float y1, float y2)
	{
		return { x1, 0, x2, 0, y1, 0, y2, 0, 0 };
	}
}

TEST(PartyStorm, TheSafeAreaIsTheUnionOfTheBoxes)
{
	PartyStorm storm;
	const auto t0 = PartyStorm::Clock::now();
	storm.set(command({ still(0, 100, 0, 100), still(100, 200, 40, 60) }, { 1 }), t0);
	EXPECT_TRUE(storm.contains(50, 50, t0));
	EXPECT_TRUE(storm.contains(150, 50, t0));
	EXPECT_FALSE(storm.contains(150, 80, t0));
	EXPECT_FALSE(storm.contains(-1, 50, t0));
	storm.set(command({ still(0, 0, 0, 100) }, { 1 }), t0);
	EXPECT_FALSE(storm.contains(0, 50, t0));
}

TEST(PartyStorm, SidesMoveAtTheirSpeedForTheirTimeThenStay)
{
	PartyStorm storm;
	const auto t0 = PartyStorm::Clock::now();
	storm.set(command({ { 0, 10, 1000, 0, 0, 0, 100, 0, 50 } }, { 1 }), t0);
	EXPECT_TRUE(storm.contains(150, 50, t0 + 10s));
	EXPECT_FALSE(storm.contains(99, 50, t0 + 10s));
	EXPECT_TRUE(storm.contains(510, 50, t0 + 80s));
	EXPECT_FALSE(storm.contains(490, 50, t0 + 80s));
}

TEST(PartyStorm, OnlyTheMembersWithAPositionAreCountedOutside)
{
	PartyLava positions;
	PartyStorm storm;
	const auto t0 = PartyStorm::Clock::now();
	positions.setPosition(1, 50, 50, 0, t0);
	positions.setPosition(2, 500, 50, 0, t0);
	positions.setPosition(3, 500, 50, 0, t0);
	storm.set(command({ still(0, 100, 0, 100) }, { 1, 2, 4 }), t0);
	const auto answer = storm.evaluate(positions, t0);
	EXPECT_EQ(answer.done, 1);
	ASSERT_EQ(answer.count, 1);
	EXPECT_EQ(answer.sessions[0], 2u);
}

TEST(PartyStorm, APositionChangesTheSetOnlyWhenItCrossesTheBoundary)
{
	PartyLava positions;
	PartyStorm storm;
	const auto t0 = PartyStorm::Clock::now();
	storm.set(command({ still(0, 100, 0, 100) }, { 1, 2 }), t0);
	CH::StormAnswer changed{};
	EXPECT_FALSE(storm.update(1, 50, 50, changed, t0));
	EXPECT_TRUE(storm.update(1, 150, 50, changed, t0));
	EXPECT_EQ(changed.count, 1);
	EXPECT_FALSE(storm.update(1, 160, 50, changed, t0));
	EXPECT_FALSE(storm.update(9, 160, 50, changed, t0));
	EXPECT_TRUE(storm.update(1, 60, 50, changed, t0));
	EXPECT_EQ(changed.count, 0);
	storm.clear();
	EXPECT_FALSE(storm.update(1, 160, 50, changed, t0));
	EXPECT_FALSE(storm.active());
}

TEST(PartyStorm, AMovingStormCatchesAPlayerWhoStandsStill)
{
	PartyLava positions;
	PartyStorm storm;
	const auto t0 = PartyStorm::Clock::now();
	positions.setPosition(1, 80, 50, 0, t0);
	storm.set(command({ { 0, 0, 100, -10, 0, 0, 100, 0, 10 } }, { 1 }), t0);
	EXPECT_EQ(storm.evaluate(positions, t0 + 1s).count, 0);
	EXPECT_EQ(storm.evaluate(positions, t0 + 3s).count, 1);
}

TEST(PartyStorm, AMissingPositionIsReportedOncePerStorm)
{
	PartyLava positions;
	PartyStorm storm;
	const auto t0 = PartyStorm::Clock::now();
	positions.setPosition(1, 50, 50, 0, t0);
	storm.set(command({ still(0, 100, 0, 100) }, { 1, 2 }), t0);
	const auto first = storm.takeStale(positions, t0 + 1s);
	ASSERT_EQ(first.size(), 1u);
	EXPECT_EQ(first[0], 2u);
	EXPECT_TRUE(storm.takeStale(positions, t0 + 2s).empty());
	const auto later = storm.takeStale(positions, t0 + 12s);
	ASSERT_EQ(later.size(), 1u);
	EXPECT_EQ(later[0], 1u);
}
