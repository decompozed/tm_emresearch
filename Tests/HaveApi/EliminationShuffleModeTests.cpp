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
#include "FakeMatch.h"
#include "HaveApi/Modes/EliminationShuffleMode.h"

namespace CH = Common::HaveApi;
using namespace Main::HaveApi;
using Tests::FakeMatch;

namespace
{
	// settings: round time (tenths of a minute), rounds
	struct Started
	{
		FakeMatch match;
		EliminationShuffleMode mode{ 99 };

		Started(std::size_t players, std::int32_t rounds) : match{ CH::PartyModeId::EliminationShuffle, players, { 20, rounds, 0, 0, 0, 0 } }
		{
			mode.onMatchStart(match);
			mode.onRoundStart(match);
		}

		void killTeam(std::int32_t team)
		{
			for (PartyPlayer& player : match.players())
			{
				if (player.fallbackTeam == team && player.alive)
				{
					mode.onDeath(match, player, nullptr);
				}
			}
		}
	};

	std::int32_t membersOf(FakeMatch& match, std::int32_t team)
	{
		return static_cast<std::int32_t>(std::ranges::count(match.players(), team, &PartyPlayer::fallbackTeam));
	}
}

TEST(EliminationShuffleMode, TeamsAreRedAndBlueAndBalanced)
{
	Started started{ 5, 5 };
	EXPECT_EQ(membersOf(started.match, EliminationShuffleMode::red) + membersOf(started.match, EliminationShuffleMode::blue), 5);
	EXPECT_LE(std::abs(membersOf(started.match, EliminationShuffleMode::red) - membersOf(started.match, EliminationShuffleMode::blue)), 1);
	EXPECT_TRUE(started.mode.changesMapEveryRound());
	EXPECT_FALSE(started.mode.showsRounds()); // no "Round N" banner
}

TEST(EliminationShuffleMode, WipedOutTeamLosesTheRound)
{
	Started started{ 4, 5 };
	started.killTeam(EliminationShuffleMode::blue);
	ASSERT_TRUE(started.mode.isRoundOver(started.match));
	started.mode.onRoundEnd(started.match, RoundEndReason::LastStanding);
	EXPECT_EQ(started.mode.roundsWon(EliminationShuffleMode::red), 1);
	EXPECT_EQ(started.mode.roundsWon(EliminationShuffleMode::blue), 0);
	ASSERT_FALSE(started.match.teamRounds.empty());
	EXPECT_EQ(started.match.teamRounds.back().red, 1);
	EXPECT_EQ(started.match.teamRounds.back().limit, 5);
}

TEST(EliminationShuffleMode, TimeUpIsADrawEvenWithMorePlayersAlive)
{
	Started started{ 4, 5 };
	const auto victim = std::ranges::find(started.match.players(), EliminationShuffleMode::red, &PartyPlayer::fallbackTeam);
	started.mode.onDeath(started.match, *victim, nullptr);
	started.mode.onRoundEnd(started.match, RoundEndReason::TimeUp);
	EXPECT_EQ(started.mode.roundsWon(EliminationShuffleMode::blue), 0);
	EXPECT_EQ(started.mode.roundsWon(EliminationShuffleMode::red), 0);
	ASSERT_FALSE(started.match.announcements.empty());
	EXPECT_EQ(started.match.announcements.back().first, CH::Announcement::TeamClashTimeUp);
}

TEST(EliminationShuffleMode, TimeUpWithAsManyPlayersAliveHasNoWinner)
{
	Started started{ 4, 5 };
	started.mode.onRoundEnd(started.match, RoundEndReason::TimeUp);
	EXPECT_EQ(started.mode.roundsWon(EliminationShuffleMode::red), 0);
	EXPECT_EQ(started.mode.roundsWon(EliminationShuffleMode::blue), 0);
}

TEST(EliminationShuffleMode, MatchEndsWhenATeamWonTheRoundsOfTheSetting)
{
	Started started{ 2, 3 };
	for (int round = 0; round < 3; ++round)
	{
		EXPECT_FALSE(started.mode.isMatchDecided(started.match)); // 2 wins of 3 is not enough (user)
		started.killTeam(EliminationShuffleMode::blue);
		started.mode.onRoundEnd(started.match, RoundEndReason::LastStanding);
		started.mode.onRoundStart(started.match);
	}
	EXPECT_TRUE(started.mode.isMatchDecided(started.match)); // 3 wins
	EXPECT_GT(started.mode.roundsPerMatch(3), 3u);           // draws do not count: more rounds than the setting
}

TEST(EliminationShuffleMode, RadarInTheLastMinuteForEverybody)
{
	Started started{ 4, 5 };
	started.match.elapsed = std::chrono::seconds{ 30 };
	started.mode.onTick(started.match);
	EXPECT_TRUE(started.match.radar.empty());
	started.match.elapsed = std::chrono::seconds{ 60 }; // FakeMatch round: 120 s
	started.mode.onTick(started.match);
	ASSERT_EQ(started.match.radar.size(), 1u);
	EXPECT_TRUE(started.match.radar.back());
	started.mode.onRoundEnd(started.match, RoundEndReason::TimeUp);
	EXPECT_FALSE(started.match.radar.back());
}

TEST(EliminationShuffleMode, LateJoinerGoesToTheSmallerTeam)
{
	Started started{ 3, 5 };
	const std::int32_t smaller = membersOf(started.match, EliminationShuffleMode::red) < membersOf(started.match, EliminationShuffleMode::blue)
		? EliminationShuffleMode::red : EliminationShuffleMode::blue;
	EXPECT_EQ(started.mode.teamForLateJoiner(started.match), smaller);
}
