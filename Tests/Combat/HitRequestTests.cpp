#include <gtest/gtest.h>
#include "FakeWorld.h"

using namespace CombatTests;

namespace
{
	constexpr std::uint32_t requester = 3;

	HitRequestEntry hitEntry(std::uint32_t target)
	{
		HitRequestEntry value{};
		value.target = uid(target);
		value.hitPoint = half3();
		value.hitPart = 1;
		return value;
	}

	Frame singleRequest(std::uint16_t order, std::uint32_t target, std::uint8_t option = 0)
	{
		HitRequest request{};
		request.trace = trace();
		request.hit = hitEntry(target);
		Frame frame{ order, 1, option };
		frame.add(request).from(requester);
		return frame;
	}

	Frame multiRequest(std::uint16_t order, std::initializer_list<std::uint32_t> targets)
	{
		Frame frame{ order, 1, static_cast<std::uint8_t>(targets.size()) };
		frame.add(trace()).from(requester);
		for (const auto target : targets)
		{
			frame.add(hitEntry(target));
		}
		return frame;
	}

	Frame reloadRequestFrame()
	{
		ReloadRequest request{};
		request.magazineAmmo = 30;
		Frame frame{ 165, 0, 0 };
		frame.add(request).from(requester);
		return frame;
	}
}

TEST(ReloadRequest, GoesToTheHost)
{
	FakeWorld world;
	auto frame = reloadRequestFrame();
	EXPECT_EQ(reloadRequest(world, frame.packet()).route, Route::Host);
}

TEST(ReloadRequest, WrongSizeDropped)
{
	FakeWorld world;
	auto frame = reloadRequestFrame();
	frame.data.push_back(0);
	EXPECT_EQ(reloadRequest(world, frame.packet()).route, Route::Drop);
}

TEST(ReloadRequest, FromTheHostDropped)
{
	FakeWorld world;
	auto frame = reloadRequestFrame().from(hostSession);
	EXPECT_EQ(reloadRequest(world, frame.packet()).route, Route::Drop);
}

TEST(ReloadRequest, MissionDropped)
{
	FakeWorld world;
	auto frame = reloadRequestFrame().withMission(1);
	EXPECT_EQ(reloadRequest(world, frame.packet()).route, Route::Drop);
}

TEST(ReloadRequest, ObserverDropped)
{
	FakeWorld world;
	world.players[requester].observer = true;
	auto frame = reloadRequestFrame();
	EXPECT_EQ(reloadRequest(world, frame.packet()).route, Route::Drop);
}

TEST(ReloadRequest, DeadSenderGoesToHost)
{
	FakeWorld world;
	world.players[requester].dead = true;
	auto frame = reloadRequestFrame();
	EXPECT_EQ(reloadRequest(world, frame.packet()).route, Route::Host);
}

TEST(ReloadRequest, NonMemberDropped)
{
	FakeWorld world;
	auto frame = reloadRequestFrame().from(40);
	EXPECT_EQ(reloadRequest(world, frame.packet()).route, Route::Drop);
}

TEST(ItemDamageRequest, GoesToTheHost)
{
	FakeWorld world;
	ItemDamageRequest request{};
	request.target = uid(2);
	request.itemId = 55;
	Frame frame{ 146, 0, 0 };
	frame.add(request).from(requester);
	EXPECT_EQ(itemDamageRequest(world, frame.packet()).route, Route::Host);
	frame.data.resize(4);
	EXPECT_EQ(itemDamageRequest(world, frame.packet()).route, Route::Drop);
}

TEST(MultiHitRequest, GoesToTheHost)
{
	FakeWorld world;
	auto frame = multiRequest(147, { 2, 4, 4 });
	EXPECT_EQ(multiHitRequest(world, frame.packet()).route, Route::Host);
}

TEST(MultiHitRequest, SizeNotMatchingOptionDropped)
{
	FakeWorld world;
	auto frame = multiRequest(153, { 2, 4 });
	frame.option = 3;
	EXPECT_EQ(multiHitRequest(world, frame.packet()).route, Route::Drop);
}

