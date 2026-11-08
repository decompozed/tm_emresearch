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
#include "HaveApi/Modes/PropHuntMode.h"
#include "HaveApi/PropModels.h"

namespace CH = Common::HaveApi;
using namespace Main::HaveApi;
using Tests::FakeMatch;

namespace
{
	CH::SettingValues settings(std::int32_t hide = 30) { return { hide, 3, 3, 5, 3, 0 }; }

	struct Started
	{
		FakeMatch match;
		PropHuntMode mode{ 3 };
		int now = 0;

		Started(std::size_t players, CH::SettingValues values = settings()) : match{ CH::PartyModeId::PropHunt, players, values }
		{
			match.map = 22;
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

		std::vector<PartyPlayer*> team(std::int32_t t)
		{
			std::vector<PartyPlayer*> out;
			for (PartyPlayer& p : match.players()) if (p.fallbackTeam == t) out.push_back(&p);
			return out;
		}

		std::int32_t eventsOf(CH::PropEventKind kind) const
		{
			return static_cast<std::int32_t>(std::ranges::count(match.propEvents, static_cast<std::int32_t>(kind), &CH::PropEventPayload::kind));
		}

		CH::PropActionPayload action(CH::PropActionKind kind) { return { static_cast<std::int32_t>(kind), 100.0f, 200.0f, 300.0f }; }
	};
}

TEST(PropHuntMode, AboutOneHunterInThree)
{
	EXPECT_EQ(PropHuntMode::hunterCount(2), 1);
	EXPECT_EQ(PropHuntMode::hunterCount(3), 1);
	EXPECT_EQ(PropHuntMode::hunterCount(5), 2);
	EXPECT_EQ(PropHuntMode::hunterCount(6), 2);
	EXPECT_EQ(PropHuntMode::hunterCount(9), 3);
	Started ph{ 6 };
	EXPECT_EQ(ph.team(PropHuntMode::hunterTeam).size(), 2u);
	EXPECT_EQ(ph.team(PropHuntMode::propTeam).size(), 4u);
	EXPECT_FALSE(ph.mode.respawnAllowed());
}

TEST(PropHuntMode, PropsGetAnObjectOfTheMapAndCannotHurt)
{
	Started ph{ 4 };
	const std::int32_t models = CH::propModelCount(22);
	ASSERT_GT(models, 0);
	for (PartyPlayer* prop : ph.team(PropHuntMode::propTeam))
	{
		const auto state = ph.match.propStates.at(prop->playerId);
		EXPECT_GE(state.first, 0);
		EXPECT_LT(state.first, models);
		EXPECT_EQ(ph.match.modifiers.at(prop->playerId).head.canDamageOthers, 0);
	}
	for (PartyPlayer* hunter : ph.team(PropHuntMode::hunterTeam))
		EXPECT_EQ(ph.match.propStates.at(hunter->playerId).first, -1);
}

TEST(PropHuntMode, HuntersAreFrozenDuringTheHideTime)
{
	Started ph{ 3, settings(20) };
	PartyPlayer* hunter = ph.team(PropHuntMode::hunterTeam)[0];
	EXPECT_EQ(ph.mode.phase(), PropHuntMode::Phase::Hiding);
	EXPECT_EQ(ph.match.modifiers.at(hunter->playerId).tail.movementSpeedScale, 0.0f);
	EXPECT_EQ(ph.match.propPhases.at(hunter->playerId).phase, static_cast<std::int32_t>(CH::PropPhaseKind::Hiding));
	ph.tick(19);
	EXPECT_EQ(ph.mode.phase(), PropHuntMode::Phase::Hiding);
	ph.tick();
	EXPECT_EQ(ph.mode.phase(), PropHuntMode::Phase::Hunting);
	EXPECT_EQ(ph.match.modifiers.at(hunter->playerId).tail.movementSpeedScale, 1.0f);
	EXPECT_EQ(ph.match.modifiers.at(hunter->playerId).head.canDamageOthers, 1);
	EXPECT_EQ(ph.match.announcements.back().first, CH::Announcement::PropHuntersReleased);
}

TEST(PropHuntMode, HuntersNeverGetARadar)
{
	Started ph{ 4, settings(20) };
	ph.tick(180);
	EXPECT_TRUE(ph.match.radar.empty());
	EXPECT_EQ(ph.eventsOf(CH::PropEventKind::Whistle), 0);
	for (const auto& sent : ph.match.teamAnnouncements)
	{
		EXPECT_NE(static_cast<std::int32_t>(sent.first), 49);
		EXPECT_NE(static_cast<std::int32_t>(sent.first), 50);
	}
	ph.mode.onRoundStart(ph.match);
	EXPECT_TRUE(ph.match.radar.empty());
}

TEST(PropHuntMode, PropsAreToldWhenFoundAndFlashResult)
{
	Started ph{ 4 };
	PartyPlayer& prop = *ph.team(PropHuntMode::propTeam)[0];
	PartyPlayer& hunter = *ph.team(PropHuntMode::hunterTeam)[0];
	ph.tick(30);
	ph.mode.onHit(ph.match, prop, hunter, CH::DeathCause::Weapon);
	ph.mode.onHit(ph.match, prop, hunter, CH::DeathCause::Weapon); // not again at once
	EXPECT_EQ(std::ranges::count(ph.match.playerAnnouncements, std::pair{ CH::Announcement::PropFound, prop.playerId }), 1);
	ph.match.positions[hunter.playerId] = { 100.0f, 0.0f, 0.0f };
	ph.mode.onPropAction(ph.match, prop, ph.action(CH::PropActionKind::Flash)); // the action is at (0, 0, 0)
	EXPECT_EQ(ph.match.playerAnnouncements.back().first, CH::Announcement::PropFlashHit);
}

TEST(PropHuntMode, HuntersDieOfThePenalty)
{
	Started ph{ 4 };
	PartyPlayer& hunter = *ph.team(PropHuntMode::hunterTeam)[0];
	PartyPlayer& prop = *ph.team(PropHuntMode::propTeam)[0];
	ph.tick(30);
	ph.mode.onPropAction(ph.match, prop, ph.action(CH::PropActionKind::PenaltyDeath)); // props cannot
	ph.mode.onPropAction(ph.match, hunter, ph.action(CH::PropActionKind::PenaltyDeath));
	ASSERT_EQ(ph.match.killed.size(), 1u);
	EXPECT_EQ(ph.match.killed[0], hunter.playerId);
	ph.mode.onPropAction(ph.match, hunter, ph.action(CH::PropActionKind::DecoyHit));
	EXPECT_EQ(ph.eventsOf(CH::PropEventKind::RemoveDecoy), 1);
}

TEST(PropHuntMode, AbilitiesHaveLimitedUses)
{
	Started ph{ 4 };
	PartyPlayer& prop = *ph.team(PropHuntMode::propTeam)[0];
	const auto first = ph.mode.modelOf(prop.playerId);
	for (int i = 0; i < 5; ++i)
		ph.mode.onPropAction(ph.match, prop, ph.action(CH::PropActionKind::Change));
	EXPECT_NE(ph.mode.modelOf(prop.playerId), -1);
	EXPECT_EQ(ph.match.propPhases.at(prop.playerId).changesLeft, 0);
	(void)first;
	ph.mode.onPropAction(ph.match, prop, ph.action(CH::PropActionKind::Lock));
	EXPECT_TRUE(ph.match.propStates.at(prop.playerId).second);
	for (int i = 0; i < 4; ++i)
		ph.mode.onPropAction(ph.match, prop, ph.action(CH::PropActionKind::Decoy));
	EXPECT_EQ(ph.eventsOf(CH::PropEventKind::Decoy), 3);
	ph.mode.onPropAction(ph.match, prop, ph.action(CH::PropActionKind::Flash)); // not while hiding
	EXPECT_EQ(ph.eventsOf(CH::PropEventKind::Flash), 0);
	ph.tick(30);
	ph.mode.onPropAction(ph.match, prop, ph.action(CH::PropActionKind::Flash));
	ph.mode.onPropAction(ph.match, prop, ph.action(CH::PropActionKind::Flash));
	EXPECT_EQ(ph.eventsOf(CH::PropEventKind::Flash), 1);
	PartyPlayer& hunter = *ph.team(PropHuntMode::hunterTeam)[0];
	ph.mode.onPropAction(ph.match, hunter, ph.action(CH::PropActionKind::Decoy)); // hunters have no abilities
	EXPECT_EQ(ph.eventsOf(CH::PropEventKind::Decoy), 3);
}

TEST(PropHuntMode, ChangesDoNotRepeatAnObjectOfTheRound)
{
	CH::SettingValues values = settings();
	values[3] = 30;
	Started ph{ 4, values };
	PartyPlayer& prop = *ph.team(PropHuntMode::propTeam)[0];
	const std::int32_t models = CH::propModelCount(22);
	std::set<std::int32_t> seen{ ph.mode.modelOf(prop.playerId) };
	for (int i = 1; i < models; ++i)
	{
		ph.mode.onPropAction(ph.match, prop, ph.action(CH::PropActionKind::Change));
		seen.insert(ph.mode.modelOf(prop.playerId));
	}
	EXPECT_EQ(static_cast<std::int32_t>(seen.size()), models); // every object once before any repeats
}

TEST(PropHuntMode, HuntersBreakDecoys)
{
	Started ph{ 4 };
	PartyPlayer& prop = *ph.team(PropHuntMode::propTeam)[0];
	PartyPlayer& hunter = *ph.team(PropHuntMode::hunterTeam)[0];
	ph.mode.onPropAction(ph.match, prop, ph.action(CH::PropActionKind::Decoy));
	ph.mode.onPropAction(ph.match, hunter, ph.action(CH::PropActionKind::DecoyHit)); // still hiding: ignored
	EXPECT_EQ(ph.eventsOf(CH::PropEventKind::RemoveDecoy), 0);
	ph.tick(30);
	ph.mode.onPropAction(ph.match, prop, ph.action(CH::PropActionKind::DecoyHit)); // props cannot break decoys
	ph.mode.onPropAction(ph.match, hunter, ph.action(CH::PropActionKind::DecoyHit));
	EXPECT_EQ(ph.eventsOf(CH::PropEventKind::RemoveDecoy), 1);
}

TEST(PropHuntMode, PropsHave5000HitPoints)
{
	Started ph{ 4 };
	PartyPlayer& prop = *ph.team(PropHuntMode::propTeam)[0];
	EXPECT_EQ(ph.match.modifiers.at(prop.playerId).tail.maxHealthOverride, PropHuntMode::propHealth);
	EXPECT_EQ(ph.match.health.at(prop.playerId), PropHuntMode::propHealth);
	PartyPlayer& hunter = *ph.team(PropHuntMode::hunterTeam)[0];
	EXPECT_EQ(ph.match.modifiers.at(hunter.playerId).tail.maxHealthOverride, 0);
}

TEST(PropHuntMode, HuntersWinWhenEveryPropIsFound)
{
	Started ph{ 3 };
	PartyPlayer& hunter = *ph.team(PropHuntMode::hunterTeam)[0];
	for (PartyPlayer* prop : ph.team(PropHuntMode::propTeam))
	{
		EXPECT_FALSE(ph.mode.isRoundOver(ph.match));
		ph.mode.onDeath(ph.match, *prop, &hunter);
		EXPECT_EQ(ph.match.propStates.at(prop->playerId).first, -1);
	}
	EXPECT_TRUE(ph.mode.isRoundOver(ph.match));
	ph.mode.onRoundEnd(ph.match, RoundEndReason::LastStanding);
	EXPECT_EQ(ph.match.announcements.back().first, CH::Announcement::PropHuntersWin);
	EXPECT_EQ(hunter.points, 2 * ph.match.rules().killPoints + ph.match.rules().lastStandingPoints);
}

TEST(PropHuntMode, PropsWinWhenTheTimeIsUp)
{
	Started ph{ 3 };
	ph.mode.onRoundEnd(ph.match, RoundEndReason::TimeUp);
	EXPECT_EQ(ph.match.announcements.back().first, CH::Announcement::PropPropsWin);
	for (PartyPlayer* prop : ph.team(PropHuntMode::propTeam))
	{
		EXPECT_EQ(prop->points, ph.match.rules().lastStandingPoints);
		EXPECT_EQ(ph.match.propStates.at(prop->playerId).first, -1);
	}
	EXPECT_EQ(ph.mode.phase(), PropHuntMode::Phase::Off);
}

TEST(PropHuntMode, RolesRotate)
{
	Started ph{ 3 };
	std::set<std::int32_t> hunted;
	for (int round = 0; round < 3; ++round)
	{
		if (round > 0)
			ph.mode.onRoundStart(ph.match);
		hunted.insert(ph.team(PropHuntMode::hunterTeam)[0]->playerId);
	}
	EXPECT_EQ(hunted.size(), 3u); // everybody hunted once in three rounds
}

TEST(PropHuntMode, SettingsAreValidated)
{
	CH::PartySettingsPayload payload{ static_cast<std::int32_t>(CH::PartyModeId::PropHunt), settings() };
	EXPECT_TRUE(CH::isValidPartySettings(payload));
	payload.values[2] = 5;
	payload.values[4] = 10;
	EXPECT_TRUE(CH::isValidPartySettings(payload));
	payload.values[5] = 3;
	EXPECT_FALSE(CH::isValidPartySettings(payload));
	payload.values[5] = 0;
	payload.values[2] = 90;
	EXPECT_FALSE(CH::isValidPartySettings(payload));
	payload.values[2] = 3;
	payload.values[1] = 7;
	EXPECT_FALSE(CH::isValidPartySettings(payload));
	EXPECT_EQ(CH::defaultSettings(CH::PartyModeId::PropHunt), settings());
}
