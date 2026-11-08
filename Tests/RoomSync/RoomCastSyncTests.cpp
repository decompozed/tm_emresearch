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
#include <cstring>
#include <memory>
#include <utility>
#include <vector>
#include "Rooms/RoomCastSync.h"
#include "Structures/Room/RoomPlayerInfo.h"

namespace CH = Common::HaveApi;
using Main::Classes::RoomCastSync;
using Main::Classes::RoomCastSyncGuard;
using Main::Classes::makeRoomCastSnapshot;

namespace
{
	struct FakeSession
	{
		std::uint16_t id{};
		bool open{ true };
		bool is_open() const noexcept { return open; }
	};

	using Entry = std::pair<Main::Structures::RoomPlayerInfo, std::weak_ptr<FakeSession>>;

	class FakeRoom
	{
	public:
		std::vector<Entry> players;
		std::vector<Entry> observers;
		std::vector<CH::RoomSyncMessage> sent;
		bool linkUp{ true };

		RoomCastSync& castSync() noexcept { return m_castSync; }

		void flushCastSync(bool force = false)
		{
			m_castSync.flush(number, makeRoomCastSnapshot(players, observers, mode, map), force, [this](const CH::RoomSyncMessage& message) {
				if (!linkUp)
				{
					return false;
				}
				sent.push_back(message);
				return true;
			});
		}

		void activate()
		{
			m_castSync.activate();
			flushCastSync(true);
		}

		void add(const std::shared_ptr<FakeSession>& session, std::uint32_t team)
		{
			const RoomCastSyncGuard guard{ *this };
			players.emplace_back(info(*session, team), session);
		}

		void addObserver(const std::shared_ptr<FakeSession>& session)
		{
			const RoomCastSyncGuard guard{ *this };
			observers.emplace_back(info(*session, 4), session);
		}

		void remove(std::uint16_t session)
		{
			const RoomCastSyncGuard guard{ *this };
			std::erase_if(players, [&](const Entry& e) { return e.first.uniqueId.session == session; });
			std::erase_if(observers, [&](const Entry& e) { return e.first.uniqueId.session == session; });
		}

		void changeHost(std::size_t index)
		{
			const RoomCastSyncGuard guard{ *this };
			std::swap(players[0], players[index]);
		}

		void setTeam(std::uint16_t session, std::uint32_t team)
		{
			const RoomCastSyncGuard guard{ *this };
			for (auto& [info, weak] : players)
			{
				if (info.uniqueId.session == session) info.team = team;
			}
		}

		void moveToObservers(std::uint16_t session)
		{
			const RoomCastSyncGuard guard{ *this };
			const auto it = std::ranges::find_if(players, [&](const Entry& e) { return e.first.uniqueId.session == session; });
			auto entry = *it;
			entry.first.team = 4;
			players.erase(it);
			observers.push_back(entry);
		}

		void setMode(std::uint32_t newMode)
		{
			const RoomCastSyncGuard guard{ *this };
			mode = newMode;
		}

		void clear()
		{
			const RoomCastSyncGuard guard{ *this };
			players.clear();
			observers.clear();
		}

		void hostLeaves()
		{
			const RoomCastSyncGuard guard{ *this };
			const auto oldHost = static_cast<std::uint16_t>(players[0].first.uniqueId.session);
			changeHost(1);
			remove(oldHost);
		}

		std::uint32_t number{ 7 };
		std::uint32_t mode{ 3 };
		std::uint32_t map{ 25 };

	private:
		static Main::Structures::RoomPlayerInfo info(const FakeSession& session, std::uint32_t team)
		{
			Main::Structures::RoomPlayerInfo result{};
			result.uniqueId.session = session.id;
			result.team = team;
			return result;
		}

		RoomCastSync m_castSync{};
	};

	std::vector<CH::RoomSyncEntry> entriesOf(const CH::RoomSyncMessage& message)
	{
		return { message.entries.begin(), message.entries.begin() + message.header.count };
	}

	std::vector<std::uint8_t> bytesOf(const CH::RoomSyncMessage& message)
	{
		const auto* data = reinterpret_cast<const std::uint8_t*>(&message);
		return { data, data + message.byteSize() };
	}

	struct RoomCastSyncTest : ::testing::Test
	{
		FakeRoom room;
		std::shared_ptr<FakeSession> host = std::make_shared<FakeSession>(FakeSession{ 10 });
		std::shared_ptr<FakeSession> guest = std::make_shared<FakeSession>(FakeSession{ 20 });
		std::shared_ptr<FakeSession> third = std::make_shared<FakeSession>(FakeSession{ 30 });

