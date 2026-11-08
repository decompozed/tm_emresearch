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
#include "FakeMatch.h"
#include "HaveApi/PartyRoomMaps.h"
#include "HaveApi/Modes/DominationMode.h"

namespace CH = Common::HaveApi;
using namespace Main::HaveApi;
using Tests::FakeMatch;

namespace
{
	constexpr std::uint32_t academy = 1;
	constexpr std::uint32_t mapWithoutZones = 2; // AcademyInvasion

	// settings: points limit, time limit
	struct Started
	{
		FakeMatch match;
		DominationMode mode{ 7 };

		explicit Started(std::size_t players, std::int32_t limit = 350, std::uint32_t map = academy)
			: match{ CH::PartyModeId::Domination, players, { limit, 1, 0, 0, 0, 0 } }
		{
			match.map = map;
			mode.onMatchStart(match);
			mode.onRoundStart(match);
		}

		std::vector<std::int32_t> team(std::int32_t which)
		{
			std::vector<std::int32_t> ids;
			for (const PartyPlayer& player : match.players())
			{
				if (player.fallbackTeam == which)
				{
					ids.push_back(player.playerId);
				}
			}
			return ids;
		}

		PartyPlayer& player(std::int32_t id)
		{
			return *std::ranges::find(match.players(), id, &PartyPlayer::playerId);
		}

		void ticks(int count)
		{
			for (int i = 0; i < count; ++i)
			{
				mode.onTick(match);
			}
		}
	};

	constexpr std::int32_t red = DominationMode::redTeam;
	constexpr std::int32_t blue = DominationMode::blueTeam;
}

TEST(DominationMode, ThreeSquareZonesWithAHeightBandAndBalancedTeams)
{
	Started s{ 5 };
	ASSERT_EQ(s.match.squares.size(), 3u);
	const auto* map = CH::findDominationMap(academy);
	for (std::size_t i = 0; i < 3; ++i)
	{
		EXPECT_EQ(s.match.squares[i].index, i);
		EXPECT_TRUE(s.match.squares[i].active);
		EXPECT_EQ(s.match.squares[i].halfSize, DominationMode::zoneHalfSize);
		EXPECT_FLOAT_EQ(s.match.squares[i].zMin, map->zones[i].z - DominationMode::zoneBelow);
		EXPECT_FLOAT_EQ(s.match.squares[i].zMax, map->zones[i].z + DominationMode::zoneAbove);
	}
	EXPECT_LE(std::abs(static_cast<int>(s.team(red).size()) - static_cast<int>(s.team(blue).size())), 1);
	ASSERT_FALSE(s.match.dominationZones.empty());
	EXPECT_EQ(s.match.dominationZones.back().count, 3);
	EXPECT_FALSE(s.mode.showsRounds());
	EXPECT_TRUE(s.mode.respawnAllowed());
}

TEST(DominationMode, AMapWithoutZonesLoadsADominationMap)
{
	ASSERT_FALSE(DominationMode::supportsMap(mapWithoutZones));
	Started s{ 4, 350, mapWithoutZones };
	ASSERT_EQ(s.match.changedMaps.size(), 1u);
	EXPECT_TRUE(DominationMode::supportsMap(s.match.changedMaps.front()));
	EXPECT_EQ(s.mode.map()->map, s.match.changedMaps.front());
}

TEST(DominationMode, OnePlayerCapturesInTenSecondsAndScoresEverySecond)
{
	Started s{ 4 };
	const auto reds = s.team(red);
	s.match.inZones[2] = { reds[0] };
	s.ticks(9);
	EXPECT_EQ(s.mode.zone(2).owner, 0);
	EXPECT_EQ(s.mode.zone(2).workTeam, red);
	s.ticks(1);
	EXPECT_EQ(s.mode.zone(2).owner, red);
	EXPECT_EQ(s.mode.redPoints(), 1); // the capture tick scores too
	s.match.inZones[2].clear();
	s.ticks(5);
	EXPECT_EQ(s.mode.redPoints(), 6); // the zone stays red when empty
	EXPECT_EQ(s.mode.bluePoints(), 0);
	const auto captured = std::ranges::count(s.match.teamAnnouncements,
		std::pair{ CH::Announcement::DominationCapturedA, red } /* A */) + std::ranges::count(s.match.teamAnnouncements,
		std::pair{ static_cast<CH::Announcement>(static_cast<int>(CH::Announcement::DominationCapturedA) + 2), red });
	EXPECT_EQ(captured, 1); // "zone C captured" to red
	EXPECT_EQ(std::ranges::count(s.match.teamAnnouncements,
		std::pair{ static_cast<CH::Announcement>(static_cast<int>(CH::Announcement::DominationEnemyCapturedA) + 2), blue }), 1);
}

