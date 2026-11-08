#include <gtest/gtest.h>
#include "FakeWorld.h"
#include "Structures/Items/BombPackets.h"

using namespace CombatTests;

namespace
{
	constexpr std::uint32_t requester = 2;

	BombActionRequest actionRequest()
	{
		return BombActionRequest{ 0, half3(), half3(0x3C00, 0, 0) };
	}

	Frame request(std::uint8_t mission, std::uint8_t extra)
	{
		Frame frame{ 155, extra, 0 };
		frame.withMission(mission).from(requester).add(actionRequest());
		return frame;
	}

	BombActionPacket actionPacket(const UniqueId& player)
	{
		BombActionPacket packet{};
		packet.request = actionRequest();
		packet.player = player;
		return packet;
	}

	Frame action(std::uint8_t mission, std::uint8_t extra, const UniqueId& player)
	{
		Frame frame{ 271, extra, 0 };
		frame.withMission(mission).add(actionPacket(player));
		return frame;
	}

	Frame dropRequest()
	{
		Frame frame{ 156, 0, 0 };
		frame.from(requester);
		return frame;
	}
}

TEST(BombActionRequest, EveryClientVariantGoesToHost)
{
	FakeWorld world;
	for (const std::uint8_t mission : { bombDefuse, bombPlant })
	{
		for (const std::uint8_t extra : { bombActionCancel, bombActionStart, bombActionComplete })
		{
			auto frame = request(mission, extra);
			EXPECT_EQ(bombActionRequest(world, frame.packet()).route, Route::Host);
		}
	}
}

TEST(BombActionRequest, MalformedDropped)
{
	FakeWorld world;
	auto mission = request(2, bombActionStart);
	EXPECT_EQ(bombActionRequest(world, mission.packet()).route, Route::Drop);
	auto extra = request(bombPlant, 34);
	EXPECT_EQ(bombActionRequest(world, extra.packet()).route, Route::Drop);
	auto option = request(bombPlant, bombActionStart);
	option.option = 1;
	EXPECT_EQ(bombActionRequest(world, option.packet()).route, Route::Drop);
	auto shorter = request(bombPlant, bombActionStart);
	shorter.data.resize(12);
	EXPECT_EQ(bombActionRequest(world, shorter.packet()).route, Route::Drop);
	auto reserved = request(bombPlant, bombActionStart);
	reserved.data[0] = 1;
	EXPECT_EQ(bombActionRequest(world, reserved.packet()).route, Route::Drop);
}

TEST(BombActionRequest, HostOrObserverDroppedDeadSenderGoesToHost)
{
	FakeWorld world;
	auto host = request(bombPlant, bombActionStart).from(hostSession);
	EXPECT_EQ(bombActionRequest(world, host.packet()).route, Route::Drop);
	world.players[requester].dead = true;
	auto dead = request(bombPlant, bombActionStart);
	EXPECT_EQ(bombActionRequest(world, dead.packet()).route, Route::Host);
	world.players[requester] = PlayerState{ true, true, false };
	auto observer = request(bombPlant, bombActionStart);
	EXPECT_EQ(bombActionRequest(world, observer.packet()).route, Route::Drop);
}

TEST(BombAction, HostOutcomeGoesToWholeMatch)
{
	FakeWorld world;
	auto plant = action(bombPlant, bombActionComplete, uid(requester));
	EXPECT_EQ(bombAction(world, plant.packet()).route, Route::Match);
	auto defuse = action(bombDefuse, bombActionCancel, uid(hostSession));
	EXPECT_EQ(bombAction(world, defuse.packet()).route, Route::Match);
}

TEST(BombAction, MalformedOrForeignDropped)
{
	FakeWorld world;
	auto fromPlayer = action(bombPlant, bombActionStart, uid(requester)).from(requester);
	EXPECT_EQ(bombAction(world, fromPlayer.packet()).route, Route::Drop);
	auto extra = action(bombPlant, 0, uid(requester));
	EXPECT_EQ(bombAction(world, extra.packet()).route, Route::Drop);
	auto mission = action(3, bombActionStart, uid(requester));
	EXPECT_EQ(bombAction(world, mission.packet()).route, Route::Drop);
	auto shorter = action(bombPlant, bombActionStart, uid(requester));
	shorter.data.resize(20);
	EXPECT_EQ(bombAction(world, shorter.packet()).route, Route::Drop);
	auto padding = action(bombPlant, bombActionStart, uid(requester));
	padding.data[31] = 1;
	EXPECT_EQ(bombAction(world, padding.packet()).route, Route::Drop);
	auto reserved = action(bombPlant, bombActionStart, uid(requester));
	reserved.data[2] = 1;
	EXPECT_EQ(bombAction(world, reserved.packet()).route, Route::Drop);
	auto stranger = action(bombPlant, bombActionStart, uid(60));
	EXPECT_EQ(bombAction(world, stranger.packet()).route, Route::Drop);
}

TEST(BombDropRequest, GoesToHost)
{
	FakeWorld world;
	auto frame = dropRequest();
	EXPECT_EQ(bombDropRequest(world, frame.packet()).route, Route::Host);
}

TEST(BombDropRequest, MalformedOrUnfitSenderDropped)
{
	FakeWorld world;
	auto payload = dropRequest().add(std::uint32_t{ 1 });
	EXPECT_EQ(bombDropRequest(world, payload.packet()).route, Route::Drop);
	auto extra = dropRequest();
	extra.extra = 1;
	EXPECT_EQ(bombDropRequest(world, extra.packet()).route, Route::Drop);
	auto mission = dropRequest().withMission(1);
	EXPECT_EQ(bombDropRequest(world, mission.packet()).route, Route::Drop);
	auto host = dropRequest().from(hostSession);
	EXPECT_EQ(bombDropRequest(world, host.packet()).route, Route::Drop);
	world.players[requester].dead = true;
	auto dead = dropRequest();
	EXPECT_EQ(bombDropRequest(world, dead.packet()).route, Route::Host);
}
