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
#include "HaveApi/Modes/ChickenRunMode.h"

namespace CH = Common::HaveApi;
using namespace Main::HaveApi;
using Tests::FakeMatch;

namespace
{
	// settings: round time, explosives (1 On / 2 Off), bonus (1 On / 2 Off)
	CH::SettingValues settings(std::int32_t explosives, std::int32_t bonus) { return { 20, explosives, bonus, 0, 0, 0 }; }

	struct Started
	{
		FakeMatch match;
		ChickenRunMode mode{ 99 };

		Started(std::size_t players, CH::SettingValues values) : match{ CH::PartyModeId::ChickenRun, players, values }
		{
			mode.onMatchStart(match);
			mode.onRoundStart(match);
		}

		PartyPlayer& chicken()
		{
			return *std::ranges::find(match.players(), mode.chicken(), &PartyPlayer::playerId);
		}

		PartyPlayer& other(std::size_t skip = 0)
		{
			for (PartyPlayer& player : match.players())
				if (player.playerId != mode.chicken() && skip-- == 0) return player;
			return match.player(0);
		}
	};
}

TEST(ChickenRunMode, OneRandomChickenPerRound)
{
	Started run{ 4, settings(2, 1) };
	EXPECT_EQ(std::ranges::count(run.match.players(), ChickenRunMode::chickenTeam, &PartyPlayer::team), 1);
	EXPECT_EQ(run.chicken().team, ChickenRunMode::chickenTeam);
	EXPECT_EQ(run.match.marked.back(), std::make_pair(run.mode.chicken(), 0));
	EXPECT_EQ(run.mode.roundStartAnnouncement(run.match)->second, run.chicken().name);
	EXPECT_TRUE(run.mode.respawnAllowed());
}

TEST(ChickenRunMode, ChickenHitPassesTheRole)
{
	Started run{ 3, settings(2, 1) };
	PartyPlayer& oldChicken = run.chicken();
	PartyPlayer& target = run.other();
	run.mode.onHit(run.match, target, oldChicken, CH::DeathCause::Weapon);
	EXPECT_EQ(run.mode.chicken(), target.playerId);
	EXPECT_EQ(target.team, ChickenRunMode::chickenTeam);
	EXPECT_EQ(oldChicken.team, ChickenRunMode::playerTeam);
	EXPECT_EQ(run.match.marked.back(), std::make_pair(target.playerId, oldChicken.playerId));
	EXPECT_EQ(run.match.announcements.back(), std::make_pair(CH::Announcement::ChickenChosen, target.name));
	EXPECT_EQ(oldChicken.points, 1); // bonus On
}

TEST(ChickenRunMode, OnlyTheChickenPassesTheRole)
{
	Started run{ 3, settings(2, 1) };
	const std::int32_t before = run.mode.chicken();
	run.mode.onHit(run.match, run.chicken(), run.other(), CH::DeathCause::Weapon);
	run.mode.onHit(run.match, run.other(1), run.other(0), CH::DeathCause::Weapon);
	EXPECT_EQ(run.mode.chicken(), before);
}

TEST(ChickenRunMode, NoTagBackUntilTheDelayIsOver)
{
	Started run{ 2, settings(2, 2) };
	PartyPlayer& first = run.chicken();
	PartyPlayer& second = run.other();
	run.mode.onHit(run.match, second, first, CH::DeathCause::Weapon);
	run.mode.onHit(run.match, first, second, CH::DeathCause::Weapon); // tag back at once: refused
	EXPECT_EQ(run.mode.chicken(), second.playerId);
	EXPECT_EQ(first.points, 0);                                        // bonus Off

	run.match.elapsed = ChickenRunMode::tagBackTime;
	run.mode.onTick(run.match);
	EXPECT_EQ(run.match.marked.back(), std::make_pair(second.playerId, 0));
	run.mode.onHit(run.match, first, second, CH::DeathCause::Weapon);
	EXPECT_EQ(run.mode.chicken(), first.playerId);
}

