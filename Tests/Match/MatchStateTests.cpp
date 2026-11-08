#include <gtest/gtest.h>
#include "../Combat/FakeWorld.h"
#include "Match/Rules.h"

using namespace CombatTests;

namespace
{
	constexpr std::uint32_t guest = 3;
	constexpr std::uint32_t stranger = 9;
}

TEST(MatchEnd, FromHostEndsTheMatch)
{
	FakeWorld world;
	Frame frame{ 254, 0, 0 };
	EXPECT_EQ(Cast::Match::matchEnd(world, frame.packet()).route, Route::Handled);
	EXPECT_TRUE(world.matchEnded);
	EXPECT_TRUE(world.sent.empty());
}

TEST(MatchEnd, FromGuestDropped)
{
	FakeWorld world;
	Frame frame{ 254, 0, 0 };
	frame.from(guest);
	EXPECT_EQ(Cast::Match::matchEnd(world, frame.packet()).route, Route::Drop);
	EXPECT_FALSE(world.matchEnded);
}

TEST(MatchEnd, PayloadOrHeaderDropped)
{
	FakeWorld world;
	Frame withPayload{ 254, 0, 0 };
	withPayload.add(std::uint32_t{ 1 });
	EXPECT_EQ(Cast::Match::matchEnd(world, withPayload.packet()).route, Route::Drop);
	Frame withExtra{ 254, 1, 0 };
	EXPECT_EQ(Cast::Match::matchEnd(world, withExtra.packet()).route, Route::Drop);
	Frame withMission{ 254, 0, 0 };
	withMission.withMission(1);
	EXPECT_EQ(Cast::Match::matchEnd(world, withMission.packet()).route, Route::Drop);
	EXPECT_FALSE(world.matchEnded);
}

TEST(MatchStart, FromHostStartsTheMatch)
{
	FakeWorld world;
	Frame frame{ 258, 0, 0 };
	EXPECT_EQ(Cast::Match::matchStart(world, frame.packet()).route, Route::Handled);
	EXPECT_TRUE(world.matchStarted);
	EXPECT_TRUE(world.sent.empty());
}

TEST(MatchStart, FromGuestDroppedAndNeverForwarded)
{
	FakeWorld world;
	Frame frame{ 258, 0, 0 };
	frame.from(guest).to(2);
	EXPECT_EQ(Cast::Match::matchStart(world, frame.packet()).route, Route::Drop);
	EXPECT_FALSE(world.matchStarted);
	EXPECT_TRUE(world.sent.empty());
}

TEST(MatchLeave, MemberLeavesTheMatch)
{
	FakeWorld world;
	Frame frame{ 256, 0, 0 };
	frame.from(guest);
	EXPECT_EQ(Cast::Match::matchLeave(world, frame.packet()).route, Route::Handled);
	ASSERT_EQ(world.leftMatch.size(), 1u);
	EXPECT_EQ(world.leftMatch[0], guest);
}

TEST(MatchLeave, StrangerOrPayloadDropped)
{
	FakeWorld world;
	Frame fromStranger{ 256, 0, 0 };
	fromStranger.from(stranger);
	EXPECT_EQ(Cast::Match::matchLeave(world, fromStranger.packet()).route, Route::Drop);
	Frame withPayload{ 256, 0, 0 };
	withPayload.from(guest).add(std::uint32_t{ 0 });
	EXPECT_EQ(Cast::Match::matchLeave(world, withPayload.packet()).route, Route::Drop);
	EXPECT_TRUE(world.leftMatch.empty());
}

TEST(GuestReadyAndMatchStop, EmptyCommandsHandledWithoutEffects)
{
	FakeWorld world;
	Frame ready{ 255, 0, 0 };
	ready.from(guest);
	EXPECT_EQ(Cast::Match::guestReady(world, ready.packet()).route, Route::Handled);
	Frame stop{ 259, 0, 0 };
	EXPECT_EQ(Cast::Match::matchStop(world, stop.packet()).route, Route::Handled);
	EXPECT_TRUE(world.sent.empty());
	EXPECT_FALSE(world.matchEnded);
	EXPECT_FALSE(world.matchStarted);
}

TEST(GuestReadyAndMatchStop, BadCommandsDropped)
{
	FakeWorld world;
	Frame ready{ 255, 0, 1 };
	ready.from(guest);
	EXPECT_EQ(Cast::Match::guestReady(world, ready.packet()).route, Route::Drop);
	Frame stop{ 259, 0, 0 };
	stop.add(std::uint32_t{ 0 });
	EXPECT_EQ(Cast::Match::matchStop(world, stop.packet()).route, Route::Drop);
	Frame strangerStop{ 259, 0, 0 };
	strangerStop.from(stranger);
	EXPECT_EQ(Cast::Match::matchStop(world, strangerStop.packet()).route, Route::Drop);
}
