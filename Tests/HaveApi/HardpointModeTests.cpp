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
#include "HaveApi/Modes/HardpointMode.h"

namespace CH = Common::HaveApi;
using namespace Main::HaveApi;
using Tests::FakeMatch;
using std::chrono::seconds;

namespace
{
	// settings: zone time (s), points limit, time limit (1 = Off)
	CH::SettingValues settings(std::int32_t zoneTime = 45, std::int32_t points = 200) { return { zoneTime, points, 1, 0, 0, 0 }; }

	struct Started
	{
		FakeMatch match;
		HardpointMode mode{ 5 };

		Started(std::size_t players, CH::SettingValues values) : match{ CH::PartyModeId::Hardpoint, players, values }
		{
			mode.onMatchStart(match);
			mode.onRoundStart(match);
			at(0); // first tick shows the zone
		}

		void at(int second)
		{
			match.elapsed = seconds{ second };
			mode.onTick(match);
		}

		std::vector<std::int32_t> team(std::int32_t t)
		{
			std::vector<std::int32_t> ids;
			for (const PartyPlayer& p : match.players()) if (p.fallbackTeam == t) ids.push_back(p.playerId);
			return ids;
		}

		PartyPlayer& byId(std::int32_t id) { return *std::ranges::find(match.players(), id, &PartyPlayer::playerId); }
	};
}

TEST(HardpointMode, TwoBalancedTeams)
{
	Started hp{ 5, settings() };
	const auto red = hp.team(HardpointMode::redTeam).size();
	const auto blue = hp.team(HardpointMode::blueTeam).size();
	EXPECT_EQ(red + blue, 5u);
	EXPECT_LE(std::max(red, blue) - std::min(red, blue), 1u);
	EXPECT_TRUE(hp.mode.respawnAllowed());
	EXPECT_EQ(hp.mode.roundsPerMatch(3), 1u);
}

TEST(HardpointMode, ZoneShownOnAMapSpot)
{
	Started hp{ 2, settings() };
	ASSERT_FALSE(hp.match.zones.empty());
	EXPECT_TRUE(hp.match.zones.back().visible);
	const auto spots = CH::findHardpointZones(6);
	ASSERT_FALSE(spots.empty());
	const auto& z = hp.match.zones.back();
	EXPECT_TRUE(std::ranges::any_of(spots, [&](const CH::ZoneSpot& s) { return s.x == z.x && s.y == z.y; }));
	EXPECT_TRUE(hp.match.placed.back().visible);
	EXPECT_EQ(hp.mode.roundStartAnnouncement(hp.match)->first, CH::Announcement::HardpointStart);
}

TEST(HardpointMode, ATeamAloneInTheZoneScores)
{
	Started hp{ 4, settings() };
	const auto red = hp.team(HardpointMode::redTeam);
	hp.match.inZone = { red[0], red[1] };     // two players count as one
	hp.at(1);
	EXPECT_EQ(hp.mode.redPoints(), 1);
	EXPECT_EQ(hp.mode.zoneState(), HardpointMode::ZoneState::Red);
	EXPECT_EQ(hp.match.announcements.back().first, CH::Announcement::HardpointRedHolding);
	EXPECT_EQ(hp.match.zones.back().state, 1);
	EXPECT_EQ(hp.match.scores.back().red, 1);
	EXPECT_EQ(hp.byId(red[0]).points, 1);      // the holders' own rows too
	hp.at(2);
	EXPECT_EQ(hp.mode.redPoints(), 2);
	EXPECT_EQ(hp.match.announcements.size(), 1u); // the holder is announced once
}

TEST(HardpointMode, ContestedZoneScoresNothing)
{
	Started hp{ 4, settings() };
	hp.match.inZone = { hp.team(1)[0], hp.team(2)[0] };
	hp.at(1);
	EXPECT_EQ(hp.mode.redPoints() + hp.mode.bluePoints(), 0);
	EXPECT_EQ(hp.mode.zoneState(), HardpointMode::ZoneState::Contested);
	EXPECT_EQ(hp.match.announcements.back().first, CH::Announcement::HardpointContested);
	hp.match.inZone = { hp.team(2)[0] };      // red left: blue holds
	hp.at(2);
	EXPECT_EQ(hp.mode.bluePoints(), 1);
}

TEST(HardpointMode, DeadPlayersDoNotHold)
{
	Started hp{ 2, settings() };
	const auto red = hp.team(1)[0];
	hp.byId(red).alive = false;
	hp.match.inZone = { red };
	hp.at(1);
	EXPECT_EQ(hp.mode.redPoints(), 0);
}

TEST(HardpointMode, ZoneMovesAfterTheZoneTimeWithAWarning)
{
	Started hp{ 2, settings(30) };
	const auto first = hp.mode.currentSpot();
	hp.at(19);
	EXPECT_TRUE(hp.match.announcements.empty());
	hp.at(20);
	EXPECT_EQ(hp.match.announcements.back().first, CH::Announcement::HardpointZoneMoving);
	hp.at(30);
	EXPECT_EQ(hp.match.announcements.back().first, CH::Announcement::HardpointZoneMoved);
	EXPECT_NE(hp.mode.currentSpot(), first);
	EXPECT_EQ(hp.mode.zoneState(), HardpointMode::ZoneState::Free);
}