		void SetUp() override
		{
			room.add(host, 1);
			room.activate();
			room.sent.clear();
		}
	};
}

TEST(RoomCastSync, NothingIsSentBeforeTheRoomIsRegistered)
{
	FakeRoom room;
	auto host = std::make_shared<FakeSession>(FakeSession{ 3 });
	room.add(host, 0);
	room.flushCastSync();
	EXPECT_TRUE(room.sent.empty());

	room.activate();
	ASSERT_EQ(room.sent.size(), 1u);
	EXPECT_EQ(room.sent[0].header.roomNumber, 7u);
	EXPECT_EQ(room.sent[0].header.hostSession, 3);
	EXPECT_EQ(room.sent[0].header.mode, 3);
	EXPECT_EQ(room.sent[0].header.map, 25);
	EXPECT_EQ(entriesOf(room.sent[0]), (std::vector<CH::RoomSyncEntry>{ { 3, 0, 0 } }));
}

TEST_F(RoomCastSyncTest, JoinSendsTheMembers)
{
	room.add(guest, 2);
	ASSERT_EQ(room.sent.size(), 1u);
	EXPECT_EQ(room.sent[0].header.hostSession, 10);
	EXPECT_EQ(entriesOf(room.sent[0]), (std::vector<CH::RoomSyncEntry>{ { 10, 1, 0 }, { 20, 2, 0 } }));
}

TEST_F(RoomCastSyncTest, ObserverJoinIsFlagged)
{
	room.addObserver(guest);
	ASSERT_EQ(room.sent.size(), 1u);
	EXPECT_EQ(entriesOf(room.sent[0]), (std::vector<CH::RoomSyncEntry>{ { 10, 1, 0 }, { 20, 4, 1 } }));
}

TEST_F(RoomCastSyncTest, LeaveSendsTheRemainingMembers)
{
	room.add(guest, 2);
	room.remove(20);
	ASSERT_EQ(room.sent.size(), 2u);
	EXPECT_EQ(entriesOf(room.sent[1]), (std::vector<CH::RoomSyncEntry>{ { 10, 1, 0 } }));
}

TEST_F(RoomCastSyncTest, HostChangeIsSent)
{
	room.add(guest, 2);
	room.changeHost(1);
	ASSERT_EQ(room.sent.size(), 2u);
	EXPECT_EQ(room.sent[1].header.hostSession, 20);
	EXPECT_EQ(room.sent[1].header.count, 2);
}

TEST_F(RoomCastSyncTest, TeamChangeIsSent)
{
	room.add(guest, 2);
	room.setTeam(20, 1);
	ASSERT_EQ(room.sent.size(), 2u);
	EXPECT_EQ(entriesOf(room.sent[1]), (std::vector<CH::RoomSyncEntry>{ { 10, 1, 0 }, { 20, 1, 0 } }));
}

TEST_F(RoomCastSyncTest, MoveToObserversIsSent)
{
	room.add(guest, 2);
	room.moveToObservers(20);
	ASSERT_EQ(room.sent.size(), 2u);
	EXPECT_EQ(entriesOf(room.sent[1]), (std::vector<CH::RoomSyncEntry>{ { 10, 1, 0 }, { 20, 4, 1 } }));
}

TEST_F(RoomCastSyncTest, ModeChangeIsSent)
{
	room.add(guest, 2);
	room.setMode(12);
	ASSERT_EQ(room.sent.size(), 2u);
	EXPECT_EQ(room.sent[1].header.mode, 12);
	EXPECT_EQ(room.sent[1].header.map, 25);
	EXPECT_EQ(entriesOf(room.sent[1]), entriesOf(room.sent[0]));
}

TEST_F(RoomCastSyncTest, PeriodicCheckSendsAMapChange)
{
	room.map = 40;
	room.flushCastSync();
	ASSERT_EQ(room.sent.size(), 1u);
	EXPECT_EQ(room.sent[0].header.map, 40);
	EXPECT_EQ(room.sent[0].header.hostSession, 10);

	room.flushCastSync();
	EXPECT_EQ(room.sent.size(), 1u);
}

