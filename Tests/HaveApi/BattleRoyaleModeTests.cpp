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
#include <string>
#include <utility>
#include "FakeMatch.h"
#include "HaveApi/Catalog.h"
#include "HaveApi/Messages.h"
#include "HaveApi/PartyMaps.h"
#include "HaveApi/PartyRoomMaps.h"
#include "HaveApi/Modes/BattleRoyaleMode.h"

namespace CH = Common::HaveApi;
using namespace Main::HaveApi;

namespace
{
	using Tests::FakeMatch;

	constexpr auto battleRoyale = static_cast<std::int32_t>(CH::PartyModeId::BattleRoyale);
	constexpr auto arena = static_cast<std::int32_t>(CH::PartyModeId::Arena);

	struct Started
	{
		FakeMatch match;
		BattleRoyaleMode mode;

		explicit Started(std::size_t players, std::uint32_t map = CH::battleRoyaleMap)
			: match{ CH::PartyModeId::BattleRoyale, players, CH::defaultSettings(CH::PartyModeId::BattleRoyale) }
		{
			match.map = map;
			mode.onMatchStart(match);
			mode.onRoundStart(match);
		}

		void kill(std::size_t victim, std::size_t killer)
		{
			mode.onDeath(match, match.player(victim), &match.player(killer));
		}
	};
}

TEST(BattleRoyaleMode, OneRoundEveryoneAloneNoRespawnNoLateJoiners)
{
	Started br{ 3 };
	EXPECT_EQ(br.match.player(0).team, 1);
	EXPECT_EQ(br.match.player(2).team, 3);
	EXPECT_FALSE(br.mode.respawnAllowed());
	EXPECT_EQ(br.mode.roundsPerMatch(3), 1u);
	EXPECT_FALSE(br.mode.showsRounds());
	EXPECT_FALSE(br.mode.acceptsLateJoiners());
	EXPECT_TRUE(br.match.changedMaps.empty());
}

TEST(BattleRoyaleMode, AMatchOutsideTheBigMapMovesToIt)
{
	Started br{ 2, 6 };
	ASSERT_EQ(br.match.changedMaps.size(), 1u);
	EXPECT_EQ(br.match.changedMaps.front(), CH::battleRoyaleMap);
}

TEST(BattleRoyaleMode, DeathEliminatesPlacesAndFollowsTheKiller)
{
	Started br{ 4 };
	br.kill(1, 0);
	EXPECT_FALSE(br.match.player(1).alive);
	EXPECT_EQ(br.match.player(1).team, 0);
	EXPECT_EQ(br.match.player(1).placement, 4);
	EXPECT_EQ(br.match.player(0).points, 1);
	ASSERT_EQ(br.match.spectated.size(), 1u);
	EXPECT_EQ(br.match.spectated.back(), std::make_pair(101, 100));
	EXPECT_FALSE(br.mode.isRoundOver(br.match));
}

TEST(BattleRoyaleMode, FallScoresNobodyAndFollowsAnyLivingPlayer)
{
	Started br{ 3 };
	br.mode.onDeath(br.match, br.match.player(2), nullptr);
	EXPECT_EQ(br.match.player(2).placement, 3);
	EXPECT_TRUE(br.match.points.empty());
	EXPECT_EQ(br.match.spectated.back(), std::make_pair(102, 0));
}

TEST(BattleRoyaleMode, LastStandingWinsAndPlacementBonusFollowsThePlaces)
{
	Started br{ 3 };
	br.kill(1, 0);
	br.kill(2, 0);
	ASSERT_TRUE(br.mode.isRoundOver(br.match));
	br.mode.onRoundEnd(br.match, RoundEndReason::LastStanding);
	EXPECT_EQ(br.match.player(0).placement, 1);
	EXPECT_EQ(br.match.player(2).placement, 2);
	EXPECT_EQ(br.match.player(1).placement, 3);
	EXPECT_EQ(br.match.player(0).points, 2 + BattleRoyaleMode::placementPoints[0]);
	EXPECT_EQ(br.match.player(2).points, BattleRoyaleMode::placementPoints[1]);
	EXPECT_EQ(br.match.player(1).points, BattleRoyaleMode::placementPoints[2]);
	ASSERT_FALSE(br.match.announcements.empty());
	EXPECT_EQ(br.match.announcements.back(), std::make_pair(CH::Announcement::BrLastToyStanding, std::string{ "P0" }));
	br.mode.onMatchEnd(br.match);
	EXPECT_EQ(std::ranges::count(br.match.announcements, CH::Announcement::BrLastToyStanding, &decltype(br.match.announcements)::value_type::first), 1);
	EXPECT_EQ(br.match.player(0).points, 2 + BattleRoyaleMode::placementPoints[0]);
}

