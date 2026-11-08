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
#include <vector>
#include "HaveApi/MessageReader.h"
#include "HaveApi/MessageWriter.h"
#include "HaveApi/Messages.h"

namespace CH = Common::HaveApi;

namespace
{
	std::uint32_t readU32(const std::vector<std::uint8_t>& bytes, std::size_t offset)
	{
		return static_cast<std::uint32_t>(bytes[offset]) | (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
			(static_cast<std::uint32_t>(bytes[offset + 2]) << 16) | (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
	}

	constexpr std::size_t lengthSize = sizeof(std::uint32_t);
}

TEST(HaveApiHeader, IdLivesInBits15To27OfTheQwordAt0x10)
{
	CH::MessageHeader header{};
	header.setId(CH::MessageId::PlayerPoints);
	EXPECT_EQ(header.idBits, std::uint64_t{ 0x13 } << 15);
	EXPECT_EQ(header.id(), 0x13);
}

TEST(HaveApiWriter, StatusTextLayout)
{
	const auto data = CH::encodeText(CH::MessageId::StatusText, "HELLO");
	ASSERT_EQ(data.size(), lengthSize + CH::payloadOffset + 6);
	EXPECT_EQ(readU32(data, 0), CH::payloadOffset + 6); // header + text + NUL, the length itself excluded
	EXPECT_EQ(readU32(data, lengthSize + 0x10), std::uint32_t{ 0x02 } << 15);
	EXPECT_EQ(std::string(reinterpret_cast<const char*>(data.data() + lengthSize + CH::payloadOffset)), "HELLO");
	EXPECT_EQ(data.back(), 0);
}

TEST(HaveApiWriter, TextIsCutAtTheMaximumLength)
{
	const std::string longText(CH::maxTextLength + 50, 'x');
	const auto data = CH::encodeText(CH::MessageId::CustomMessage, longText);
	EXPECT_EQ(data.size(), lengthSize + CH::payloadOffset + CH::maxTextLength + 1);
}

// Golden bytes: a 0x13 message captured in have_api_packets.log by the previous implementation, checked
// against the DLL reconstruction (playerId 0x101F3, points -10, notification 3).
TEST(HaveApiWriter, PlayerPointsMatchesCapturedBytes)
{
	std::vector<std::uint8_t> expected = { 0x34, 0x00, 0x00, 0x00 };
	expected.resize(lengthSize + 0x10, 0);
	constexpr std::array<std::uint8_t, 8> idBits = { 0x00, 0x80, 0x09, 0x00, 0x00, 0x00, 0x00, 0x00 };
	expected.insert(expected.end(), idBits.begin(), idBits.end());
	expected.resize(lengthSize + CH::payloadOffset, 0);
	constexpr std::array<std::uint8_t, 12> payload = { 0xF3, 0x01, 0x01, 0x00, 0xF6, 0xFF, 0xFF, 0xFF, 0x03, 0x00, 0x00, 0x00 };
	expected.insert(expected.end(), payload.begin(), payload.end());

	const auto data = CH::encode(CH::MessageId::PlayerPoints, CH::PlayerPointsPayload{ 0x101F3, -10, 3 });
	EXPECT_EQ(data, expected);
}

TEST(HaveApiReader, RoundTrip)
{
	const CH::PartySettingsPayload sent{ 5, { 8, 2, 1, 1, 1, 0 } };
	const auto data = CH::encode(CH::MessageId::PartySettings, sent);

	const auto message = CH::ReceivedMessage::parse(data);
	ASSERT_TRUE(message.has_value());
	EXPECT_EQ(message->id(), 0x0A);
	const auto received = message->payloadAs<CH::PartySettingsPayload>();
	ASSERT_TRUE(received.has_value());
	EXPECT_EQ(received->modeId, 5);
	EXPECT_EQ(received->values, sent.values);
}

TEST(HaveApiReader, RoomOptionFromTheDll)
{
	const auto data = CH::encode(CH::MessageId::RoomOptions, CH::RoomOptionPayload{ CH::RoomOptionKind::MaxHealth, 1500 });
	EXPECT_EQ(data.size(), lengthSize + 48); // the DLL sends 48 bytes after the length
	const auto option = CH::ReceivedMessage::parse(data)->payloadAs<CH::RoomOptionPayload>();
	ASSERT_TRUE(option.has_value());
	EXPECT_EQ(option->kind, CH::RoomOptionKind::MaxHealth);
	EXPECT_EQ(option->value, 1500);
}

TEST(HaveApiReader, RejectsTruncatedOrInconsistentData)
{
	auto data = CH::encodeEmpty(CH::MessageId::CreatePartyMode);
	EXPECT_TRUE(CH::ReceivedMessage::parse(data).has_value());

	std::vector<std::uint8_t> tooShort(data.begin(), data.end() - 1);
	EXPECT_FALSE(CH::ReceivedMessage::parse(tooShort).has_value());

	data[0] = 0xFF; // length larger than the data
	EXPECT_FALSE(CH::ReceivedMessage::parse(data).has_value());

	data[0] = 0x10; // length shorter than the header
	EXPECT_FALSE(CH::ReceivedMessage::parse(data).has_value());
}

TEST(HaveApiReader, PayloadShorterThanTheStructureIsRefused)
{
	const auto data = CH::encodeEmpty(CH::MessageId::RoomOptions);
	const auto message = CH::ReceivedMessage::parse(data);
	ASSERT_TRUE(message.has_value());
	EXPECT_FALSE(message->payloadAs<CH::RoomOptionPayload>().has_value());
}