TEST_F(RoomCastSyncTest, ResyncCarriesModeAndMap)
{
	room.mode = 16;
	room.map = 7;
	room.flushCastSync(true);
	room.flushCastSync(true);
	ASSERT_EQ(room.sent.size(), 2u);
	EXPECT_EQ(room.sent[1].header.mode, 16);
	EXPECT_EQ(room.sent[1].header.map, 7);
}

TEST_F(RoomCastSyncTest, ClosingSendsAnEmptyRoom)
{
	room.add(guest, 2);
	room.clear();
	ASSERT_EQ(room.sent.size(), 2u);
	EXPECT_EQ(room.sent[1].header.count, 0);
	EXPECT_EQ(room.sent[1].header.hostSession, 0);
	EXPECT_EQ(room.sent[1].byteSize(), sizeof(CH::RoomSyncHeader));
}

TEST_F(RoomCastSyncTest, NestedChangesSendOneMessage)
{
	room.add(guest, 2);
	room.add(third, 1);
	room.sent.clear();
	room.hostLeaves();
	ASSERT_EQ(room.sent.size(), 1u);
	EXPECT_EQ(room.sent[0].header.hostSession, 20);
	EXPECT_EQ(entriesOf(room.sent[0]), (std::vector<CH::RoomSyncEntry>{ { 20, 2, 0 }, { 30, 1, 0 } }));
}

TEST_F(RoomCastSyncTest, NoChangeNoMessage)
{
	room.add(guest, 2);
	room.add(third, 1);
	room.sent.clear();
	room.setTeam(20, 2);
	room.changeHost(0);
	std::swap(room.players[1], room.players[2]);
	room.flushCastSync();
	EXPECT_TRUE(room.sent.empty());
}

TEST_F(RoomCastSyncTest, SequenceGrowsWithEveryMessage)
{
	room.add(guest, 2);
	room.remove(20);
	room.flushCastSync(true);
	ASSERT_EQ(room.sent.size(), 3u);
	EXPECT_LT(room.sent[0].header.sequence, room.sent[1].header.sequence);
	EXPECT_LT(room.sent[1].header.sequence, room.sent[2].header.sequence);
}

TEST_F(RoomCastSyncTest, PeriodicCheckDropsAnExpiredHost)
{
	room.add(guest, 2);
	room.sent.clear();
	host.reset();
	room.flushCastSync();
	ASSERT_EQ(room.sent.size(), 1u);
	EXPECT_EQ(room.sent[0].header.hostSession, 0);
	EXPECT_EQ(entriesOf(room.sent[0]), (std::vector<CH::RoomSyncEntry>{ { 20, 2, 0 } }));

	room.flushCastSync();
	EXPECT_EQ(room.sent.size(), 1u);
}

TEST_F(RoomCastSyncTest, PeriodicCheckDropsAClosedSocket)
{
	room.add(guest, 2);
	room.sent.clear();
	guest->open = false;
	room.flushCastSync();
	ASSERT_EQ(room.sent.size(), 1u);
	EXPECT_EQ(room.sent[0].header.hostSession, 10);
	EXPECT_EQ(entriesOf(room.sent[0]), (std::vector<CH::RoomSyncEntry>{ { 10, 1, 0 } }));
}

TEST_F(RoomCastSyncTest, StateTheLinkDidNotTakeIsSentAgain)
{
	room.linkUp = false;
	room.add(guest, 2);
	EXPECT_TRUE(room.sent.empty());
	room.linkUp = true;
	room.flushCastSync();
	ASSERT_EQ(room.sent.size(), 1u);
	EXPECT_EQ(room.sent[0].header.count, 2);
}

TEST(RoomSyncCodec, MessageRoundTrips)
{
	Main::Classes::RoomCastSnapshot snapshot{};
	snapshot.host = 5;
	snapshot.count = 3;
	snapshot.mode = 9;
	snapshot.map = 300;
	snapshot.entries[0] = { 5, 1, 0 };
	snapshot.entries[1] = { 6, 2, 0 };
	snapshot.entries[2] = { 9, 4, 1 };
	const auto message = RoomCastSync::makeMessage(42, snapshot);
	const auto bytes = bytesOf(message);
	ASSERT_EQ(bytes.size(), 16u + 3 * 4);
	EXPECT_EQ(bytes[12], 9);
	EXPECT_EQ(bytes[13], 0);
	EXPECT_EQ(bytes[14], 300 & 0xFF);
	EXPECT_EQ(bytes[15], 300 >> 8);

	const auto decoded = CH::decodeRoomSync(bytes.data(), bytes.size());
	ASSERT_TRUE(decoded.has_value());
	EXPECT_EQ(decoded->header.roomNumber, 42u);
	EXPECT_EQ(decoded->header.sequence, message.header.sequence);
	EXPECT_EQ(decoded->header.hostSession, 5);
	EXPECT_EQ(decoded->header.mode, 9);
	EXPECT_EQ(decoded->header.map, 300);
	EXPECT_EQ(entriesOf(*decoded), entriesOf(message));
}