TEST(BattleRoyaleMode, TheLastTwoDyingTogetherLeaveTheLastDeadFirst)
{
	Started br{ 2 };
	br.mode.onDeath(br.match, br.match.player(0), nullptr);
	br.mode.onDeath(br.match, br.match.player(1), nullptr);
	ASSERT_TRUE(br.mode.isRoundOver(br.match));
	br.mode.onRoundEnd(br.match, RoundEndReason::LastStanding);
	EXPECT_EQ(br.match.player(1).placement, 1);
	EXPECT_EQ(br.match.player(0).placement, 2);
}

TEST(BattleRoyaleMode, ALivingPlayerWhoLeavesIsPlacedAfterTheOthersAlive)
{
	Started br{ 4 };
	br.kill(3, 0);
	br.mode.onPlayerLeft(br.match, br.match.player(2));
	EXPECT_EQ(br.match.player(2).placement, 3);
	EXPECT_FALSE(br.match.player(2).alive);
	br.mode.onPlayerLeft(br.match, br.match.player(3));
	EXPECT_EQ(br.match.player(3).placement, 4);
	EXPECT_FALSE(br.mode.isRoundOver(br.match));
}

TEST(BattleRoyaleMode, TheLastPlayerLeftWinsAtTheMatchEnd)
{
	Started br{ 2 };
	br.mode.onPlayerLeft(br.match, br.match.player(1));
	br.mode.onMatchEnd(br.match);
	EXPECT_EQ(br.match.player(0).placement, 1);
	EXPECT_EQ(br.match.player(1).placement, 2);
	EXPECT_EQ(br.match.announcements.back(), std::make_pair(CH::Announcement::BrLastToyStanding, std::string{ "P0" }));
}

TEST(BattleRoyaleMode, TimeUpRanksTheSurvivorsByKills)
{
	Started br{ 4 };
	br.kill(3, 1);
	br.mode.onRoundEnd(br.match, RoundEndReason::TimeUp);
	EXPECT_EQ(br.match.player(1).placement, 1);
	EXPECT_EQ(br.match.player(0).placement, 2);
	EXPECT_EQ(br.match.player(2).placement, 3);
	EXPECT_EQ(br.match.player(3).placement, 4);
	EXPECT_EQ(br.match.announcements.back().first, CH::Announcement::ArenaTimeUp);
	br.mode.onMatchEnd(br.match);
	EXPECT_EQ(br.match.announcements.back(), std::make_pair(CH::Announcement::BrLastToyStanding, std::string{ "P1" }));
}

TEST(BattleRoyaleMode, NobodyDealsDamageUntilEveryoneLoadedAndTheCountdownEnds)
{
	Started br{ 2 };
	br.match.player(1).loaded = false;
	EXPECT_EQ(br.match.modifiers[100].head.canDamageOthers, 0);
	EXPECT_EQ(br.match.modifiers[101].head.canDamageOthers, 0);
	br.match.elapsed = std::chrono::seconds{ 10 };
	br.mode.onTick(br.match);
	EXPECT_TRUE(br.match.preparePanels.empty());
	br.match.player(1).loaded = true;
	br.mode.onTick(br.match);
	ASSERT_EQ(br.match.preparePanels.size(), 1u);
	EXPECT_EQ(br.match.preparePanels.front(), BattleRoyaleMode::dropCountdownSeconds);
	br.match.elapsed = std::chrono::seconds{ 14 };
	br.mode.onTick(br.match);
	EXPECT_FALSE(br.mode.armed());
	br.match.elapsed = std::chrono::seconds{ 15 };
	br.mode.onTick(br.match);
	EXPECT_TRUE(br.mode.armed());
	EXPECT_EQ(br.match.modifiers[100].head.canDamageOthers, 1);
	EXPECT_EQ(br.match.modifiers[101].head.canDamageOthers, 1);
}

