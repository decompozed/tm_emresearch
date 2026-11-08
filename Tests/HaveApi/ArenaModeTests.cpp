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
#include <string>
#include <utility>
#include <vector>
#include "FakeMatch.h"
#include "HaveApi/Modes/ArenaMode.h"

namespace CH = Common::HaveApi;
using namespace Main::HaveApi;

namespace
{
	using Tests::FakeMatch;

	struct Started
	{
		FakeMatch match;
		ArenaMode mode;

		explicit Started(std::size_t players, CH::SettingValues values = CH::defaultSettings(CH::PartyModeId::Arena))
			: match{ CH::PartyModeId::Arena, players, values }
		{
			mode.onMatchStart(match);
			mode.onRoundStart(match);
		}
	};
}

TEST(ArenaMode, EveryPlayerHasItsOwnTeam)
{
	Started arena{ 3 };
	EXPECT_EQ(arena.match.player(0).team, 1);
	EXPECT_EQ(arena.match.player(1).team, 2);
	EXPECT_EQ(arena.match.player(2).team, 3);
	EXPECT_FALSE(arena.mode.respawnAllowed());
}

TEST(ArenaMode, KillEliminatesTheVictimAndScoresTheKiller)
{
	Started arena{ 3 };
	arena.mode.onDeath(arena.match, arena.match.player(1), &arena.match.player(0));
	EXPECT_FALSE(arena.match.player(1).alive);
	EXPECT_EQ(arena.match.player(1).team, 0);          // the DLL counts the living players by team != 0
	EXPECT_EQ(arena.match.updates.back(), std::make_pair(101, 0));
	EXPECT_EQ(arena.match.player(0).points, 1);
	EXPECT_EQ(arena.match.points.back(), std::make_pair(100, 1));
	EXPECT_FALSE(arena.mode.isRoundOver(arena.match)); // two players still alive
}

TEST(ArenaMode, FallGivesNoPoints)
{
	Started arena{ 2 };
	arena.mode.onDeath(arena.match, arena.match.player(1), nullptr);
	EXPECT_TRUE(arena.match.points.empty());
	EXPECT_TRUE(arena.mode.isRoundOver(arena.match));
}

TEST(ArenaMode, LastStandingGetsTheBonusAndTheAnnouncement)
{
	Started arena{ 3 };
	arena.mode.onDeath(arena.match, arena.match.player(1), &arena.match.player(0));
	arena.mode.onDeath(arena.match, arena.match.player(2), &arena.match.player(0));
	ASSERT_TRUE(arena.mode.isRoundOver(arena.match));
	arena.mode.onRoundEnd(arena.match, RoundEndReason::LastStanding);
	EXPECT_EQ(arena.match.player(0).points, 2 + 3);
	ASSERT_FALSE(arena.match.announcements.empty());
	EXPECT_EQ(arena.match.announcements.back(), std::make_pair(CH::Announcement::ArenaWinner, std::string{ "P0" }));
}

TEST(ArenaMode, TimeUpAnnouncesWithoutBonus)
{
	Started arena{ 2 };
	arena.mode.onRoundEnd(arena.match, RoundEndReason::TimeUp);
	EXPECT_EQ(arena.match.announcements.back().first, CH::Announcement::ArenaTimeUp);
	EXPECT_EQ(arena.match.player(0).points, 0);
}

TEST(ArenaMode, NewRoundRevivesEveryone)
{
	Started arena{ 2 };
	arena.mode.onDeath(arena.match, arena.match.player(1), &arena.match.player(0));
	arena.mode.onRoundStart(arena.match);
	EXPECT_TRUE(arena.match.player(1).alive);
	EXPECT_EQ(arena.match.player(1).team, 2);
}

TEST(ArenaMode, SinglePlayerRoundOnlyEndsAtTheTimeLimit)
{
	Started arena{ 1 };
	EXPECT_FALSE(arena.mode.isRoundOver(arena.match));
}

TEST(ArenaMode, HealWinnerHealsTheSurvivorsAtTheNextRoundOnlyWhenOn)
{
	Started off{ 2 };                                  // default: heal winner Off (2)
	off.mode.onDeath(off.match, off.match.player(1), &off.match.player(0));
	EXPECT_TRUE(off.match.healed.empty());             // no heal for a kill
	EXPECT_FALSE(off.mode.healSurvivorsAtRoundStart(off.match));

	Started on{ 2, { 20, 1, 1, 0, 0, 0 } };
	on.mode.onDeath(on.match, on.match.player(1), &on.match.player(0));
	EXPECT_TRUE(on.match.healed.empty());
	EXPECT_TRUE(on.mode.healSurvivorsAtRoundStart(on.match));
}

TEST(ArenaMode, RadarStartsInTheSecondHalfWhenOn)
{
	Started arena{ 2 };                                // default: radar phase On (1)
	arena.match.elapsed = std::chrono::seconds{ 59 };
	arena.mode.onTick(arena.match);
	EXPECT_EQ(arena.match.radar, std::vector<bool>{ false }); // only the reset at round start
	arena.match.elapsed = std::chrono::seconds{ 60 };
	arena.mode.onTick(arena.match);
	EXPECT_EQ(arena.match.radar.back(), true);

	Started off{ 2, { 20, 2, 2, 0, 0, 0 } };
	off.match.elapsed = std::chrono::seconds{ 100 };
	off.mode.onTick(off.match);
	EXPECT_EQ(off.match.radar, std::vector<bool>{ false });
}

TEST(ArenaMode, MatchWinnerHasTheMostPoints)
{
	Started arena{ 3 };
	arena.match.player(2).points = 7;
	arena.mode.onMatchEnd(arena.match);
	EXPECT_EQ(arena.match.announcements.back(), std::make_pair(CH::Announcement::ArenaWinner, std::string{ "P2" }));
}