TEST(MultiHitRequest, OptionZeroDropped)
{
	FakeWorld world;
	auto frame = multiRequest(153, {});
	EXPECT_EQ(multiHitRequest(world, frame.packet()).route, Route::Drop);
}

TEST(MultiHitRequest, SenderAsTargetDropped)
{
	FakeWorld world;
	auto frame = multiRequest(147, { 2, requester });
	EXPECT_EQ(multiHitRequest(world, frame.packet()).route, Route::Drop);
}

TEST(MultiHitRequest, TargetOutsideRoomDropped)
{
	FakeWorld world;
	auto frame = multiRequest(147, { 2, 31 });
	EXPECT_EQ(multiHitRequest(world, frame.packet()).route, Route::Drop);
}

TEST(SingleHitRequest, RifleHitGoesToTheHost)
{
	FakeWorld world;
	auto frame = singleRequest(152, 2);
	EXPECT_EQ(singleHitRequest(world, frame.packet()).route, Route::Host);
}

TEST(SingleHitRequest, RifleWithOptionDropped)
{
	FakeWorld world;
	auto frame = singleRequest(152, 2, 1);
	EXPECT_EQ(singleHitRequest(world, frame.packet()).route, Route::Drop);
}

TEST(SingleHitRequest, SniperFlagAllowed)
{
	FakeWorld world;
	auto frame = singleRequest(154, 2, 1);
	EXPECT_EQ(singleHitRequest(world, frame.packet()).route, Route::Host);
}

TEST(SingleHitRequest, MeleeSwingWithoutTargetAllowed)
{
	FakeWorld world;
	for (const std::uint8_t option : { std::uint8_t{ 0 }, std::uint8_t{ 1 } })
	{
		Frame frame{ 149, 0, option };
		frame.add(trace()).from(requester);
		EXPECT_EQ(singleHitRequest(world, frame.packet()).route, Route::Host);
	}
}

TEST(SingleHitRequest, MeleeSwingBadHeaderDropped)
{
	FakeWorld world;
	Frame withExtra{ 149, 1, 0 };
	withExtra.add(trace()).from(requester);
	EXPECT_EQ(singleHitRequest(world, withExtra.packet()).route, Route::Drop);
	Frame optionTwo{ 149, 0, 2 };
	optionTwo.add(trace()).from(requester);
	EXPECT_EQ(singleHitRequest(world, optionTwo.packet()).route, Route::Drop);
}

TEST(SingleHitRequest, SniperSwingWithoutTargetDropped)
{
	FakeWorld world;
	Frame frame{ 154, 1, 0 };
	frame.add(trace()).from(requester);
	EXPECT_EQ(singleHitRequest(world, frame.packet()).route, Route::Drop);
}

TEST(SingleHitRequest, UnusedBitsDropped)
{
	FakeWorld world;
	auto frame = singleRequest(152, 2);
	auto request = frame.at<HitRequest>(0);
	request.hit.unused = 1;
	std::memcpy(frame.data.data(), &request, sizeof(request));
	EXPECT_EQ(singleHitRequest(world, frame.packet()).route, Route::Drop);
}

TEST(SingleHitRequest, TargetOutsideRoomDropped)
{
	FakeWorld world;
	auto frame = singleRequest(152, 88);
	EXPECT_EQ(singleHitRequest(world, frame.packet()).route, Route::Drop);
}

TEST(SingleHitRequest, MissionDropped)
{
	FakeWorld world;
	auto frame = singleRequest(152, 2).withMission(1);
	EXPECT_EQ(singleHitRequest(world, frame.packet()).route, Route::Drop);
}

TEST(ProjectileRequest, GoesToTheHost)
{
	FakeWorld world;
	Frame frame{ 151, 1, 3 };
	frame.add(trace()).from(requester);
	EXPECT_EQ(projectileRequest(world, frame.packet()).route, Route::Host);
	frame.option = 5;
	EXPECT_EQ(projectileRequest(world, frame.packet()).route, Route::Drop);
}
