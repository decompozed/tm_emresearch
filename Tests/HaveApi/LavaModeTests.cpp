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
#include "FakeMatch.h"
#include "HaveApi/Modes/LavaMode.h"

namespace CH = Common::HaveApi;
using namespace Main::HaveApi;
using Tests::FakeMatch;
using std::chrono::seconds;

namespace
{
	// settings: speed, grace (1 Off / 2 Normal / 3 Long), walls, hot phase, sleep phase (1 On / 2 Off)
	CH::SettingValues settings(std::int32_t grace = 2, std::int32_t walls = 1, std::int32_t hot = 1, std::int32_t sleep = 1)
	{
		return { 8, grace, walls, hot, sleep, 0 };
	}

	struct Started
	{
		FakeMatch match;
		LavaMode mode;

		Started(std::size_t players, CH::SettingValues values) : match{ CH::PartyModeId::LavaEscape, players, values }
		{
			mode.onMatchStart(match);
			mode.onRoundStart(match);
			protectedPlayers = !match.lavaCommands.empty() && match.lavaCommands.front().action == CH::LavaAction::ProtectPlayers;
			match.lavaCommands.clear();
		}

		bool protectedPlayers = false;

		void at(int second)
		{
			match.elapsed = seconds{ second };
			mode.onTick(match);
		}
	};
}

TEST(LavaMode, StartsAfterTheGracePeriod)
{
	Started lava{ 2, settings(2) };
	lava.at(4);
	EXPECT_TRUE(lava.match.lavaCommands.empty());
	lava.at(5);
	ASSERT_EQ(lava.match.lavaCommands.size(), 1u);
	const CH::LavaCommand& start = lava.match.lavaCommands.back();
	EXPECT_EQ(start.action, CH::LavaAction::Start);
	EXPECT_EQ(start.wallsOn, 1);
	EXPECT_FLOAT_EQ(start.floorSpeed, 8 * 3.0f);
	EXPECT_FLOAT_EQ(start.wallCloseSeconds, 120.0f);                  // Normal
	EXPECT_FLOAT_EQ(start.speedFactor, 1.0f);
	EXPECT_EQ(lava.match.announcements.back().first, CH::Announcement::LavaComing);

	Started off{ 2, settings(1) };
	off.at(2);
	EXPECT_TRUE(off.match.lavaCommands.empty());
	off.at(3);
	EXPECT_EQ(off.match.lavaCommands.size(), 1u);
	EXPECT_EQ(LavaMode::gracePeriod(Started{ 2, settings(3) }.match), seconds{ 10 });
}

TEST(LavaMode, RetriesTheStartWhenCastCannotPlaceIt)
{
	Started lava{ 2, settings(1) };
	lava.match.lavaWorks = false;                          // no player position yet
	lava.at(3);
	lava.match.lavaWorks = true;
	lava.at(4);
	EXPECT_EQ(lava.match.lavaCommands.size(), 2u);
	EXPECT_EQ(lava.match.announcements.size(), 1u);         // announced once, when it started
}

TEST(LavaMode, HotPhaseDoublesTheSpeed)
{
	Started lava{ 2, settings(1, 1, 1, 2) };
	lava.at(3);
	lava.at(62);
	EXPECT_EQ(lava.match.lavaCommands.size(), 1u);
	lava.at(63);
	ASSERT_EQ(lava.match.lavaCommands.size(), 2u);
	EXPECT_EQ(lava.match.lavaCommands.back().action, CH::LavaAction::SetSpeed);
	EXPECT_FLOAT_EQ(lava.match.lavaCommands.back().speedFactor, 2.0f);
	EXPECT_EQ(lava.match.announcements.back().first, CH::Announcement::LavaHotPhase);
	EXPECT_EQ(lava.match.lavaPhases.back(), std::make_pair(true, false));
}

