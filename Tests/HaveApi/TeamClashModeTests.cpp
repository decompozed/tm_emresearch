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
#include <array>
#include <set>
#include "FakeMatch.h"
#include "HaveApi/Modes/TeamClashMode.h"

namespace CH = Common::HaveApi;
using namespace Main::HaveApi;
using Tests::FakeMatch;

namespace
{
	// settings: round time, keep teams (1 On / 2 Off), radar phase, teams
	CH::SettingValues settings(std::int32_t keepTeams, std::int32_t teams, std::int32_t radar = 1)
	{
		return { 30, keepTeams, radar, teams, 0, 0 };
	}

	struct Started
	{
		FakeMatch match;
		TeamClashMode mode{ 1234 };

		Started(std::size_t players, CH::SettingValues values) : match{ CH::PartyModeId::TeamClash, players, values }
		{
			mode.onMatchStart(match);
			mode.onRoundStart(match);
		}

		std::array<int, 6> sizes()
		{
			std::array<int, 6> counts{};
			for (const PartyPlayer& player : match.players()) ++counts[static_cast<std::size_t>(player.fallbackTeam)];
			return counts;
		}

		// index of a player of `team` other than `except`
		std::size_t memberOf(std::int32_t team, std::size_t except = 99)
		{
			for (std::size_t i = 0; i < match.players().size(); ++i)
				if (match.player(i).fallbackTeam == team && i != except) return i;
			return 99;
		}
	};
}

TEST(TeamClashMode, TeamsAreBalancedAndWithinTheTeamCount)
{
	Started clash{ 7, settings(1, 3) };
	const auto sizes = clash.sizes();
	EXPECT_EQ(sizes[0], 0);
	EXPECT_EQ(sizes[4] + sizes[5], 0);
	const auto [low, high] = std::minmax({ sizes[1], sizes[2], sizes[3] });
	EXPECT_LE(high - low, 1);
	for (const PartyPlayer& player : clash.match.players()) EXPECT_EQ(player.team, player.fallbackTeam);
}

TEST(TeamClashMode, KeepTeamsOnKeepsThemOffDrawsThemAgain)
{
	Started kept{ 10, settings(1, 5) };
	std::vector<std::int32_t> before;
	for (const PartyPlayer& player : kept.match.players()) before.push_back(player.fallbackTeam);
	for (int round = 0; round < 5; ++round) kept.mode.onRoundStart(kept.match);
	for (std::size_t i = 0; i < before.size(); ++i) EXPECT_EQ(kept.match.player(i).fallbackTeam, before[i]);

	Started drawn{ 10, settings(2, 5) };
	std::set<std::vector<std::int32_t>> seen;
	for (int round = 0; round < 6; ++round)
	{
		std::vector<std::int32_t> teams;
		for (const PartyPlayer& player : drawn.match.players()) teams.push_back(player.fallbackTeam);
		seen.insert(teams);
		drawn.mode.onRoundStart(drawn.match);
	}
	EXPECT_GT(seen.size(), 1u);
}

TEST(TeamClashMode, EnemyKillScoresTeamKillDoesNot)
{
	Started clash{ 4, settings(1, 3) };
	// 4 players in 3 teams: 2 + 1 + 1. Who gets the team of two depends on the standard library's shuffle (MSVC and
	// libstdc++ differ with the same seed), so `a` is the first player that has a mate.
	std::size_t a = 0;
	while (a < 4 && clash.memberOf(clash.match.player(a).fallbackTeam, a) == 99) ++a;
	ASSERT_LT(a, 4u);
	const std::size_t mate = clash.memberOf(clash.match.player(a).fallbackTeam, a);
	std::size_t enemy = 99;
	for (std::size_t i = 0; i < 4; ++i)
		if (clash.match.player(i).fallbackTeam != clash.match.player(a).fallbackTeam) { enemy = i; break; }
	ASSERT_NE(enemy, 99u);

	clash.mode.onDeath(clash.match, clash.match.player(mate), &clash.match.player(a));
	EXPECT_EQ(clash.match.player(a).points, 0);
	EXPECT_EQ(clash.match.player(mate).team, 0);          // eliminated
	EXPECT_NE(clash.match.player(mate).fallbackTeam, 0);  // keeps its team for the DLL

	clash.mode.onDeath(clash.match, clash.match.player(enemy), &clash.match.player(a));
	EXPECT_EQ(clash.match.player(a).points, 1);
}

