#include <gtest/gtest.h>
#include "FakeWorld.h"
#include "Structures/Respawn/RespawnPackets.h"

using namespace CombatTests;

namespace
{
	constexpr std::uint32_t requester = 3;

	Frame request(std::uint8_t mission, bool withPosition)
	{
		Frame frame{ 166, 0, 0 };
		frame.withMission(mission).from(requester);
		if (withPosition)
		{
			frame.add(RespawnRequest{ half3(), 0x3C00 });
		}
		return frame;
	}

	Frame respawnFrame(const UniqueId& unit, std::uint16_t order = 276)
	{
		RespawnPacket packet{};
		packet.position = half3();
		packet.yaw = 0x3C00;
		packet.unit = unit;
		Frame frame{ order, 0, 0 };
		frame.add(packet);
		return frame;
	}
}

TEST(RespawnRequest, HostPickedSpawnGoesToHost)
{
	FakeWorld world;
	auto frame = request(hostPickedSpawn, false);
	EXPECT_EQ(respawnRequest(world, frame.packet()).route, Route::Host);
}

TEST(RespawnRequest, ChosenSpawnGoesToHost)
{
	FakeWorld world;
	auto frame = request(chosenSpawn, true);
	EXPECT_EQ(respawnRequest(world, frame.packet()).route, Route::Host);
}

TEST(RespawnRequest, MissionAndSizeMustMatch)
{
	FakeWorld world;
	auto hostPickedWithPosition = request(hostPickedSpawn, true);
	EXPECT_EQ(respawnRequest(world, hostPickedWithPosition.packet()).route, Route::Drop);
	auto chosenWithoutPosition = request(chosenSpawn, false);
	EXPECT_EQ(respawnRequest(world, chosenWithoutPosition.packet()).route, Route::Drop);
}

TEST(RespawnRequest, MissionOneAndTwoDropped)
{
	FakeWorld world;
	auto one = request(1, true);
	EXPECT_EQ(respawnRequest(world, one.packet()).route, Route::Drop);
	auto two = request(2, true);
	EXPECT_EQ(respawnRequest(world, two.packet()).route, Route::Drop);
}

TEST(RespawnRequest, ExtraOrOptionDropped)
{
	FakeWorld world;
	auto extra = request(hostPickedSpawn, false);
	extra.extra = 1;
	EXPECT_EQ(respawnRequest(world, extra.packet()).route, Route::Drop);
	auto option = request(hostPickedSpawn, false);
	option.option = 1;
	EXPECT_EQ(respawnRequest(world, option.packet()).route, Route::Drop);
}

TEST(RespawnRequest, FromHostDropped)
{
	FakeWorld world;
	auto frame = request(hostPickedSpawn, false).from(hostSession);
	EXPECT_EQ(respawnRequest(world, frame.packet()).route, Route::Drop);
}

TEST(RespawnRequest, FromNonMemberDropped)
{
	FakeWorld world;
	auto frame = request(hostPickedSpawn, false).from(60);
	EXPECT_EQ(respawnRequest(world, frame.packet()).route, Route::Drop);
}

TEST(RespawnRequest, AliveOrLoadingSenderAccepted)
{
	FakeWorld world;
	world.players[requester].inMatch = false;
	auto frame = request(hostPickedSpawn, false);
	EXPECT_EQ(respawnRequest(world, frame.packet()).route, Route::Host);
}

TEST(RespawnRequest, DoesNotReviveTheSender)
{
	FakeWorld world;
	world.players[requester].dead = true;
	auto frame = request(hostPickedSpawn, false);
	EXPECT_EQ(respawnRequest(world, frame.packet()).route, Route::Host);
	EXPECT_TRUE(world.players[requester].dead);
}

