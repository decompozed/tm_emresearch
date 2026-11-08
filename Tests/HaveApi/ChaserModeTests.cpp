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
#include <set>
#include "FakeMatch.h"
#include "HaveApi/Modes/ChaserMode.h"

namespace CH = Common::HaveApi;
using namespace Main::HaveApi;
using Tests::FakeMatch;

namespace
{
	struct Started
	{
		FakeMatch match;
		ChaserMode mode{ 7 };

		explicit Started(std::size_t players) : match{ CH::PartyModeId::Chaser, players, { 20, 0, 0, 0, 0, 0 } }
		{
			mode.onMatchStart(match);
			mode.onRoundStart(match);
		}

		PartyPlayer& byId(std::int32_t id) { return *std::ranges::find(match.players(), id, &PartyPlayer::playerId); }
		PartyPlayer& chaser() { return byId(mode.chaser()); }

		PartyPlayer& runner(bool target, std::size_t skip = 0)
		{
			for (PartyPlayer& player : match.players())
			{
				const bool isTarget = std::ranges::find(mode.targets(), player.playerId) != mode.targets().end();
				if (player.playerId != mode.chaser() && isTarget == target && skip-- == 0) return player;
			}
			ADD_FAILURE() << "no such runner";
			return match.player(0);
		}

		void killAllRunners()
		{
			for (PartyPlayer& player : match.players())
				if (player.playerId != mode.chaser() && player.alive) mode.onDeath(match, player, &chaser());
		}
	};
}

TEST(ChaserMode, OneChaserAndOneTargetInThree)
{
	Started chase{ 7 };                           // 6 runners -> 2 targets
	EXPECT_NE(chase.mode.chaser(), 0);
	EXPECT_EQ(chase.match.chaser, chase.mode.targets().front()); // 0x1A = the marked target
	EXPECT_EQ(chase.match.marked.back(), std::make_pair(chase.mode.chaser(), 0));
	EXPECT_EQ(chase.mode.targets().size(), 2u);
	EXPECT_EQ(chase.match.targetStates, (std::vector<std::int8_t>{ 1, 1 }));
	for (const std::int32_t id : chase.mode.targets()) EXPECT_EQ(chase.byId(id).team, ChaserMode::runnerTeam);
	EXPECT_EQ(chase.chaser().team, ChaserMode::chaserTeam);
	EXPECT_EQ(std::ranges::count(chase.match.players(), ChaserMode::chaserTeam, &PartyPlayer::team), 1);
	EXPECT_EQ(chase.mode.roundStartAnnouncement(chase.match)->first, CH::Announcement::ChaserChosen);
	EXPECT_FALSE(chase.mode.respawnAllowed());
}

TEST(ChaserMode, TwoPlayersHaveOneTarget)
{
	Started chase{ 2 };
	EXPECT_EQ(chase.mode.targets().size(), 1u);
}

TEST(ChaserMode, EveryPlayerIsTheChaserOnceBeforeAnyoneTwice)
{
	Started chase{ 4 };
	std::set<std::int32_t> chasers{ chase.mode.chaser() };
	for (int round = 0; round < 3; ++round)
	{
		chase.mode.onRoundStart(chase.match);
		chasers.insert(chase.mode.chaser());
	}
	EXPECT_EQ(chasers.size(), 4u);
}

TEST(ChaserMode, TargetKillIsWorthTheBonus)
{
	Started chase{ 4 };                           // 3 runners -> 1 target
	chase.mode.onDeath(chase.match, chase.runner(false), &chase.chaser());
	EXPECT_EQ(chase.chaser().points, 1);
	chase.mode.onDeath(chase.match, chase.runner(true), &chase.chaser());
	EXPECT_EQ(chase.chaser().points, 1 + 1 + 2);
	EXPECT_EQ(chase.match.targetStates, (std::vector<std::int8_t>{ 2 }));
}

TEST(ChaserMode, AllRunnersEliminatedTheChaserWins)
{
	Started chase{ 3 };
	chase.killAllRunners();
	ASSERT_TRUE(chase.mode.isRoundOver(chase.match));
	const auto before = chase.chaser().points;
	chase.mode.onRoundEnd(chase.match, RoundEndReason::LastStanding);
	EXPECT_EQ(chase.chaser().points, before + 3);
	EXPECT_EQ(chase.match.announcements.back().first, CH::Announcement::ChaserAllEliminated);
}