TEST(LavaMode, SleepPhasePausesTheLava)
{
	Started lava{ 2, settings(1, 1, 2, 1) };
	lava.at(3);
	lava.at(48);
	EXPECT_FLOAT_EQ(lava.match.lavaCommands.back().speedFactor, 0.0f);
	EXPECT_EQ(lava.match.announcements.back().first, CH::Announcement::LavaStopped);
	EXPECT_EQ(lava.match.lavaPhases.back(), std::make_pair(false, true));
	lava.at(52);
	EXPECT_FLOAT_EQ(lava.match.lavaCommands.back().speedFactor, 0.0f);
	lava.at(53);
	EXPECT_FLOAT_EQ(lava.match.lavaCommands.back().speedFactor, 1.0f);
	EXPECT_EQ(lava.match.lavaPhases.back(), std::make_pair(false, false));
}

TEST(LavaMode, PhasesOffKeepTheSpeed)
{
	Started lava{ 2, settings(1, 2, 2, 2) };
	for (int second = 0; second <= 200; ++second) lava.at(second);
	EXPECT_EQ(lava.match.lavaCommands.size(), 1u);
	EXPECT_EQ(lava.match.lavaCommands.front().wallsOn, 0);
}

TEST(LavaMode, LastOneAliveWinsAndTheLavaStops)
{
	Started lava{ 3, settings() };
	lava.mode.onDeath(lava.match, lava.match.player(1), nullptr);            // lava / fall: nobody scores
	lava.mode.onDeath(lava.match, lava.match.player(2), &lava.match.player(0)); // cannot happen (Cast), scores nothing
	EXPECT_EQ(lava.match.player(0).points, 0);
	ASSERT_TRUE(lava.mode.isRoundOver(lava.match));
	lava.mode.onRoundEnd(lava.match, RoundEndReason::LastStanding);
	EXPECT_EQ(lava.match.lavaCommands.back().action, CH::LavaAction::Stop);
	EXPECT_EQ(lava.match.player(0).points, 3);
	EXPECT_EQ(lava.match.announcements.back(), std::make_pair(CH::Announcement::LavaWinner, std::string{ "P0" }));
}

TEST(LavaMode, NextRoundStartsTheLavaAgain)
{
	Started lava{ 2, settings(1) };
	lava.at(3);
	lava.mode.onRoundEnd(lava.match, RoundEndReason::TimeUp);
	lava.mode.onRoundStart(lava.match);
	lava.at(3);
	EXPECT_EQ(lava.match.lavaCommands.back().action, CH::LavaAction::Start);
	EXPECT_TRUE(lava.match.player(1).alive);
}

TEST(LavaMode, EveryoneForThemselves)
{
	Started lava{ 3, settings() };
	EXPECT_NE(lava.match.player(0).team, lava.match.player(1).team);
	EXPECT_NE(lava.match.player(1).team, lava.match.player(2).team);
	EXPECT_FALSE(lava.mode.respawnAllowed());
}

TEST(LavaMode, SinglePlayerOnlyEndsAtTheTimeLimit)
{
	Started lava{ 1, settings() };
	EXPECT_FALSE(lava.mode.isRoundOver(lava.match));
}

TEST(LavaMode, MatchWinnerHasTheMostPoints)
{
	Started lava{ 3, settings() };
	lava.match.player(1).points = 5;
	lava.mode.onMatchEnd(lava.match);
	EXPECT_EQ(lava.match.announcements.back(), std::make_pair(CH::Announcement::LavaWinner, std::string{ "P1" }));
}

TEST(LavaMode, WaitsForEveryLivingPlayerToLoad)
{
	Started lava{ 3, settings(1) };
	lava.match.player(2).loaded = false;
	lava.at(10);
	EXPECT_TRUE(lava.match.lavaCommands.empty());
	lava.match.player(2).alive = false;                    // an eliminated late joiner does not hold it back
	lava.at(11);
	EXPECT_EQ(lava.match.lavaCommands.size(), 1u);
}

TEST(LavaMode, PlayersAreProtectedFromEachOtherForTheMatch)
{
	Started lava{ 2, settings() };
	EXPECT_TRUE(lava.protectedPlayers);
}

TEST(LavaMode, SlowerSpeedsCloseTheWallsLater)
{
	Started slow{ 2, { 5, 1, 1, 2, 2, 0 } };
	slow.at(3);
	EXPECT_FLOAT_EQ(slow.match.lavaCommands.back().wallCloseSeconds, 120.0f * 8 / 5);
	EXPECT_FLOAT_EQ(slow.match.lavaCommands.back().floorSpeed, 15.0f);
}