TEST(TeamClashMode, WipedOutTeamIsAnnouncedAndLastTeamWins)
{
	Started clash{ 3, settings(1, 3) };                    // one player per team
	PartyPlayer& a = clash.match.player(0);
	PartyPlayer& b = clash.match.player(1);
	PartyPlayer& c = clash.match.player(2);
	clash.mode.onDeath(clash.match, b, &a);
	ASSERT_FALSE(clash.match.announcements.empty());
	EXPECT_EQ(clash.match.announcements.back().first, CH::teamEliminated(b.fallbackTeam));
	EXPECT_FALSE(clash.mode.isRoundOver(clash.match));

	const auto announced = clash.match.announcements.size();
	clash.mode.onDeath(clash.match, c, &a);
	EXPECT_EQ(clash.match.announcements.size(), announced); // the winner is announced instead
	ASSERT_TRUE(clash.mode.isRoundOver(clash.match));
	clash.mode.onRoundEnd(clash.match, RoundEndReason::LastStanding);
	EXPECT_EQ(clash.match.announcements.back().first, CH::teamWon(a.fallbackTeam));
	EXPECT_EQ(a.points, 2 + 3);
}

TEST(TeamClashMode, EveryMemberOfTheWinningTeamGetsTheBonus)
{
	Started clash{ 4, settings(1, 3) };                    // teams of 2, 1, 1
	std::size_t pair = 99;
	for (std::int32_t team = 1; team <= 3; ++team)
		if (clash.sizes()[static_cast<std::size_t>(team)] == 2) pair = clash.memberOf(team);
	ASSERT_NE(pair, 99u);
	const std::int32_t winners = clash.match.player(pair).fallbackTeam;
	const std::size_t mate = clash.memberOf(winners, pair);
	for (std::size_t i = 0; i < 4; ++i)
		if (clash.match.player(i).fallbackTeam != winners) clash.mode.onDeath(clash.match, clash.match.player(i), nullptr);
	clash.mode.onDeath(clash.match, clash.match.player(mate), nullptr); // a dead member still shares the win
	ASSERT_TRUE(clash.mode.isRoundOver(clash.match));
	clash.mode.onRoundEnd(clash.match, RoundEndReason::LastStanding);
	EXPECT_EQ(clash.match.player(pair).points, 3);
	EXPECT_EQ(clash.match.player(mate).points, 3);
}

TEST(TeamClashMode, OneTeamOnlyEndsAtTheTimeLimit)
{
	Started clash{ 1, settings(1, 3) };
	EXPECT_FALSE(clash.mode.isRoundOver(clash.match));
}

TEST(TeamClashMode, RoundStartAnnouncementMatchesTheTeamCount)
{
	Started four{ 2, settings(1, 4) };
	EXPECT_EQ(four.mode.roundStartAnnouncement(four.match)->first, CH::Announcement::TeamClashStart4);
}

TEST(TeamClashMode, LateJoinerGoesToTheSmallestTeam)
{
	Started clash{ 5, settings(1, 3) };                    // 2, 2, 1
	std::int32_t smallest = 1;
	for (std::int32_t team = 1; team <= 3; ++team)
		if (clash.sizes()[static_cast<std::size_t>(team)] == 1) smallest = team;
	EXPECT_EQ(clash.mode.teamForLateJoiner(clash.match), smallest);
}

TEST(TeamClashMode, MatchWinnerIsTheBestTeamWithKeptTeamsTheBestPlayerOtherwise)
{
	Started kept{ 3, settings(1, 3) };
	kept.match.player(1).points = 4;
	kept.mode.onMatchEnd(kept.match);
	EXPECT_EQ(kept.match.announcements.back().first, CH::teamWon(kept.match.player(1).fallbackTeam));

	Started drawn{ 3, settings(2, 3) };
	drawn.match.player(2).points = 4;
	drawn.mode.onMatchEnd(drawn.match);
	EXPECT_EQ(drawn.match.announcements.back(), std::make_pair(CH::Announcement::ArenaWinner, std::string{ "P2" }));
}

TEST(TeamClashMode, RadarAsArena)
{
	Started on{ 2, settings(1, 3, 1) };
	on.match.elapsed = std::chrono::seconds{ 60 };
	on.mode.onTick(on.match);
	EXPECT_EQ(on.match.radar.back(), true);

	Started off{ 2, settings(1, 3, 2) };
	off.match.elapsed = std::chrono::seconds{ 100 };
	off.mode.onTick(off.match);
	EXPECT_EQ(off.match.radar, std::vector<bool>{ false });
}

TEST(TeamClashMode, KeptTeamsAreDrawnAgainWhenOnlyOneTeamIsLeft)
{
	Started clash{ 2, settings(1, 3) };
	clash.match.player(1).fallbackTeam = clash.match.player(0).fallbackTeam; // the other teams left the match
	clash.mode.onRoundStart(clash.match);
	EXPECT_NE(clash.match.player(0).fallbackTeam, clash.match.player(1).fallbackTeam);
}