TEST(BattleRoyaleMode, TheDropDoesNotWaitForeverForASlowClient)
{
	Started br{ 2 };
	br.match.player(1).loaded = false;
	br.match.elapsed = BattleRoyaleMode::dropTimeout;
	br.mode.onTick(br.match);
	EXPECT_EQ(br.match.preparePanels.size(), 1u);
}

TEST(BattleRoyaleMode, SwitchedOffItIsNeitherOfferedNorListed)
{
	EXPECT_FALSE(CH::isPartyModeOffered(battleRoyale, false));
	EXPECT_TRUE(CH::isPartyModeOffered(battleRoyale, true));
	EXPECT_TRUE(CH::isPartyModeOffered(arena, false));
	EXPECT_EQ(CH::serverFeatures(false), 0);
	EXPECT_EQ(CH::serverFeatures(true), CH::BattleRoyaleFeature);
}

TEST(BattleRoyaleMode, AnOlderDllCannotPlayIt)
{
	EXPECT_EQ(CH::minimumDllProtocol(battleRoyale), CH::battleRoyaleDllProtocol);
	EXPECT_GT(CH::battleRoyaleDllProtocol, 3u);
	EXPECT_EQ(CH::minimumDllProtocol(arena), 0u);
}

TEST(BattleRoyaleMode, MatchTimeSetting)
{
	EXPECT_EQ(CH::defaultSettings(CH::PartyModeId::BattleRoyale)[0], 10);
	EXPECT_TRUE(CH::isValidPartySettings({ battleRoyale, { 8, 1, 1 } }));
	EXPECT_TRUE(CH::isValidPartySettings({ battleRoyale, { 12, 1, 1 } }));
	EXPECT_FALSE(CH::isValidPartySettings({ battleRoyale, { 10 } }));
	EXPECT_FALSE(CH::isValidPartySettings({ battleRoyale, { 9, 1, 1 } }));
	EXPECT_TRUE(CH::isValidPartySettings({ battleRoyale, { 10, 5, 2 } }));
	EXPECT_FALSE(CH::isValidPartySettings({ battleRoyale, { 10, 6, 1 } }));
	EXPECT_FALSE(CH::isValidPartySettings({ battleRoyale, { 10, 1, 3 } }));
	EXPECT_FALSE(CH::isValidPartySettings({ battleRoyale, { 10, 1, 1, 1 } }));
	EXPECT_EQ(CH::defaultSettings(CH::PartyModeId::BattleRoyale)[1], 1);
	EXPECT_EQ(CH::defaultSettings(CH::PartyModeId::BattleRoyale)[2], 1);
}

TEST(PartyMaps, TheBigMapIsForBattleRoyaleOnly)
{
	EXPECT_TRUE(CH::isPartyMapAllowed(battleRoyale, CH::battleRoyaleMap));
	EXPECT_FALSE(CH::isPartyMapAllowed(battleRoyale, 6));
	EXPECT_TRUE(CH::isPartyMapAllowed(battleRoyale, Common::Enums::Random));
	EXPECT_FALSE(CH::isPartyMapAllowed(arena, CH::battleRoyaleMap));
	EXPECT_FALSE(CH::isPartyMapAllowed(0, CH::battleRoyaleMap));
	EXPECT_TRUE(CH::isPartyMapAllowed(arena, 6));
	EXPECT_FALSE(CH::isPartyMapAllowed(static_cast<std::int32_t>(CH::PartyModeId::Domination), CH::battleRoyaleMap));
}

TEST(PartyMaps, ARoomKeepsToItsGamesMaps)
{
	EXPECT_EQ(allowedRoomMap(battleRoyale, CH::partyRoomMode, 6, 6), CH::battleRoyaleMap);
	EXPECT_EQ(allowedRoomMap(battleRoyale, CH::partyRoomMode, 6, CH::battleRoyaleMap), CH::battleRoyaleMap);
	EXPECT_EQ(allowedRoomMap(arena, CH::partyRoomMode, 6, CH::battleRoyaleMap), 6u);
	EXPECT_EQ(allowedRoomMap(arena, CH::partyRoomMode, CH::battleRoyaleMap, CH::battleRoyaleMap), static_cast<std::uint32_t>(Common::Enums::Random));
	EXPECT_EQ(allowedRoomMap(battleRoyale, 1, 6, CH::battleRoyaleMap), CH::battleRoyaleMap);
}