TEST(DominationMode, MorePlayersCaptureFaster)
{
	EXPECT_DOUBLE_EQ(DominationMode::speed(2), 1.5);
	EXPECT_DOUBLE_EQ(DominationMode::speed(5), 2.0);
	Started s{ 6 };
	const auto reds = s.team(red);
	s.match.inZones[0] = { reds[0], reds[1], reds[2] };
	s.ticks(4);
	EXPECT_EQ(s.mode.zone(0).owner, 0);
	s.ticks(1); // 5 x 2 = 10 s of work
	EXPECT_EQ(s.mode.zone(0).owner, red);
}

TEST(DominationMode, ContestedPausesAndLeavingLosesTheProgress)
{
	Started s{ 4 };
	const auto reds = s.team(red);
	const auto blues = s.team(blue);
	s.match.inZones[1] = { reds[0] };
	s.ticks(5);
	s.match.inZones[1] = { reds[0], blues[0] };
	s.ticks(20);
	EXPECT_TRUE(s.mode.zone(1).contested);
	EXPECT_EQ(s.mode.zone(1).owner, 0);
	EXPECT_DOUBLE_EQ(s.mode.zone(1).work, 5.0); // paused
	ASSERT_FALSE(s.match.dominationZones.empty());
	EXPECT_EQ(s.match.dominationZones.back().zones[1].permillePerSecond, 0);
	s.match.inZones[1] = { reds[0] };
	s.ticks(5);
	EXPECT_EQ(s.mode.zone(1).owner, red); // 5 + 5
	s.match.inZones[0] = { reds[0] };
	s.ticks(3);
	s.match.inZones[0].clear();
	s.ticks(1);
	EXPECT_EQ(s.mode.zone(0).work, 0.0); // left: lost
	EXPECT_EQ(s.mode.zone(0).workTeam, 0);
}

TEST(DominationMode, AnEnemyZonePassesDirectlyAndItsOwnersHearTheAttackOnce)
{
	Started s{ 4 };
	const auto reds = s.team(red);
	const auto blues = s.team(blue);
	s.match.inZones[2] = { reds[0] };
	s.ticks(10);
	ASSERT_EQ(s.mode.zone(2).owner, red);
	s.match.inZones[2] = { blues[0] };
	s.ticks(10);
	EXPECT_EQ(s.mode.zone(2).owner, blue); // no neutral step
	const auto underAttackC = static_cast<CH::Announcement>(static_cast<int>(CH::Announcement::DominationUnderAttackA) + 2);
	EXPECT_EQ(std::ranges::count(s.match.teamAnnouncements, std::pair{ underAttackC, red }), 1);
}

TEST(DominationMode, OwnersAloneInTheirZoneStopNothingAndTheDeadDoNotCount)
{
	Started s{ 4 };
	const auto reds = s.team(red);
	const auto blues = s.team(blue);
	s.match.inZones[0] = { reds[0] };
	s.ticks(10);
	s.match.inZones[0] = { reds[0], blues[0] };
	s.mode.onDeath(s.match, s.player(reds[0]), &s.player(blues[0])); // the red defender died (CastServer still lists it)
	s.ticks(10);
	EXPECT_EQ(s.mode.zone(0).owner, blue);
}