TEST(PlayerRespawn, HostRespawnRevivesAndGoesToWholeMatch)
{
	FakeWorld world;
	world.players[requester].dead = true;
	auto frame = respawnFrame(uid(requester));
	EXPECT_EQ(playerRespawn(world, frame.packet()).route, Route::Match);
	EXPECT_FALSE(world.players[requester].dead);
	ASSERT_EQ(world.respawns.size(), 1u);
	EXPECT_EQ(world.respawns[0].host, hostSession);
	EXPECT_EQ(world.respawns[0].player, requester);
	EXPECT_EQ(world.respawns[0].position.y, half3().y);
}

TEST(PlayerRespawn, FirstSpawnMarksPlayerInMatch)
{
	FakeWorld world;
	world.players[requester] = PlayerState{ false, false, false };
	auto frame = respawnFrame(uid(requester));
	EXPECT_EQ(playerRespawn(world, frame.packet()).route, Route::Match);
	EXPECT_TRUE(world.players[requester].inMatch);
}

TEST(PlayerRespawn, HostOwnRespawnAccepted)
{
	FakeWorld world;
	auto frame = respawnFrame(uid(hostSession));
	EXPECT_EQ(playerRespawn(world, frame.packet()).route, Route::Match);
}

TEST(PlayerRespawn, NonFiniteYawAccepted)
{
	FakeWorld world;
	auto frame = respawnFrame(uid(requester));
	const std::uint16_t nan = 0xFFF6;
	std::memcpy(frame.data.data() + 6, &nan, sizeof(nan));
	EXPECT_EQ(playerRespawn(world, frame.packet()).route, Route::Match);
}

TEST(PlayerRespawn, FromNonHostDropped)
{
	FakeWorld world;
	world.players[requester].dead = true;
	auto frame = respawnFrame(uid(requester)).from(requester);
	EXPECT_EQ(playerRespawn(world, frame.packet()).route, Route::Drop);
	EXPECT_TRUE(world.players[requester].dead);
	EXPECT_TRUE(world.respawns.empty());
}

TEST(PlayerRespawn, WrongSizeDropped)
{
	FakeWorld world;
	auto empty = respawnFrame(uid(requester));
	empty.data.clear();
	EXPECT_EQ(playerRespawn(world, empty.packet()).route, Route::Drop);
	auto shorter = respawnFrame(uid(requester));
	shorter.data.resize(8);
	EXPECT_EQ(playerRespawn(world, shorter.packet()).route, Route::Drop);
	EXPECT_TRUE(world.respawns.empty());
}

TEST(PlayerRespawn, NonZeroHeaderDropped)
{
	FakeWorld world;
	auto mission = respawnFrame(uid(requester)).withMission(2);
	EXPECT_EQ(playerRespawn(world, mission.packet()).route, Route::Drop);
	auto option = respawnFrame(uid(requester));
	option.option = 100;
	EXPECT_EQ(playerRespawn(world, option.packet()).route, Route::Drop);
	auto extra = respawnFrame(uid(requester));
	extra.extra = 1;
	EXPECT_EQ(playerRespawn(world, extra.packet()).route, Route::Drop);
}

TEST(PlayerRespawn, HeaderKeptAsSent)
{
	FakeWorld world;
	auto frame = respawnFrame(uid(requester));
	const auto verdict = playerRespawn(world, frame.packet());
	EXPECT_EQ(verdict.option, 0);
	EXPECT_EQ(verdict.size, sizeof(RespawnPacket));
}

TEST(PlayerRespawn, KeyBitThirtyOneDropped)
{
	FakeWorld world;
	auto unit = uid(requester);
	unit.unknown = 1;
	auto frame = respawnFrame(unit);
	EXPECT_EQ(playerRespawn(world, frame.packet()).route, Route::Drop);
}

TEST(PlayerRespawn, UnitOutsideRoomDropped)
{
	FakeWorld world;
	auto stranger = respawnFrame(uid(60));
	EXPECT_EQ(playerRespawn(world, stranger.packet()).route, Route::Drop);
	auto otherServer = respawnFrame(uid(requester, server + 1));
	EXPECT_EQ(playerRespawn(world, otherServer.packet()).route, Route::Drop);
	EXPECT_TRUE(world.respawns.empty());
}
