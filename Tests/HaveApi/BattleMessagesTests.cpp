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
#include <array>
#include <cstdint>
#include <string>
#include "HaveApi/BattleMessages.h"
#include "HaveApi/MessageReader.h"

namespace CH = Common::HaveApi;

TEST(HaveApiBattle, PlayerIdIsTheUniqueIdWithoutTheTopBit)
{
	// session 499 on server 1, the id seen in the DLL's scoreboard messages
	EXPECT_EQ(CH::playerIdFromUniqueId((1u << 16) | 499u), 0x101F3);
	EXPECT_EQ(CH::playerIdFromUniqueId(0x80000000u | 0x101F3u), 0x101F3);
}

TEST(HaveApiBattle, NeutralModifiersKeepTheGameBehaviour)
{
	constexpr CH::Modifiers modifiers = CH::neutralModifiers();
	EXPECT_EQ(modifiers.head.canDamageOthers, 1);
	EXPECT_FLOAT_EQ(modifiers.head.outgoingDamage, 1.0f);
	for (const float scale : modifiers.head.attribute87Scale) EXPECT_FLOAT_EQ(scale, 1.0f);
	for (const float scale : modifiers.head.attribute86Scale) EXPECT_FLOAT_EQ(scale, 1.0f);
	EXPECT_EQ(modifiers.tail.movementSpeedOverride, 0);
	EXPECT_FLOAT_EQ(modifiers.tail.movementSpeedScale, 1.0f);
	EXPECT_EQ(modifiers.tail.maxHealthOverride, 0);
	EXPECT_FLOAT_EQ(modifiers.tail.maxHealthScale, 1.0f);
	EXPECT_EQ(modifiers.markerRange, 0);
	EXPECT_FLOAT_EQ(modifiers.incomingDamage, 1.0f);
}

TEST(HaveApiBattle, RosterLayout)
{
	const std::array<CH::RosterEntry, 2> players = { CH::RosterEntry{ 0x101F3, 1, 10, 1 }, CH::RosterEntry{ 0x101F4, 2, 20, 2 } };
	const auto data = CH::encodeRoster(true, 120, 1500, players);
	const auto message = CH::ReceivedMessage::parse(data);
	ASSERT_TRUE(message.has_value());
	EXPECT_EQ(message->id(), 0x0E);
	ASSERT_EQ(message->payload().size(), sizeof(CH::RosterPrefix) + 2 * sizeof(CH::RosterEntry));
	const auto prefix = message->payloadAs<CH::RosterPrefix>();
	EXPECT_EQ(prefix->respawnAllowed, 1);
	EXPECT_EQ(prefix->roundSeconds, 120);
	EXPECT_EQ(prefix->elapsedMilliseconds, 1500);
	EXPECT_EQ(prefix->count, 2);
	// the second entry starts at message offset 0x35 + 16 (DLL: RosterMessage::entries at 0x35)
	EXPECT_EQ(message->payload()[sizeof(CH::RosterPrefix) + sizeof(CH::RosterEntry)], 0xF4);
}

TEST(HaveApiBattle, RoundStateBannerAndTiming)
{
	CH::RoundState round;
	round.banner = "Round 1";
	round.lengthSeconds = 120;
	round.countdownSounds = true;
	const auto payload = CH::ReceivedMessage::parse(CH::encodeRoundState(round))->payloadAs<CH::RoundStatePayload>();
	ASSERT_TRUE(payload.has_value());
	EXPECT_EQ(std::string(payload->roundBanner.data()), "Round 1");
	EXPECT_EQ(payload->roundLengthSeconds, 120u);
	EXPECT_EQ(payload->countdownSounds, 1);
	EXPECT_FLOAT_EQ(payload->modifiersHead.outgoingDamage, 1.0f);
	EXPECT_FLOAT_EQ(payload->incomingDamage, 1.0f);
}

TEST(HaveApiBattle, BannerIsCutToTheField)
{
	CH::RoundState round;
	const std::string longBanner(100, 'R');
	round.banner = longBanner;
	const auto payload = CH::ReceivedMessage::parse(CH::encodeRoundState(round))->payloadAs<CH::RoundStatePayload>();
	EXPECT_EQ(payload->roundBanner.back(), '\0');
}

TEST(HaveApiBattle, AnnouncementLayout)
{
	const auto message = CH::ReceivedMessage::parse(CH::encodeAnnouncement(24, 5, "Winner"));
	ASSERT_TRUE(message.has_value());
	const auto prefix = message->payloadAs<CH::AnnouncementPrefix>();
	EXPECT_EQ(prefix->event, 24);
	EXPECT_EQ(prefix->durationSeconds, 5);
	const auto text = message->payload().subspan(sizeof(CH::AnnouncementPrefix)); // DLL: text at +0x30
	EXPECT_EQ(std::string(reinterpret_cast<const char*>(text.data())), "Winner");
}
