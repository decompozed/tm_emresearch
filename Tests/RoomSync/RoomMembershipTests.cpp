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
#include <initializer_list>
#include "Rooms/RoomMembership.h"

namespace CH = Common::HaveApi;
using Cast::Domain::AttachVerdict;
using Cast::Domain::RoomMembership;

namespace
{
	constexpr std::uint16_t create = RoomMembership::createOrder;
	constexpr std::uint16_t join = RoomMembership::joinOrder;

	CH::RoomSyncMessage sync(std::uint32_t sequence, std::uint16_t host, std::initializer_list<CH::RoomSyncEntry> entries)
	{
		CH::RoomSyncMessage message{};
		message.header.roomNumber = 3;
		message.header.sequence = sequence;
		message.header.hostSession = host;
		for (const auto& entry : entries) message.entries[message.header.count++] = entry;
		return message;
	}
}

TEST(RoomMembership, SyncIsApplied)
{
	RoomMembership room;
	ASSERT_TRUE(room.apply(sync(1, 10, { { 10, 1, 0 }, { 20, 2, 0 }, { 30, 4, 1 } })));
	EXPECT_EQ(room.host(), 10u);
	EXPECT_TRUE(room.isHost(10));
	EXPECT_FALSE(room.isHost(20));
	EXPECT_TRUE(room.isAuthorizedMember(10));
	EXPECT_TRUE(room.isAuthorizedMember(20));
	EXPECT_TRUE(room.isAuthorizedMember(30));
	EXPECT_FALSE(room.isAuthorizedMember(40));
	ASSERT_NE(room.member(30), nullptr);
	EXPECT_EQ(room.member(30)->isObserver, 1);
	EXPECT_EQ(room.members().size(), 3u);
	EXPECT_EQ(room.lastSequence(), 1u);
}

TEST(RoomMembership, OlderSequenceIsIgnored)
{
	RoomMembership room;
	ASSERT_TRUE(room.apply(sync(5, 10, { { 10, 1, 0 }, { 20, 2, 0 } })));
	EXPECT_FALSE(room.apply(sync(4, 20, { { 20, 2, 0 } })));
	EXPECT_FALSE(room.apply(sync(5, 20, { { 20, 2, 0 } })));
	EXPECT_TRUE(room.isHost(10));
	EXPECT_TRUE(room.isAuthorizedMember(10));

	room.forgetSequence();
	EXPECT_TRUE(room.apply(sync(1, 20, { { 20, 2, 0 } })));
	EXPECT_TRUE(room.isHost(20));
	EXPECT_FALSE(room.isAuthorizedMember(10));
}

TEST(RoomMembership, HostChange)
{
	RoomMembership room;
	room.apply(sync(1, 10, { { 10, 1, 0 }, { 20, 2, 0 } }));
	ASSERT_TRUE(room.apply(sync(2, 20, { { 10, 1, 0 }, { 20, 2, 0 } })));
	EXPECT_FALSE(room.isHost(10));
	EXPECT_TRUE(room.isHost(20));
	EXPECT_EQ(room.host(), 20u);
}

TEST(RoomMembership, UnknownHostFailsClosed)
{
	RoomMembership room;
	room.apply(sync(1, 0, { { 10, 1, 0 }, { 20, 2, 0 } }));
	EXPECT_FALSE(room.host().has_value());
	EXPECT_FALSE(room.isHost(0));
	EXPECT_FALSE(room.isHost(10));
	EXPECT_FALSE(room.isHost(20));
	EXPECT_EQ(room.attachVerdict(10, create, 0, false), AttachVerdict::HostUnknown);
	EXPECT_EQ(room.attachVerdict(20, join, 10, false), AttachVerdict::HostUnknown);

	room.apply(sync(2, 10, { { 10, 1, 0 }, { 20, 2, 0 } }));
	room.invalidateHost();
	EXPECT_FALSE(room.host().has_value());
	EXPECT_FALSE(room.isHost(10));
	EXPECT_TRUE(room.isAuthorizedMember(10));

	room.apply(sync(3, 10, { { 10, 1, 0 }, { 20, 2, 0 } }));
	EXPECT_TRUE(room.isHost(10));
}

TEST(RoomMembership, DeclaredHostAndMembersAttach)
{
	RoomMembership room;
	room.apply(sync(1, 10, { { 10, 1, 0 }, { 20, 2, 0 }, { 30, 4, 1 } }));
	EXPECT_EQ(room.attachVerdict(10, create, 0, false), AttachVerdict::Attach);
	EXPECT_EQ(room.attachVerdict(20, join, 10, false), AttachVerdict::Attach);
	EXPECT_EQ(room.attachVerdict(30, join, 10, false), AttachVerdict::Attach);
}

TEST(RoomMembership, RepeatedJoinIsANoOp)
{
	RoomMembership room;
	room.apply(sync(1, 10, { { 10, 1, 0 }, { 20, 2, 0 } }));
	EXPECT_EQ(room.attachVerdict(20, join, 10, true), AttachVerdict::AlreadyAttached);
	EXPECT_EQ(room.attachVerdict(20, join, 10, true), AttachVerdict::AlreadyAttached);
	EXPECT_EQ(room.attachVerdict(10, create, 0, true), AttachVerdict::AlreadyAttached);
	EXPECT_TRUE(room.isAuthorizedMember(20));
}

TEST(RoomMembership, UnauthorizedCreateOrJoinIsRefused)
{
	RoomMembership room;
	room.apply(sync(1, 10, { { 10, 1, 0 }, { 20, 2, 0 } }));
	EXPECT_EQ(room.attachVerdict(20, create, 0, false), AttachVerdict::NotHost);
	EXPECT_EQ(room.attachVerdict(40, create, 0, false), AttachVerdict::NotMember);
	EXPECT_EQ(room.attachVerdict(40, join, 10, false), AttachVerdict::NotMember);
	EXPECT_EQ(room.attachVerdict(20, join, 20, false), AttachVerdict::WrongHost);
	EXPECT_EQ(room.attachVerdict(20, join, 99, false), AttachVerdict::WrongHost);
}

TEST(RoomMembership, RemovedMemberIsNoLongerAuthorized)
{
	RoomMembership room;
	room.apply(sync(1, 10, { { 10, 1, 0 }, { 20, 2, 0 } }));
	room.apply(sync(2, 10, { { 10, 1, 0 } }));
	EXPECT_FALSE(room.isAuthorizedMember(20));
	EXPECT_EQ(room.attachVerdict(20, join, 10, false), AttachVerdict::NotMember);
}