TEST(ChickenRunMode, ExplosivesPassTheRoleOnlyWhenOn)
{
	Started off{ 2, settings(2, 1) };
	const std::int32_t before = off.mode.chicken();
	off.mode.onHit(off.match, off.other(), off.chicken(), CH::DeathCause::Explosive);
	EXPECT_EQ(off.mode.chicken(), before);

	Started on{ 2, settings(1, 1) };
	PartyPlayer& target = on.other();
	on.mode.onHit(on.match, target, on.chicken(), CH::DeathCause::Explosive);
	EXPECT_EQ(on.mode.chicken(), target.playerId);
}

TEST(ChickenRunMode, TheChickenLosesAtTheEndOfTheRound)
{
	Started run{ 3, settings(2, 1) };
	PartyPlayer& loser = run.chicken();
	run.mode.onRoundEnd(run.match, RoundEndReason::TimeUp);
	EXPECT_EQ(loser.points, 0);
	EXPECT_EQ(run.other(0).points, 3);
	EXPECT_EQ(run.other(1).points, 3);
	EXPECT_EQ(run.match.announcements.back(), std::make_pair(CH::Announcement::ChickenPlayerEliminated, loser.name));
	EXPECT_FALSE(run.mode.isRoundOver(run.match));
}

TEST(ChickenRunMode, ALeavingChickenIsReplaced)
{
	Started run{ 3, settings(2, 1) };
	PartyPlayer& leaving = run.chicken();
	run.mode.onPlayerLeft(run.match, leaving);
	EXPECT_NE(run.mode.chicken(), leaving.playerId);
	EXPECT_NE(run.mode.chicken(), 0);
	EXPECT_EQ(run.match.announcements.back().first, CH::Announcement::ChickenChosen);
}

// ---- corner cases ----

namespace
{
	std::size_t chickens(FakeMatch& match)
	{
		return static_cast<std::size_t>(std::ranges::count(match.players(), ChickenRunMode::chickenTeam, &PartyPlayer::team));
	}
}

TEST(ChickenRunMode, ChickenDyingAloneStaysTheChicken)
{
	Started run{ 3, settings(1, 1) };
	const std::int32_t before = run.mode.chicken();
	run.mode.onDeath(run.match, run.chicken(), nullptr);                           // fall / own rocket
	run.mode.onHit(run.match, run.chicken(), run.chicken(), CH::DeathCause::Explosive); // self damage
	EXPECT_EQ(run.mode.chicken(), before);
	EXPECT_TRUE(run.match.announcements.empty());
}

TEST(ChickenRunMode, ChickenKilledByAPlayerStaysTheChicken)
{
	Started run{ 3, settings(2, 1) };
	const std::int32_t before = run.mode.chicken();
	PartyPlayer& killer = run.other();
	run.mode.onHit(run.match, run.chicken(), killer, CH::DeathCause::Weapon); // the killing hit comes first
	run.mode.onDeath(run.match, run.chicken(), &killer);
	EXPECT_EQ(run.mode.chicken(), before);
	EXPECT_EQ(killer.points, 0);
}

TEST(ChickenRunMode, ChickenKillingAPlayerPassesTheRole)
{
	Started run{ 3, settings(2, 1) };
	PartyPlayer& oldChicken = run.chicken();
	PartyPlayer& victim = run.other();
	run.mode.onHit(run.match, victim, oldChicken, CH::DeathCause::Weapon);
	run.mode.onDeath(run.match, victim, &oldChicken);
	EXPECT_EQ(run.mode.chicken(), victim.playerId); // it respawns as the chicken
}

TEST(ChickenRunMode, AlwaysExactlyOneChicken)
{
	Started run{ 5, settings(1, 1) };
	for (int step = 0; step < 20; ++step)
	{
		run.match.elapsed += ChickenRunMode::tagBackTime; // every hit allowed
		run.mode.onTick(run.match);
		run.mode.onHit(run.match, run.other(static_cast<std::size_t>(step % 4)), run.chicken(), CH::DeathCause::Weapon);
		ASSERT_EQ(chickens(run.match), 1u);
	}
}