TEST(RoomSyncCodec, FullSyncEndDecodes)
{
	const auto message = RoomCastSync::makeMessage(0, {}, CH::RoomSyncFlags::FullSyncEnd);
	const auto bytes = bytesOf(message);
	const auto decoded = CH::decodeRoomSync(bytes.data(), bytes.size());
	ASSERT_TRUE(decoded.has_value());
	EXPECT_TRUE(decoded->header.flags & CH::RoomSyncFlags::FullSyncEnd);
}

TEST(RoomSyncCodec, MalformedMessagesAreRejected)
{
	const auto encode = [](std::uint32_t room, std::uint16_t host, std::vector<CH::RoomSyncEntry> entries, std::uint8_t flags = 0,
		std::uint16_t mode = 0, std::uint16_t map = 0) {
		CH::RoomSyncMessage message{};
		message.header = { room, 1, host, static_cast<std::uint8_t>(entries.size()), flags, mode, map };
		std::ranges::copy(entries, message.entries.begin());
		return bytesOf(message);
	};
	const auto decodes = [](const std::vector<std::uint8_t>& bytes) { return CH::decodeRoomSync(bytes.data(), bytes.size()).has_value(); };

	EXPECT_TRUE(decodes(encode(1, 5, { { 5, 1, 0 } })));
	EXPECT_TRUE(decodes(encode(1, 0, { { 5, 1, 0 } })));
	EXPECT_TRUE(decodes(encode(1, 0, {})));
	EXPECT_TRUE(decodes(encode(1, 5, { { 5, 1, 0 } }, 0, CH::maxRoomSyncMode, 0xFFFF)));

	auto truncated = encode(1, 5, { { 5, 1, 0 } });
	truncated.pop_back();
	EXPECT_FALSE(decodes(truncated));
	auto padded = encode(1, 5, { { 5, 1, 0 } });
	padded.push_back(0);
	EXPECT_FALSE(decodes(padded));
	EXPECT_FALSE(decodes({ 1, 2, 3 }));
	EXPECT_FALSE(decodes(encode(1, 6, { { 5, 1, 0 } })));
	EXPECT_FALSE(decodes(encode(1, 5, { { 5, 4, 1 } })));
	EXPECT_FALSE(decodes(encode(1, 5, { { 5, 1, 0 }, { 5, 2, 0 } })));
	EXPECT_FALSE(decodes(encode(1, 0, { { 0, 1, 0 } })));
	EXPECT_FALSE(decodes(encode(1, 0, { { 501, 1, 0 } })));
	EXPECT_FALSE(decodes(encode(1, 0, { { 5, 1, 2 } })));
	EXPECT_FALSE(decodes(encode(0, 0, { { 5, 1, 0 } })));
	EXPECT_FALSE(decodes(encode(1, 0, {}, CH::RoomSyncFlags::FullSyncEnd)));
	EXPECT_FALSE(decodes(encode(1, 0, {}, 2)));
	EXPECT_FALSE(decodes(encode(1, 5, { { 5, 1, 0 } }, 0, CH::maxRoomSyncMode + 1)));
	EXPECT_FALSE(decodes(encode(0, 0, {}, CH::RoomSyncFlags::FullSyncEnd, 1)));
	EXPECT_FALSE(decodes(encode(0, 0, {}, CH::RoomSyncFlags::FullSyncEnd, 0, 1)));
	EXPECT_TRUE(decodes(encode(0, 0, {}, CH::RoomSyncFlags::FullSyncEnd)));

	CH::RoomSyncMessage tooMany{};
	tooMany.header = { 1, 1, 0, static_cast<std::uint8_t>(CH::maxRoomSyncEntries + 1), 0, 0, 0 };
	std::vector<std::uint8_t> bytes(sizeof(CH::RoomSyncHeader) + tooMany.header.count * sizeof(CH::RoomSyncEntry));
	std::memcpy(bytes.data(), &tooMany.header, sizeof(tooMany.header));
	EXPECT_FALSE(decodes(bytes));
}
