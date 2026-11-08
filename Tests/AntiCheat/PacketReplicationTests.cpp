#include <gtest/gtest.h>
#include "AntiCheat/Checks/PacketReplicaCheck.h"
#include "AntiCheat/Checks/PacketFloodingCheck.h"

#include <type_traits>

namespace
{
	constexpr std::uint32_t sessionId = 12;
	constexpr std::uint32_t accountId = 3456;
	constexpr std::uint16_t order = 254;

	std::optional<Ac::ACFlag> send(Ac::PacketReplicationChecker& checker, std::uint64_t at, std::vector<std::uint8_t> data = { 1, 2, 3, 4 },
		std::uint32_t session = sessionId)
	{
		const Ac::PacketReplicationEvent event{ session, accountId, order, std::move(data), at };
		return checker.processEventBase(event);
	}
}

TEST(PacketReplication, EventsCarryNoSession)
{
	static_assert(std::is_same_v<decltype(Ac::PacketReplicationEvent::sessionId), std::uint32_t>);
	static_assert(std::is_same_v<decltype(Ac::PacketFloodingEvent::sessionId), std::uint32_t>);
	SUCCEED();
}

TEST(PacketReplication, FiveIdenticalPacketsInTheWindowFlagTheAccount)
{
	Ac::PacketReplicationChecker checker;
	for (std::uint64_t i = 0; i < Ac::PacketReplicationChecker::replicaThreshold - 1; ++i)
		EXPECT_FALSE(send(checker, 1000 + i * 1000));

	const auto flag = send(checker, 5000);
	ASSERT_TRUE(flag);
	EXPECT_EQ(flag->accountId, accountId);
	EXPECT_NE(flag->details.find("ID 254"), std::string::npos);
	EXPECT_NE(flag->details.find("5 identical packets"), std::string::npos);
}

TEST(PacketReplication, CountingRestartsAfterAFlag)
{
	Ac::PacketReplicationChecker checker;
	for (std::uint64_t i = 0; i < Ac::PacketReplicationChecker::replicaThreshold; ++i)
		send(checker, i);
	for (std::uint64_t i = 0; i < Ac::PacketReplicationChecker::replicaThreshold - 1; ++i)
		EXPECT_FALSE(send(checker, 10 + i));
	EXPECT_TRUE(send(checker, 20));
}

TEST(PacketReplication, PacketsOutsideTheWindowDoNotCount)
{
	Ac::PacketReplicationChecker checker;
	constexpr std::uint64_t step = Ac::PacketReplicationChecker::analysisWindowMs / 4 + 1;
	for (std::uint64_t i = 0; i < 10; ++i)
		EXPECT_FALSE(send(checker, i * step));
}

TEST(PacketReplication, DifferentPacketsDoNotCount)
{
	Ac::PacketReplicationChecker checker;
	for (std::uint8_t i = 0; i < 10; ++i)
		EXPECT_FALSE(send(checker, i, { 1, 2, 3, i }));
}

TEST(PacketReplication, SessionsAreCountedApart)
{
	Ac::PacketReplicationChecker checker;
	for (std::uint32_t i = 0; i < 4; ++i)
	{
		EXPECT_FALSE(send(checker, i, { 9 }, sessionId));
		EXPECT_FALSE(send(checker, i, { 9 }, sessionId + 1));
	}
	EXPECT_EQ(checker.trackedSessions(), 2u);
}

TEST(PacketReplication, FlaggedSessionIsForgotten)
{
	Ac::PacketReplicationChecker checker;
	for (std::uint64_t i = 0; i < Ac::PacketReplicationChecker::replicaThreshold; ++i)
		send(checker, i);
	EXPECT_EQ(checker.trackedSessions(), 0u);
}

TEST(PacketReplication, OtherEventTypesAreIgnored)
{
	Ac::PacketReplicationChecker checker;
	const Ac::PacketFloodingEvent flood{ sessionId, accountId, 1, 1000, "flood", order, 0 };
	for (int i = 0; i < 10; ++i)
		EXPECT_FALSE(checker.processEventBase(flood));
}

TEST(PacketFlood, MoreThanTheLimitInTheWindowFlags)
{
	Ac::PacketFloodChecker checker;
	const auto at = [](std::uint64_t time) { return Ac::PacketFloodingEvent{ sessionId, accountId, 3, 1000, "flood", order, time }; };
	EXPECT_FALSE(checker.processEventBase(at(0)));
	EXPECT_FALSE(checker.processEventBase(at(100)));
	EXPECT_FALSE(checker.processEventBase(at(200)));
	const auto flag = checker.processEventBase(at(300));
	ASSERT_TRUE(flag);
	EXPECT_EQ(flag->accountId, accountId);
	EXPECT_EQ(flag->cheatType, "flood");
	EXPECT_FALSE(checker.processEventBase(at(5000)));
}