TEST(DominationMode, ThePointsLimitEndsTheMatchAndEqualPointsAreADraw)
{
	Started s{ 4, 100 };
	const auto reds = s.team(red);
	const auto blues = s.team(blue);
	s.match.inZones[0] = { reds[0] };
	s.match.inZones[1] = { blues[0] };
	s.ticks(10);
	EXPECT_EQ(s.mode.redPoints(), s.mode.bluePoints());
	s.mode.onRoundEnd(s.match, RoundEndReason::TimeUp);
	EXPECT_EQ(s.match.announcements.back().first, CH::Announcement::TeamClashTimeUp); // draw
	EXPECT_EQ(s.match.dominationZones.back().count, 0);    // the zones go away
	EXPECT_FALSE(s.match.squares.back().active);

	Started t{ 4, 100 };
	t.match.inZones[0] = t.team(red);
	t.match.inZones[2] = t.team(red);
	t.ticks(60);
	EXPECT_TRUE(t.mode.isRoundOver(t.match));
	EXPECT_EQ(t.mode.redPoints(), 100); // capped at the limit
}

// 0x38 to everybody: both bases (the official team spawns) and the base each team uses (D89).
TEST(DominationMode, EverybodyGetsBothBasesAndTheBaseOfEachTeam)
{
	Started s{ 4 };
	ASSERT_FALSE(s.match.teamBases.empty());
	const auto& bases = s.match.teamBases.back();
	EXPECT_EQ(bases.baseOf, (std::array<std::int32_t, 2>{ red, blue }));
	const auto spawns = CH::findPartySpawns(academy);
	for (const std::int32_t team : { red, blue })
	{
		const auto index = static_cast<std::size_t>(team - 1);
		ASSERT_GT(bases.counts[index], 0);
		for (std::int32_t i = 0; i < bases.counts[index]; ++i)
		{
			const auto& p = bases.points[index][static_cast<std::size_t>(i)];
			EXPECT_TRUE(std::ranges::any_of(spawns, [&](const CH::PartySpawn& q) { return q.team == team && q.x == p[0] && q.y == p[1]; }));
		}
	}
	const auto sent = s.match.teamBases.size();
	s.match.inZones[2] = { s.team(red)[0] }; // red takes C: nothing changes, nothing sent
	s.ticks(11);
	ASSERT_EQ(s.mode.zone(2).owner, red);
	EXPECT_EQ(s.match.teamBases.size(), sent);
}

// User 2026-10-03: at the own base; the teams swap bases (never one base for both) when a team holds the other's home
// zone (red A, blue B) and has not lost its own to it.
TEST(DominationMode, TheTeamsSwapBasesWhenATeamHoldsTheOthersHomeZone)
{
	Started s{ 4 };
	const auto reds = s.team(red);
	const auto blues = s.team(blue);
	const auto baseOf = [&] { return s.match.teamBases.back().baseOf; };
	const std::array<std::int32_t, 2> own{ red, blue };
	const std::array<std::int32_t, 2> swapped{ blue, red };
	EXPECT_EQ(baseOf(), own);

	s.match.inZones[0] = { blues[0] }; // blue takes A (red's home), B neutral: swapped
	s.ticks(11);
	ASSERT_EQ(s.mode.zone(0).owner, blue);
	EXPECT_EQ(s.mode.spawnBaseOf(red), blue);
	EXPECT_EQ(s.mode.spawnBaseOf(blue), red);
	EXPECT_EQ(baseOf(), swapped);

	s.match.inZones = {};
	s.match.inZones[1] = { blues[0] }; // blue holds both: each at its own base
	s.ticks(11);
	ASSERT_EQ(s.mode.zone(1).owner, blue);
	EXPECT_EQ(baseOf(), own);

	s.match.inZones = {};
	s.match.inZones[1] = { reds[0] }; // red takes B (blue's home), blue holds A: swapped
	s.ticks(11);
	ASSERT_EQ(s.mode.zone(1).owner, red);
	EXPECT_EQ(baseOf(), swapped);

	s.match.inZones = {};
	s.match.inZones[0] = { reds[0] }; // red takes A back (holds both): both at their own base again
	s.ticks(11);
	ASSERT_EQ(s.mode.zone(0).owner, red);
	EXPECT_EQ(baseOf(), own);
}

