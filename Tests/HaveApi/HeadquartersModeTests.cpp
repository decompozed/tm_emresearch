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
#include "HaveApi/Modes/HeadquartersMode.h"

namespace CH = Common::HaveApi;
using namespace Main::HaveApi;
using Tests::FakeMatch;
using std::chrono::seconds;

namespace
{
	// settings: capture time (s), HQ lifetime (s), points limit, time limit (1 = Off)
	CH::SettingValues settings(std::int32_t capture = 10, std::int32_t lifetime = 60, std::int32_t points = 200)
	{
		return { capture, lifetime, points, 1, 0, 0 };
	}

	struct Started
	{
		FakeMatch match;
		HeadquartersMode mode{ 7 };
		int now = 0;

		Started(std::size_t players, CH::SettingValues values) : match{ CH::PartyModeId::Headquarters, players, values }
		{
			mode.onMatchStart(match);
			mode.onRoundStart(match);
		}

		void tick(int seconds = 1)
		{
			for (int i = 0; i < seconds; ++i)
			{
				match.elapsed = std::chrono::seconds{ ++now };
				mode.onTick(match);
			}
		}

		std::vector<std::int32_t> team(std::int32_t t)
		{
			std::vector<std::int32_t> ids;
			for (const PartyPlayer& p : match.players()) if (p.fallbackTeam == t) ids.push_back(p.playerId);
			return ids;
		}

		PartyPlayer& byId(std::int32_t id) { return *std::ranges::find(match.players(), id, &PartyPlayer::playerId); }

		// red captures with one player: capture time ticks
		void redCaptures(int captureSeconds = 10)
		{
			match.inZone = { team(HeadquartersMode::redTeam)[0] };
			tick(captureSeconds);
			match.inZone.clear();
		}
	};
}

TEST(HeadquartersMode, StartsWithANeutralHqAndTwoTeams)
{
	Started hq{ 4, settings() };
	EXPECT_EQ(hq.mode.phase(), HeadquartersMode::Phase::Neutral);
	EXPECT_EQ(hq.team(HeadquartersMode::redTeam).size(), 2u);
	ASSERT_FALSE(hq.match.zones.empty());
	EXPECT_TRUE(hq.match.zones.back().visible);
	EXPECT_EQ(hq.match.zones.back().state, 0);
	EXPECT_TRUE(hq.match.placed.back().visible);
	EXPECT_EQ(hq.mode.roundStartAnnouncement(hq.match)->first, CH::Announcement::HqOnline);
	EXPECT_TRUE(hq.mode.respawnAllowed());
	EXPECT_FALSE(hq.mode.showsRounds());
}

TEST(HeadquartersMode, OnePlayerCapturesInCaptureTime)
{
	Started hq{ 4, settings(10) };
	hq.match.inZone = { hq.team(HeadquartersMode::redTeam)[0] };
	hq.tick(9);
	EXPECT_EQ(hq.mode.phase(), HeadquartersMode::Phase::Neutral);
	const auto& bar = hq.match.progress.back();
	EXPECT_EQ(bar.kind, static_cast<std::int32_t>(CH::HqProgressKind::Capturing));
	EXPECT_EQ(bar.team, HeadquartersMode::redTeam);
	EXPECT_EQ(bar.permille, 900);
	EXPECT_EQ(bar.permillePerSecond, 100);
	hq.tick();
	EXPECT_EQ(hq.mode.phase(), HeadquartersMode::Phase::Held);
	EXPECT_EQ(hq.mode.owner(), HeadquartersMode::redTeam);
	EXPECT_EQ(hq.mode.redPoints(), HeadquartersMode::captureBonus);
	EXPECT_EQ(hq.match.blockedTeams.back(), HeadquartersMode::redTeam);
	EXPECT_EQ(hq.match.zones.back().state, HeadquartersMode::redTeam);
	EXPECT_EQ(hq.byId(hq.team(HeadquartersMode::redTeam)[0]).points, hq.match.rules().lastStandingPoints);
	EXPECT_TRUE(std::ranges::find(hq.match.teamAnnouncements, std::pair{ CH::Announcement::HqCaptured, HeadquartersMode::redTeam }) !=
		hq.match.teamAnnouncements.end());
	EXPECT_TRUE(std::ranges::find(hq.match.teamAnnouncements, std::pair{ CH::Announcement::HqEnemyCaptured, HeadquartersMode::blueTeam }) !=
		hq.match.teamAnnouncements.end());
}

TEST(HeadquartersMode, MorePlayersCaptureFaster)
{
	EXPECT_EQ(HeadquartersMode::speed(1), 1.0);
	EXPECT_EQ(HeadquartersMode::speed(2), 1.5);
	EXPECT_EQ(HeadquartersMode::speed(3), 2.0);
	EXPECT_EQ(HeadquartersMode::speed(6), 2.0);
	Started hq{ 6, settings(10) };
	hq.match.inZone = hq.team(HeadquartersMode::redTeam); // 3 players: x2
	hq.tick(5);
	EXPECT_EQ(hq.mode.phase(), HeadquartersMode::Phase::Held);
}

