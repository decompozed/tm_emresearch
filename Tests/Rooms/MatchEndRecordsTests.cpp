#include <gtest/gtest.h>
#include "Handlers/Room/MatchEndRecords.h"
#include "Structures/Match/EndScoreboard.h"
#include "Enums/GameEnums.h"
#include "Structures/ClientData/Structures.h"

#include <set>

using Main::Structures::ClientEndingMatch;
using Main::Structures::UniqueId;

namespace
{
	constexpr std::size_t headerSize = 4;

	ClientEndingMatch score(std::uint32_t session, std::uint32_t kills)
	{
		ClientEndingMatch record{};
		record.uniqueId.session = session;
		record.uniqueId.server = 1;
		record.totalKills = kills;
		return record;
	}

	template<typename Record>
	std::vector<std::uint8_t> payload(const std::vector<Record>& records, std::size_t header = headerSize)
	{
		std::vector<std::uint8_t> bytes(header, 0);
		for (const auto& record : records)
		{
			const auto raw = std::bit_cast<std::array<std::uint8_t, sizeof(Record)>>(record);
			bytes.insert(bytes.end(), raw.begin(), raw.end());
		}
		return bytes;
	}

	struct Room
	{
		std::set<std::uint32_t> members;
		bool operator()(const UniqueId& id) const { return members.contains(id.session); }
	};
}

TEST(MatchEndRecords, RecordLayoutIsTheClients)
{
	static_assert(sizeof(ClientEndingMatch) == 20);
	SUCCEED();
}

TEST(MatchEndRecords, EveryMemberOnce)
{
	const auto bytes = payload(std::vector{ score(1, 4), score(2, 7), score(3, 0) });
	const auto result = Main::Handlers::readMatchEndRecords<ClientEndingMatch>(bytes, headerSize, 3, Room{ { 1, 2, 3 } });
	ASSERT_EQ(result.players.size(), 3u);
	EXPECT_TRUE(result.clean());
	EXPECT_EQ(result.players[1].uniqueId.session, 2u);
	EXPECT_EQ(result.players[1].totalKills, 7u);
}

TEST(MatchEndRecords, DuplicatedPlayerRewardedOnce)
{
	const auto bytes = payload(std::vector{ score(1, 4), score(2, 7), score(1, 50), score(1, 50) });
	const auto result = Main::Handlers::readMatchEndRecords<ClientEndingMatch>(bytes, headerSize, 4, Room{ { 1, 2 } });
	ASSERT_EQ(result.players.size(), 2u);
	EXPECT_EQ(result.duplicates, 2u);
	EXPECT_EQ(result.players[0].totalKills, 4u);
	EXPECT_FALSE(result.clean());
}

TEST(MatchEndRecords, SameSessionOnAnotherServerIsADuplicate)
{
	auto other = score(1, 9);
	other.uniqueId.server = 2;
	const auto bytes = payload(std::vector{ score(1, 4), other });
	const auto result = Main::Handlers::readMatchEndRecords<ClientEndingMatch>(bytes, headerSize, 2, Room{ { 1 } });
	EXPECT_EQ(result.players.size(), 1u);
	EXPECT_EQ(result.duplicates, 1u);
}

TEST(MatchEndRecords, PlayersOutsideTheRoomSkipped)
{
	const auto bytes = payload(std::vector{ score(1, 4), score(8, 7) });
	const auto result = Main::Handlers::readMatchEndRecords<ClientEndingMatch>(bytes, headerSize, 2, Room{ { 1 } });
	ASSERT_EQ(result.players.size(), 1u);
	EXPECT_EQ(result.strangers, 1u);
	EXPECT_EQ(result.players[0].uniqueId.session, 1u);
}

TEST(MatchEndRecords, DeclaredCountBeyondThePacketIsClamped)
{
	const auto bytes = payload(std::vector{ score(1, 4), score(2, 7) });
	const auto result = Main::Handlers::readMatchEndRecords<ClientEndingMatch>(bytes, headerSize, 255, Room{ { 1, 2 } });
	EXPECT_EQ(result.players.size(), 2u);
	EXPECT_EQ(result.missing, 253u);
}

TEST(MatchEndRecords, TrailingPartialRecordIgnored)
{
	auto bytes = payload(std::vector{ score(1, 4) });
	bytes.resize(bytes.size() + sizeof(ClientEndingMatch) - 1, 0xFF);
	const auto result = Main::Handlers::readMatchEndRecords<ClientEndingMatch>(bytes, headerSize, 2, Room{ { 1 } });
	EXPECT_EQ(result.players.size(), 1u);
	EXPECT_EQ(result.missing, 1u);
}

TEST(MatchEndRecords, ShortPacketHasNoRecords)
{
	const std::vector<std::uint8_t> bytes(2, 0);
	const auto result = Main::Handlers::readMatchEndRecords<ClientEndingMatch>(bytes, headerSize, 3, Room{ { 1 } });
	EXPECT_TRUE(result.players.empty());
	EXPECT_EQ(result.missing, 3u);
}

TEST(MatchEndRecords, ScrimmageRecords)
{
	using Record = Main::ClientData::ScrimmageEndingPlayer;
	Record a{};
	a.uniqueId.session = 4;
	a.damage = 1200;
	Record b{};
	b.uniqueId.session = 4;
	const auto bytes = payload(std::vector{ a, b }, sizeof(Main::ClientData::ScrimmageEndingHeader));
	const auto result = Main::Handlers::readMatchEndRecords<Record>(bytes, sizeof(Main::ClientData::ScrimmageEndingHeader), 2, Room{ { 4 } });
	ASSERT_EQ(result.players.size(), 1u);
	EXPECT_EQ(result.players[0].damage, 1200u);
	EXPECT_EQ(result.duplicates, 1u);
}