// Every map: A on the red base's side, B on the blue one's, C as far from both bases, no zone on a team spawn.
TEST(DominationMode, EveryMapHasAOnTheRedSideBOnTheBlueSideCInTheMiddle)
{
	for (const CH::DominationMap& map : CH::dominationMaps)
	{
		const auto spawns = CH::findPartySpawns(map.map);
		ASSERT_FALSE(spawns.empty()) << map.map;
		std::array<std::array<float, 2>, 3> base{};
		std::array<int, 3> count{};
		for (const CH::PartySpawn& spawn : spawns)
		{
			base[static_cast<std::size_t>(spawn.team)][0] += spawn.x;
			base[static_cast<std::size_t>(spawn.team)][1] += spawn.y;
			++count[static_cast<std::size_t>(spawn.team)];
		}
		ASSERT_GT(count[1], 0) << map.map;
		ASSERT_GT(count[2], 0) << map.map;
		const auto distanceTo = [&](const CH::ZoneSpot& zone, std::size_t team) {
			return std::hypot(zone.x - base[team][0] / static_cast<float>(count[team]), zone.y - base[team][1] / static_cast<float>(count[team]));
		};
		EXPECT_LT(distanceTo(map.zones[0], 1), distanceTo(map.zones[0], 2)) << map.map;
		EXPECT_LT(distanceTo(map.zones[1], 2), distanceTo(map.zones[1], 1)) << map.map;
		const float toRed = distanceTo(map.zones[2], 1);
		const float toBlue = distanceTo(map.zones[2], 2);
		EXPECT_LE(std::abs(toRed - toBlue) / (std::max)(toRed, toBlue), 0.26f) << map.map;
		// the maps whose A / B the user put at the bases (Tools/nif/domination_zones.py OVERRIDES) are left out
		constexpr std::array<std::uint32_t, 9> userBaseMaps{ 9, 11, 13, 18, 22, 23, 46, 49, 57 };
		const bool userBases = std::ranges::find(userBaseMaps, map.map) != userBaseMaps.end();
		const bool userZones = map.map == 9 || map.map == 49; // Cargo (O): the first version's zones, as the user asked
		for (std::size_t i = userZones ? 3 : userBases ? 2 : 0; i < map.zones.size(); ++i)
		{
			for (const CH::PartySpawn& spawn : spawns)
			{
				EXPECT_GE(std::hypot(map.zones[i].x - spawn.x, map.zones[i].y - spawn.y), 799.0f) << map.map;
			}
		}
	}
}

// The user left these out (2026-10-03): Chess, RockBand, RockBand S, The Aftermath, Invasion (Easy / Hard), Junk Yard,
// Town Square.
TEST(DominationMode, MapsLeftOutByTheUser)
{
	for (const std::uint32_t map : { 12u, 28u, 29u, 38u, 19u, 20u, 21u, 40u })
	{
		EXPECT_FALSE(DominationMode::supportsMap(map)) << map;
	}
}

TEST(DominationMode, ADominationRoomKeepsToMapsWithZones)
{
	const auto domination = static_cast<std::int32_t>(CH::PartyModeId::Domination);
	EXPECT_EQ(allowedRoomMap(domination, CH::partyRoomMode, academy, mapWithoutZones), academy);  // refused: the current map stays
	EXPECT_EQ(allowedRoomMap(domination, CH::partyRoomMode, academy, 18), 18u);                   // HouseTop has zones
	EXPECT_EQ(allowedRoomMap(domination, CH::partyRoomMode, mapWithoutZones, mapWithoutZones), CH::dominationMaps.front().map);
	EXPECT_EQ(allowedRoomMap(domination, CH::partyRoomMode, academy, 0), 0u);                     // Random: drawn among them
	EXPECT_EQ(allowedRoomMap(0, CH::partyRoomMode, academy, mapWithoutZones), mapWithoutZones);    // not Domination
}