TEST(HeadquartersMode, ContestedPausesAndLeavingLosesTheProgress)
{
	Started hq{ 4, settings(10) };
	const auto red = hq.team(HeadquartersMode::redTeam)[0];
	const auto blue = hq.team(HeadquartersMode::blueTeam)[0];
	hq.match.inZone = { red };
	hq.tick(4);
	hq.match.inZone = { red, blue };
	hq.tick(3);
	EXPECT_EQ(hq.match.progress.back().permille, 400);         // paused
	EXPECT_EQ(hq.match.progress.back().permillePerSecond, 0);
	EXPECT_EQ(hq.match.zones.back().state, 3);                  // contested
	EXPECT_EQ(hq.match.announcements.back().first, CH::Announcement::HqContested);
	hq.match.inZone = { red };
	hq.tick(6);
	EXPECT_EQ(hq.mode.phase(), HeadquartersMode::Phase::Held);  // 4 + 6

	Started other{ 4, settings(10) };
	other.match.inZone = { other.team(HeadquartersMode::redTeam)[0] };
	other.tick(5);
	other.match.inZone.clear();
	other.tick();
	EXPECT_EQ(other.match.progress.back().kind, static_cast<std::int32_t>(CH::HqProgressKind::Hidden));
	other.match.inZone = { other.team(HeadquartersMode::redTeam)[0] };
	other.tick(9);
	EXPECT_EQ(other.mode.phase(), HeadquartersMode::Phase::Neutral); // started again from 0
}

TEST(HeadquartersMode, OwnersScoreEverySecondAndTheHqExpires)
{
	Started hq{ 4, settings(10, 45) };
	hq.redCaptures();
	hq.tick(10);
	EXPECT_EQ(hq.mode.redPoints(), HeadquartersMode::captureBonus + 10);
	EXPECT_EQ(hq.match.progress.back().kind, static_cast<std::int32_t>(CH::HqProgressKind::Held));
	EXPECT_EQ(hq.match.progress.back().secondsLeft, 35);
	hq.tick(35);
	EXPECT_EQ(hq.mode.phase(), HeadquartersMode::Phase::Waiting);
	EXPECT_EQ(hq.match.announcements.back().first, CH::Announcement::HqExpired);
	EXPECT_EQ(hq.match.blockedTeams.back(), 0);            // the owners respawn again
	EXPECT_FALSE(hq.match.zones.back().visible);
	EXPECT_FALSE(hq.match.placed.back().visible);
	EXPECT_EQ(hq.match.progress.back().kind, static_cast<std::int32_t>(CH::HqProgressKind::NextHq));
}

TEST(HeadquartersMode, AttackersDestroyTheHq)
{
	Started hq{ 4, settings(10) };
	hq.redCaptures();
	const auto blue = hq.team(HeadquartersMode::blueTeam);
	hq.match.inZone = blue; // 2 players: x1.5 -> 7 s for 10 s of work
	hq.tick(7);
	EXPECT_EQ(hq.mode.phase(), HeadquartersMode::Phase::Waiting);
	EXPECT_EQ(hq.mode.bluePoints(), HeadquartersMode::captureBonus);
	EXPECT_EQ(hq.match.announcements.back().first, CH::Announcement::HqDestroyed);
	EXPECT_EQ(hq.match.blockedTeams.back(), 0);
	for (const auto id : blue)
		EXPECT_EQ(hq.byId(id).points, hq.match.rules().lastStandingPoints);
}

TEST(HeadquartersMode, ADefenderInsideStopsTheDestruction)
{
	Started hq{ 4, settings(10) };
	hq.redCaptures();
	const auto red = hq.team(HeadquartersMode::redTeam)[0];
	const auto blue = hq.team(HeadquartersMode::blueTeam)[0];
	hq.match.inZone = { blue };
	hq.tick(5);
	EXPECT_EQ(hq.match.progress.back().kind, static_cast<std::int32_t>(CH::HqProgressKind::Destroying));
	EXPECT_EQ(hq.match.progress.back().team, HeadquartersMode::blueTeam);
	hq.match.inZone = { blue, red };
	hq.tick(10);
	EXPECT_EQ(hq.mode.phase(), HeadquartersMode::Phase::Held);  // paused, not destroyed
	hq.byId(blue).alive = false;                                // killed in the HQ
	hq.tick();
	EXPECT_EQ(hq.match.progress.back().kind, static_cast<std::int32_t>(CH::HqProgressKind::Held)); // progress lost
}

TEST(HeadquartersMode, DeadPlayersDoNotCountAndRespawnsReviveThem)
{
	Started hq{ 4, settings(10) };
	auto& red = hq.byId(hq.team(HeadquartersMode::redTeam)[0]);
	auto& blue = hq.byId(hq.team(HeadquartersMode::blueTeam)[0]);
	hq.mode.onDeath(hq.match, red, &blue);
	EXPECT_FALSE(red.alive);
	EXPECT_EQ(blue.points, hq.match.rules().killPoints);
	hq.match.inZone = { red.playerId };       // its last position
	hq.tick(10);
	EXPECT_EQ(hq.mode.phase(), HeadquartersMode::Phase::Neutral);
	hq.mode.onRespawn(hq.match, red);
	EXPECT_TRUE(red.alive);
}