TEST(ChickenRunMode, OnlyThePreviousChickenIsProtected)
{
	Started run{ 3, settings(2, 1) };
	PartyPlayer& a = run.chicken();
	PartyPlayer& b = run.other(0);
	PartyPlayer& c = run.other(1);
	run.mode.onHit(run.match, b, a, CH::DeathCause::Weapon); // a -> b
	run.mode.onHit(run.match, c, b, CH::DeathCause::Weapon); // b -> c at once: allowed, c was not the previous chicken
	EXPECT_EQ(run.mode.chicken(), c.playerId);
	EXPECT_EQ(run.match.marked.back(), std::make_pair(c.playerId, b.playerId)); // now b is protected, a is not
	run.mode.onHit(run.match, a, c, CH::DeathCause::Weapon);
	EXPECT_EQ(run.mode.chicken(), a.playerId);
}

TEST(ChickenRunMode, NewRoundDrawsAChickenAndClearsTheProtection)
{
	Started run{ 2, settings(2, 1) };
	PartyPlayer& first = run.chicken();
	PartyPlayer& second = run.other();
	run.mode.onHit(run.match, second, first, CH::DeathCause::Weapon); // first protected for 3 s
	run.mode.onRoundStart(run.match);
	EXPECT_EQ(chickens(run.match), 1u);
	EXPECT_EQ(run.match.marked.back().second, 0);
	PartyPlayer& chicken = run.chicken();
	PartyPlayer& target = run.other();
	run.mode.onHit(run.match, target, chicken, CH::DeathCause::Weapon); // nobody protected
	EXPECT_EQ(run.mode.chicken(), target.playerId);
}

TEST(ChickenRunMode, ALeavingPlayerThatIsNotTheChickenChangesNothing)
{
	Started run{ 3, settings(2, 1) };
	const std::int32_t before = run.mode.chicken();
	run.mode.onPlayerLeft(run.match, run.other());
	EXPECT_EQ(run.mode.chicken(), before);
	EXPECT_TRUE(run.match.announcements.empty());
}

TEST(ChickenRunMode, ChickenLeavingWithOneOtherPlayerLeavesItTheRole)
{
	Started run{ 2, settings(2, 1) };
	PartyPlayer& remaining = run.other();
	run.mode.onPlayerLeft(run.match, run.chicken());
	EXPECT_EQ(run.mode.chicken(), remaining.playerId);
	EXPECT_EQ(remaining.team, ChickenRunMode::chickenTeam);
}

TEST(ChickenRunMode, ProtectedPlayerLeavingClearsTheMark)
{
	Started run{ 3, settings(2, 1) };
	PartyPlayer& first = run.chicken();
	run.mode.onHit(run.match, run.other(), first, CH::DeathCause::Weapon);
	run.mode.onPlayerLeft(run.match, first);                // not the chicken any more: nothing to replace
	run.match.elapsed = ChickenRunMode::tagBackTime;
	run.mode.onTick(run.match);
	EXPECT_EQ(run.match.marked.back().second, 0);
}

TEST(ChickenRunMode, LateJoinerIsAnOrdinaryPlayerAndCanBecomeTheChicken)
{
	Started run{ 2, settings(2, 1) };
	PartyPlayer late;
	late.playerId = 200;
	late.name = "Late";
	late.fallbackTeam = run.mode.teamForLateJoiner(run.match);
	late.team = late.fallbackTeam;
	EXPECT_EQ(late.team, ChickenRunMode::playerTeam);
	run.match.addPlayer(late);
	run.mode.onHit(run.match, run.match.player(2), run.chicken(), CH::DeathCause::Weapon);
	EXPECT_EQ(run.mode.chicken(), 200);
}

TEST(ChickenRunMode, RoundEndWithoutAChickenHasNoLoser)
{
	Started run{ 1, settings(2, 1) };
	run.mode.onPlayerLeft(run.match, run.chicken());         // nobody left to take the role
	EXPECT_EQ(run.mode.chicken(), 0);
	run.mode.onRoundEnd(run.match, RoundEndReason::TimeUp);
	EXPECT_TRUE(std::ranges::none_of(run.match.announcements,
		[](const auto& a) { return a.first == CH::Announcement::ChickenPlayerEliminated; }));
}

TEST(ChickenRunMode, MatchWinnerHasTheMostPoints)
{
	Started run{ 3, settings(2, 1) };
	run.match.player(1).points = 9;
	run.mode.onMatchEnd(run.match);
	EXPECT_EQ(run.match.announcements.back(), std::make_pair(CH::Announcement::ArenaWinner, std::string{ "P1" }));
}
