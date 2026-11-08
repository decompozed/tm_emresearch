#include <gtest/gtest.h>
#include "../Combat/FakeWorld.h"
#include "Match/Rules.h"
#include "Structures/Match/JoinStatePackets.h"
#include "Structures/Match/MatchFlowPackets.h"

using namespace CombatTests;

namespace
{
	constexpr std::uint32_t joiner = 3;

	Frame joinRequest()
	{
		Frame frame{ 79, 0, 0 };
		frame.from(joiner).to(hostSession).withMission(joinRequestMission);
		return frame;
	}

	Frame clock(std::uint8_t extra, bool withPayload)
	{
		Frame frame{ 408, extra, 0 };
		frame.to(joiner);
		if (withPayload)
		{
			frame.add(HostClock{ 123456789ULL, 90000 });
		}
		return frame;
	}
}

TEST(JoinStateRequest, GoesToTheHost)
{
	FakeWorld world;
	auto frame = joinRequest();
	EXPECT_EQ(Cast::Match::joinStateRequest(world, frame.packet()).route, Route::Host);
}

TEST(JoinStateRequest, BadCommandsDropped)
{
	FakeWorld world;
	auto missionZero = joinRequest();
	missionZero.withMission(0);
	EXPECT_EQ(Cast::Match::joinStateRequest(world, missionZero.packet()).route, Route::Drop);
	auto withPayload = joinRequest();
	withPayload.add(std::uint32_t{ 0 });
	EXPECT_EQ(Cast::Match::joinStateRequest(world, withPayload.packet()).route, Route::Drop);
	Frame withExtra{ 79, 1, 0 };
	withExtra.from(joiner).withMission(joinRequestMission);
	EXPECT_EQ(Cast::Match::joinStateRequest(world, withExtra.packet()).route, Route::Drop);
}

TEST(JoinStateRequest, FromHostOrStrangerDropped)
{
	FakeWorld world;
	auto fromHost = joinRequest();
	fromHost.from(hostSession);
	EXPECT_EQ(Cast::Match::joinStateRequest(world, fromHost.packet()).route, Route::Drop);
	auto fromStranger = joinRequest();
	fromStranger.from(9);
	EXPECT_EQ(Cast::Match::joinStateRequest(world, fromStranger.packet()).route, Route::Drop);
}

TEST(HostClock, ClockAndNoModGoToTheRecipient)
{
	FakeWorld world;
	auto withClock = clock(joinStateExtra, true);
	EXPECT_EQ(Cast::Match::hostClock(world, withClock.packet()).route, Route::Recipient);
	auto noMod = clock(noModExtra, false);
	EXPECT_EQ(Cast::Match::hostClock(world, noMod.packet()).route, Route::Recipient);
}

TEST(HostClock, ExtraAndSizeMustMatch)
{
	FakeWorld world;
	auto clockWithoutPayload = clock(joinStateExtra, false);
	EXPECT_EQ(Cast::Match::hostClock(world, clockWithoutPayload.packet()).route, Route::Drop);
	auto noModWithPayload = clock(noModExtra, true);
	EXPECT_EQ(Cast::Match::hostClock(world, noModWithPayload.packet()).route, Route::Drop);
	auto otherExtra = clock(5, true);
	EXPECT_EQ(Cast::Match::hostClock(world, otherExtra.packet()).route, Route::Drop);
	Frame withOption{ 408, joinStateExtra, 1 };
	withOption.to(joiner).add(HostClock{});
	EXPECT_EQ(Cast::Match::hostClock(world, withOption.packet()).route, Route::Drop);
	auto withMission = clock(joinStateExtra, true);
	withMission.withMission(1);
	EXPECT_EQ(Cast::Match::hostClock(world, withMission.packet()).route, Route::Drop);
}

TEST(HostClock, OnlyFromTheHostToAnotherMember)
{
	FakeWorld world;
	auto fromGuest = clock(noModExtra, false);
	fromGuest.from(2);
	EXPECT_EQ(Cast::Match::hostClock(world, fromGuest.packet()).route, Route::Drop);
	auto toHost = clock(noModExtra, false);
	toHost.to(hostSession);
	EXPECT_EQ(Cast::Match::hostClock(world, toHost.packet()).route, Route::Drop);
	auto toStranger = clock(noModExtra, false);
	toStranger.to(9);
	EXPECT_EQ(Cast::Match::hostClock(world, toStranger.packet()).route, Route::Drop);
}
