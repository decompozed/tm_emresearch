#include <gtest/gtest.h>
#include "FakeWorld.h"

using namespace CombatTests;

namespace
{
	Frame launch(std::uint8_t extra, std::uint8_t slot, std::uint32_t owner, std::uint32_t ammoId)
	{
		ProjectileLaunchPacket packet{};
		packet.trace = trace();
		packet.owner = uid(owner);
		packet.ammoId = ammoId;
		Frame frame{ 272, extra, slot };
		frame.add(packet);
		return frame;
	}

	Frame reloadFrame(std::uint32_t player)
	{
		ReloadPacket packet{};
		packet.request.magazineAmmo = 30;
		packet.player = uid(player);
		Frame frame{ 275, 0, 0 };
		frame.add(packet);
		return frame;
	}
}

TEST(ProjectileLaunch, ForwardedAndOwnerRemembered)
{
	FakeWorld world;
	auto frame = launch(0, 2, 3, 77);
	EXPECT_EQ(projectileLaunch(world, frame.packet()).route, Route::Match);
	EXPECT_EQ(world.explosives[77], 3u);
}

TEST(ProjectileLaunch, SlotZeroDropped)
{
	FakeWorld world;
	auto frame = launch(0, 0, 3, 77);
	EXPECT_EQ(projectileLaunch(world, frame.packet()).route, Route::Drop);
}

TEST(ProjectileLaunch, SlotFiveDropped)
{
	FakeWorld world;
	auto frame = launch(0, 5, 3, 77);
	EXPECT_EQ(projectileLaunch(world, frame.packet()).route, Route::Drop);
}

TEST(ProjectileLaunch, UnknownExtraDropped)
{
	FakeWorld world;
	auto frame = launch(2, 1, 3, 77);
	EXPECT_EQ(projectileLaunch(world, frame.packet()).route, Route::Drop);
}

TEST(ProjectileLaunch, AmmoIdZeroOnlyWithExtraFour)
{
	FakeWorld world;
	auto withoutId = launch(4, 1, 3, 0);
	EXPECT_EQ(projectileLaunch(world, withoutId.packet()).route, Route::Match);
	auto missingId = launch(1, 1, 3, 0);
	EXPECT_EQ(projectileLaunch(world, missingId.packet()).route, Route::Drop);
	auto unexpectedId = launch(4, 1, 3, 9);
	EXPECT_EQ(projectileLaunch(world, unexpectedId.packet()).route, Route::Drop);
}

TEST(ProjectileLaunch, TraceTagOnlyForSlotOneExtraZero)
{
	FakeWorld world;
	const std::uint32_t tag = 12;
	auto allowed = launch(0, 1, 3, 77);
	std::memcpy(allowed.data.data(), &tag, sizeof(tag));
	EXPECT_EQ(projectileLaunch(world, allowed.packet()).route, Route::Match);
	auto refused = launch(0, 2, 3, 78);
	std::memcpy(refused.data.data(), &tag, sizeof(tag));
	EXPECT_EQ(projectileLaunch(world, refused.packet()).route, Route::Drop);
}

TEST(ProjectileLaunch, FromNonHostDropped)
{
	FakeWorld world;
	auto frame = launch(0, 2, 3, 77).from(3);
	EXPECT_EQ(projectileLaunch(world, frame.packet()).route, Route::Drop);
	EXPECT_TRUE(world.explosives.empty());
}

TEST(ProjectileLaunch, DeadOwnerForwarded)
{
	FakeWorld world;
	world.players[3].dead = true;
	auto frame = launch(0, 2, 3, 77);
	EXPECT_EQ(projectileLaunch(world, frame.packet()).route, Route::Match);
}

TEST(ProjectileLaunch, WrongSizeDropped)
{
	FakeWorld world;
	auto frame = launch(0, 2, 3, 77);
	frame.data.resize(20);
	EXPECT_EQ(projectileLaunch(world, frame.packet()).route, Route::Drop);
}

TEST(Reload, HostOutcomeGoesToWholeMatch)
{
	FakeWorld world;
	auto frame = reloadFrame(3);
	EXPECT_EQ(reload(world, frame.packet()).route, Route::Match);
}

TEST(Reload, FromNonHostDropped)
{
	FakeWorld world;
	auto frame = reloadFrame(3).from(3);
	EXPECT_EQ(reload(world, frame.packet()).route, Route::Drop);
}

TEST(Reload, WrongSizeDropped)
{
	FakeWorld world;
	auto frame = reloadFrame(3);
	frame.data.resize(4);
	EXPECT_EQ(reload(world, frame.packet()).route, Route::Drop);
}

TEST(Reload, PlayerOutsideRoomDropped)
{
	FakeWorld world;
	auto frame = reloadFrame(60);
	EXPECT_EQ(reload(world, frame.packet()).route, Route::Drop);
}

TEST(Reload, ExtraDropped)
{
	FakeWorld world;
	auto frame = reloadFrame(3);
	frame.extra = 1;
	EXPECT_EQ(reload(world, frame.packet()).route, Route::Drop);
}
