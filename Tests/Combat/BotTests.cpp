#include <bit>
#include <gtest/gtest.h>
#include "FakeWorld.h"
#include "Structures/Npc/NpcPackets.h"
#include "Structures/Respawn/RespawnPackets.h"

using namespace CombatTests;

namespace
{
	constexpr std::uint32_t botKey = 1000001;

	UniqueId bot(std::uint32_t key = botKey)
	{
		return std::bit_cast<UniqueId>(key);
	}

	Frame moveFrame(const UniqueId& unit, std::uint8_t pitch1 = 200, std::uint8_t pitch2 = 17)
	{
		BotMovePacket packet{};
		packet.position = half3();
		packet.velocity = half3(0, 0, 0);
		packet.timeMs = 123456;
		packet.state.animation = 32;
		packet.state.yawDegrees = 359;
		packet.bot = unit;
		Frame frame{ 285, pitch1, pitch2 };
		frame.add(packet);
		return frame;
	}

	Frame respawnFrame(const UniqueId& unit)
	{
		RespawnPacket packet{};
		packet.position = half3();
		packet.unit = unit;
		Frame frame{ 286, 0, 0 };
		frame.add(packet);
		return frame;
	}
}

TEST(BotMove, HostMoveGoesToWholeMatch)
{
	FakeWorld world;
	world.roomMode = Common::Enums::AiBattle;
	auto frame = moveFrame(bot());
	const auto verdict = botMove(world, frame.packet());
	EXPECT_EQ(verdict.route, Route::Match);
	EXPECT_EQ(verdict.option, 17);
}

TEST(BotMove, AcceptedInAnyMode)
{
	FakeWorld world;
	auto frame = moveFrame(bot());
	EXPECT_EQ(botMove(world, frame.packet()).route, Route::Match);
}

TEST(BotMove, FromNonHostDropped)
{
	FakeWorld world;
	auto frame = moveFrame(bot()).from(3);
	EXPECT_EQ(botMove(world, frame.packet()).route, Route::Drop);
}

TEST(BotMove, WrongSizeDropped)
{
	FakeWorld world;
	auto frame = moveFrame(bot());
	frame.data.resize(32);
	EXPECT_EQ(botMove(world, frame.packet()).route, Route::Drop);
}

TEST(BotMove, MissionDropped)
{
	FakeWorld world;
	auto frame = moveFrame(bot()).withMission(1);
	EXPECT_EQ(botMove(world, frame.packet()).route, Route::Drop);
}

TEST(BotMove, ReservedStateBitsDropped)
{
	FakeWorld world;
	auto frame = moveFrame(bot());
	frame.data[19] |= 0x80;
	EXPECT_EQ(botMove(world, frame.packet()).route, Route::Drop);
}

TEST(BotMove, PlayerKeyDropped)
{
	FakeWorld world;
	auto frame = moveFrame(uid(3));
	EXPECT_EQ(botMove(world, frame.packet()).route, Route::Drop);
}

TEST(BotMove, KeyBitThirtyOneDropped)
{
	FakeWorld world;
	auto frame = moveFrame(bot(botKey | 0x80000000u));
	EXPECT_EQ(botMove(world, frame.packet()).route, Route::Drop);
}

TEST(BotRespawn, HostRespawnGoesToWholeMatch)
{
	FakeWorld world;
	auto frame = respawnFrame(bot());
	EXPECT_EQ(botRespawn(world, frame.packet()).route, Route::Match);
	EXPECT_TRUE(world.respawns.empty());
}

TEST(BotRespawn, FromNonHostDropped)
{
	FakeWorld world;
	auto frame = respawnFrame(bot()).from(2);
	EXPECT_EQ(botRespawn(world, frame.packet()).route, Route::Drop);
}

TEST(BotRespawn, PlayerKeyDropped)
{
	FakeWorld world;
	world.players[3].dead = true;
	auto frame = respawnFrame(uid(3));
	EXPECT_EQ(botRespawn(world, frame.packet()).route, Route::Drop);
	EXPECT_TRUE(world.players[3].dead);
}

TEST(BotRespawn, NonZeroHeaderDropped)
{
	FakeWorld world;
	auto frame = respawnFrame(bot());
	frame.option = 1;
	EXPECT_EQ(botRespawn(world, frame.packet()).route, Route::Drop);
}

TEST(BotRespawn, WrongSizeDropped)
{
	FakeWorld world;
	auto frame = respawnFrame(bot());
	frame.data.resize(8);
	EXPECT_EQ(botRespawn(world, frame.packet()).route, Route::Drop);
}