TEST(HardpointMode, TheZoneMovesAwayFromTheClientsSpawns)
{
	Started hp{ 2, settings(30) };
	const auto spots = CH::findHardpointZones(hp.match.map);
	ASSERT_GE(spots.size(), 3u);
	const std::size_t free = (hp.mode.currentSpot() + 1) % spots.size();
	for (std::size_t i = 0; i < spots.size(); ++i)
	{
		if (i != free) hp.match.spawns.push_back({ spots[i].x + 100, spots[i].y }); // a client spawn at every other spot
	}
	hp.at(30);
	EXPECT_EQ(hp.mode.currentSpot(), free);
}

TEST(HardpointMode, SpotAwayFromSpawns)
{
	const std::vector<CH::ZoneSpot> spots{ { 0, 0, 0 }, { 10000, 0, 0 }, { 0, 10000, 0 }, { 5000, 5000, 0 } };
	std::mt19937 random{ 1 };
	// no spawn known: any spot but the current one
	for (int i = 0; i < 20; ++i) EXPECT_NE(spotAwayFromSpawns(spots, {}, 3000, 1, random), 1u);
	// only spot 2 is clear of the spawns
	const std::vector<SpawnPoint> spawns{ { 0, 500 }, { 10000, 500 }, { 5000, 4000 } };
	for (int i = 0; i < 20; ++i) EXPECT_EQ(spotAwayFromSpawns(spots, spawns, 3000, 0, random), 2u);
	// none clear: the one farthest from its nearest spawn (spot 2 is the current one)
	const std::vector<SpawnPoint> everywhere{ { 0, 0 }, { 10000, 1000 }, { 0, 8000 }, { 5000, 7000 } };
	EXPECT_EQ(spotAwayFromSpawns(spots, everywhere, 3000, 2, random), 3u);
	// a single spot stays
	EXPECT_EQ(spotAwayFromSpawns(std::span{ spots }.first(1), spawns, 3000, 0, random), 0u);
}

TEST(HardpointMode, SpawnPointsAreRememberedOnce)
{
	std::vector<SpawnPoint> spawns;
	rememberSpawn(spawns, { 0, 0 });
	rememberSpawn(spawns, { 100, 100 });      // same spawn point
	rememberSpawn(spawns, { 1000, 0 });
	EXPECT_EQ(spawns.size(), 2u);
	for (int i = 0; i < 100; ++i) rememberSpawn(spawns, { 5000.0f + 1000.0f * i, 0 });
	EXPECT_EQ(spawns.size(), maxSpawnPoints);  // the oldest are dropped
	EXPECT_EQ(spawns.back().x, 5000.0f + 1000.0f * 99);
}

TEST(HardpointMode, PointsLimitEndsTheMatchAndTheBestTeamWins)
{
	Started hp{ 2, settings(60, 100) };
	hp.match.inZone = { hp.team(2)[0] };
	for (int s = 1; s <= 100; ++s) hp.at(s % 50 + 1);   // stay under the zone time
	EXPECT_EQ(hp.mode.bluePoints(), 100);
	ASSERT_TRUE(hp.mode.isRoundOver(hp.match));
	hp.mode.onRoundEnd(hp.match, RoundEndReason::LastStanding);
	EXPECT_FALSE(hp.match.zones.back().visible);
	EXPECT_FALSE(hp.match.placed.back().visible);
	EXPECT_EQ(hp.match.announcements.back().first, CH::teamWon(2));
}

TEST(HardpointMode, TimeUpWithEqualPointsHasNoWinner)
{
	Started hp{ 2, settings() };
	const auto count = hp.match.announcements.size();
	hp.mode.onRoundEnd(hp.match, RoundEndReason::TimeUp);
	EXPECT_EQ(hp.match.announcements.size(), count);
}

TEST(HardpointMode, EnemyKillScoresTeamKillDoesNot)
{
	Started hp{ 4, settings() };
	PartyPlayer& a = hp.byId(hp.team(1)[0]);
	PartyPlayer& mate = hp.byId(hp.team(1)[1]);
	PartyPlayer& enemy = hp.byId(hp.team(2)[0]);
	hp.mode.onDeath(hp.match, mate, &a);
	EXPECT_EQ(a.points, 0);
	hp.mode.onDeath(hp.match, enemy, &a);
	EXPECT_EQ(a.points, 1);
	EXPECT_TRUE(enemy.alive);                 // respawn: nobody is eliminated
}

TEST(HardpointMode, LateJoinerGoesToTheSmallerTeam)
{
	Started hp{ 3, settings() };
	const auto smaller = hp.team(1).size() < hp.team(2).size() ? 1 : 2;
	EXPECT_EQ(hp.mode.teamForLateJoiner(hp.match), smaller);
}

TEST(HardpointMode, UnknownMapUsesTheCentre)
{
	FakeMatch match{ CH::PartyModeId::Hardpoint, 2, settings() };
	match.map = 999;
	HardpointMode mode{ 1 };
	mode.onMatchStart(match);
	mode.onRoundStart(match);
	match.elapsed = seconds{ 0 };
	mode.onTick(match);
	EXPECT_EQ(match.zones.back().x, 0.0f);
	EXPECT_EQ(match.zones.back().y, 0.0f);
}

TEST(HardpointZones, EveryMapHasFourToEightSpots)
{
	for (const CH::MapZones& map : CH::hardpointZones)
	{
		EXPECT_GE(map.count, 4) << map.map; // small maps (ToyGarden2, TrackersFactory): 4
		EXPECT_LE(map.count, 8) << map.map;
	}
}
