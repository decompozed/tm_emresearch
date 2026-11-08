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
#include "HaveApi/Modes/HardpointMode.h"
#include "HaveApi/Modes/HeadquartersMode.h"
#include "HaveApi/Modes/TeamBaseSpawns.h"

namespace CH = Common::HaveApi;
using namespace Main::HaveApi;
using Tests::FakeMatch;

namespace
{
	// 0x38: each team at its own base, both bases' points
	void expectTeamBases(FakeMatch& match)
	{
		const auto spawns = CH::findPartySpawns(match.mapId());
		ASSERT_FALSE(spawns.empty());
		ASSERT_FALSE(match.teamBases.empty());
		const auto& bases = match.teamBases.back();
		EXPECT_EQ(bases.baseOf, (std::array<std::int32_t, 2>{ 1, 2 }));
		for (const std::int32_t team : { 1, 2 })
		{
			const auto index = static_cast<std::size_t>(team - 1);
			ASSERT_GT(bases.counts[index], 0);
			for (std::int32_t i = 0; i < bases.counts[index]; ++i)
			{
				const auto& p = bases.points[index][static_cast<std::size_t>(i)];
				EXPECT_TRUE(std::ranges::any_of(spawns, [&](const CH::PartySpawn& s) { return s.team == team && s.x == p[0] && s.y == p[1]; }));
			}
		}
	}
}

TEST(TeamBaseSpawns, TheTwoTeamsNeverShareASpawn)
{
	for (const CH::PartySpawn& a : CH::partySpawns)
	{
		for (const CH::PartySpawn& b : CH::findPartySpawns(a.map))
		{
			if (a.team != b.team)
			{
				EXPECT_GE(std::hypot(a.x - b.x, a.y - b.y), 500.0f) << a.map;
			}
		}
	}
}

TEST(TeamBaseSpawns, HardpointSendsEachPlayerItsBase)
{
	FakeMatch match{ CH::PartyModeId::Hardpoint, 6, { 45, 200, 1, 0, 0, 0 } };
	HardpointMode mode{ 3 };
	mode.onMatchStart(match);
	mode.onRoundStart(match);
	expectTeamBases(match);
}

TEST(TeamBaseSpawns, HeadquartersSendsEachPlayerItsBase)
{
	FakeMatch match{ CH::PartyModeId::Headquarters, 4, { 10, 60, 200, 1, 0, 0 } };
	HeadquartersMode mode{ 3 };
	mode.onMatchStart(match);
	mode.onRoundStart(match);
	expectTeamBases(match);
}