TEST(ChaserMode, ChaserKilledTheSurvivorsWin)
{
	Started chase{ 4 };
	PartyPlayer& dead = chase.runner(false);
	chase.mode.onDeath(chase.match, dead, &chase.chaser());
	PartyPlayer& hero = chase.runner(true);
	chase.mode.onDeath(chase.match, chase.chaser(), &hero);
	EXPECT_EQ(hero.points, 1);
	ASSERT_TRUE(chase.mode.isRoundOver(chase.match));
	chase.mode.onRoundEnd(chase.match, RoundEndReason::LastStanding);
	EXPECT_EQ(chase.match.announcements.back().first, CH::Announcement::ChaserEliminated);
	EXPECT_EQ(hero.points, 1 + 3);
	EXPECT_EQ(dead.points, 0);                    // eliminated runners get nothing
}

TEST(ChaserMode, TimeUpTheSurvivorsWin)
{
	Started chase{ 3 };
	chase.mode.onRoundEnd(chase.match, RoundEndReason::TimeUp);
	EXPECT_EQ(chase.match.announcements.back().first, CH::Announcement::ChaserTimeUp);
	EXPECT_EQ(chase.chaser().points, 0);
	EXPECT_EQ(chase.runner(false).points + chase.runner(true).points, 6);
}

TEST(ChaserMode, ChaserDyingAloneEndsTheRound)
{
	Started chase{ 3 };
	chase.mode.onDeath(chase.match, chase.chaser(), nullptr); // fall / own rocket
	EXPECT_TRUE(chase.mode.isRoundOver(chase.match));
}

TEST(ChaserMode, RunnerKillingARunnerScoresNothing)
{
	Started chase{ 4 };
	PartyPlayer& a = chase.runner(false, 0);
	PartyPlayer& b = chase.runner(false, 1);
	chase.mode.onDeath(chase.match, b, &a);
	EXPECT_EQ(a.points, 0);
	EXPECT_FALSE(b.alive);
}

TEST(ChaserMode, LeavingTargetIsRemovedFromTheMarkers)
{
	Started chase{ 4 };
	PartyPlayer& target = chase.runner(true);
	chase.mode.onPlayerLeft(chase.match, target);
	EXPECT_TRUE(chase.mode.targets().empty());
	EXPECT_TRUE(chase.match.targetStates.empty());
}

TEST(ChaserMode, LeavingChaserEndsTheRoundForTheRunners)
{
	Started chase{ 3 };
	const std::int32_t gone = chase.mode.chaser();
	FakeMatch rest{ CH::PartyModeId::Chaser, 0, { 20, 0, 0, 0, 0, 0 } };
	for (PartyPlayer& player : chase.match.players())
		if (player.playerId != gone) rest.addPlayer(player);   // the match removes the leaving player
	chase.mode.onPlayerLeft(rest, chase.chaser());
	ASSERT_TRUE(chase.mode.isRoundOver(rest));
	chase.mode.onRoundEnd(rest, RoundEndReason::LastStanding);
	EXPECT_EQ(rest.announcements.back().first, CH::Announcement::ChaserEliminated);
	EXPECT_EQ(rest.player(0).points, 3);
	EXPECT_EQ(rest.player(1).points, 3);
}

TEST(ChaserMode, LateJoinerIsARunner)
{
	Started chase{ 2 };
	EXPECT_EQ(chase.mode.teamForLateJoiner(chase.match), ChaserMode::runnerTeam);
}

TEST(ChaserMode, SinglePlayerRoundOnlyEndsAtTheTimeLimit)
{
	Started chase{ 1 };
	EXPECT_FALSE(chase.mode.isRoundOver(chase.match));
	EXPECT_TRUE(chase.mode.targets().empty());
}

TEST(ChaserMode, MatchWinnerHasTheMostPoints)
{
	Started chase{ 3 };
	chase.match.player(2).points = 8;
	chase.mode.onMatchEnd(chase.match);
	EXPECT_EQ(chase.match.announcements.back(), std::make_pair(CH::Announcement::ArenaWinner, std::string{ "P2" }));
}

TEST(ChaserMode, NextLivingTargetIsMarkedWhenTheMarkedOneDies)
{
	Started chase{ 7 };                           // 2 targets
	const std::int32_t first = chase.mode.targets()[0];
	const std::int32_t second = chase.mode.targets()[1];
	chase.mode.onDeath(chase.match, chase.byId(first), &chase.chaser());
	EXPECT_EQ(chase.match.chaser, second);
	EXPECT_EQ(chase.match.targetStates, (std::vector<std::int8_t>{ 2, 1 }));
	chase.mode.onDeath(chase.match, chase.byId(second), &chase.chaser());
	EXPECT_EQ(chase.match.chaser, 0);
}

TEST(ChaserMode, ChaserOfTheLastRoundIsARunnerAgain)
{
	Started chase{ 3 };
	const std::int32_t old = chase.mode.chaser();
	chase.mode.onRoundStart(chase.match);
	EXPECT_EQ(chase.byId(old).team, ChaserMode::runnerTeam);
	EXPECT_EQ(chase.chaser().team, ChaserMode::chaserTeam);
}