TEST(HeadquartersMode, NextHqComesOnlineElsewhereAfterTheDelay)
{
	Started hq{ 4, settings(10, 45) };
	const auto first = hq.mode.currentSpot();
	hq.redCaptures();
	hq.tick(45);
	ASSERT_EQ(hq.mode.phase(), HeadquartersMode::Phase::Waiting);
	hq.tick(static_cast<int>(HeadquartersMode::nextHqDelay.count()) - 1);
	EXPECT_EQ(hq.mode.phase(), HeadquartersMode::Phase::Waiting);
	hq.tick();
	EXPECT_EQ(hq.mode.phase(), HeadquartersMode::Phase::Neutral);
	EXPECT_NE(hq.mode.currentSpot(), first);
	EXPECT_EQ(hq.match.announcements.back().first, CH::Announcement::HqOnline);
	EXPECT_TRUE(hq.match.zones.back().visible);
	EXPECT_TRUE(hq.match.placed.back().visible);
}

TEST(HeadquartersMode, NextHqIsAwayFromTheClientsSpawns)
{
	Started hq{ 4, settings(10, 45) };
	const auto spots = CH::findHardpointZones(hq.match.map);
	ASSERT_GE(spots.size(), 3u);
	const std::size_t free = (hq.mode.currentSpot() + 2) % spots.size();
	for (std::size_t i = 0; i < spots.size(); ++i)
	{
		if (i != free) hq.match.spawns.push_back({ spots[i].x, spots[i].y - 200 });
	}
	hq.redCaptures();
	hq.tick(45 + static_cast<int>(HeadquartersMode::nextHqDelay.count()));
	ASSERT_EQ(hq.mode.phase(), HeadquartersMode::Phase::Neutral);
	EXPECT_EQ(hq.mode.currentSpot(), free);
}

TEST(HeadquartersMode, PointsLimitEndsTheMatch)
{
	Started hq{ 2, settings(5, 90, 100) };
	hq.match.inZone = { hq.team(HeadquartersMode::redTeam)[0] };
	hq.tick(5);
	hq.match.inZone.clear();
	EXPECT_FALSE(hq.mode.isRoundOver(hq.match));
	hq.tick(90);
	EXPECT_LE(hq.mode.redPoints(), 100);
	EXPECT_TRUE(hq.mode.isRoundOver(hq.match) || hq.mode.phase() == HeadquartersMode::Phase::Waiting);
	hq.mode.onRoundEnd(hq.match, RoundEndReason::LastStanding);
	EXPECT_EQ(hq.match.blockedTeams.back(), 0);
	EXPECT_FALSE(hq.match.zones.back().visible);
	EXPECT_EQ(hq.match.announcements.back().first, CH::teamWon(HeadquartersMode::redTeam));
}

TEST(HeadquartersMode, LateJoinersGoToTheSmallerTeam)
{
	Started hq{ 3, settings() };
	const auto red = hq.team(HeadquartersMode::redTeam).size();
	const std::int32_t expected = red < 2 ? HeadquartersMode::redTeam : HeadquartersMode::blueTeam;
	EXPECT_EQ(hq.mode.teamForLateJoiner(hq.match), expected);
}

TEST(HeadquartersMode, SettingsAreValidated)
{
	CH::PartySettingsPayload payload{ static_cast<std::int32_t>(CH::PartyModeId::Headquarters), settings() };
	EXPECT_TRUE(CH::isValidPartySettings(payload));
	payload.values[0] = 7;
	EXPECT_FALSE(CH::isValidPartySettings(payload));
	EXPECT_EQ(CH::defaultSettings(CH::PartyModeId::Headquarters), settings());
}

// D94 (user 2026-10-03): a late joiner of the team holding the HQ does not appear, it waits dead with its team; one of
// the other team, or while nobody holds the HQ, plays at once.
TEST(HeadquartersMode, ALateJoinerOfTheHoldingTeamWaitsDead)
{
	Started hq{ 4, settings() };
	PartyPlayer red;
	red.playerId = 300;
	red.fallbackTeam = HeadquartersMode::redTeam;
	PartyPlayer blue = red;
	blue.playerId = 301;
	blue.fallbackTeam = HeadquartersMode::blueTeam;
	EXPECT_FALSE(hq.mode.keepsLateJoinerDead(red)); // neutral HQ
	EXPECT_FALSE(hq.mode.keepsLateJoinerDead(blue));
	hq.redCaptures();
	ASSERT_EQ(hq.mode.phase(), HeadquartersMode::Phase::Held);
	ASSERT_EQ(hq.mode.owner(), HeadquartersMode::redTeam);
	EXPECT_TRUE(hq.mode.keepsLateJoinerDead(red));   // red holds it: waits
	EXPECT_FALSE(hq.mode.keepsLateJoinerDead(blue)); // the other team plays
}
