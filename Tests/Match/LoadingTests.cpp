#include <gtest/gtest.h>
#include "../Combat/FakeWorld.h"
#include "Match/Rules.h"
#include "Structures/Match/MatchFlowPackets.h"

using namespace CombatTests;

namespace
{
	constexpr std::uint32_t loader = 3;

	Frame loading(std::uint8_t progress)
	{
		Frame frame{ 257, 0, progress };
		frame.from(loader);
		return frame;
	}
}

TEST(LoadingProgress, BuildsTheProgressForTheMatch)
{
	FakeWorld world;
	auto frame = loading(4);
	EXPECT_EQ(Cast::Match::loadingProgress(world, frame.packet()).route, Route::Handled);
	ASSERT_EQ(world.sent.size(), 1u);
	const auto& sent = world.sent[0];
	EXPECT_EQ(sent.route, Route::Match);
	EXPECT_EQ(sent.order, 257);
	EXPECT_EQ(sent.mission, 0);
	EXPECT_EQ(sent.extra, 0);
	EXPECT_EQ(sent.option, 4);
	ASSERT_EQ(sent.payload.size(), sizeof(LoadingProgress));
	EXPECT_EQ(raw(sent.at<LoadingProgress>(0).player), raw(uid(loader)));
	EXPECT_TRUE(world.loaded.empty());
}

TEST(LoadingProgress, DoneMarksThePlayerLoaded)
{
	FakeWorld world;
	world.players[loader].inMatch = false;
	auto frame = loading(loadingDone);
	EXPECT_EQ(Cast::Match::loadingProgress(world, frame.packet()).route, Route::Handled);
	ASSERT_EQ(world.loaded.size(), 1u);
	EXPECT_EQ(world.loaded[0], loader);
	EXPECT_EQ(world.sent.size(), 1u);
}

TEST(LoadingProgress, BadCommandsDropped)
{
	FakeWorld world;
	auto withPayload = loading(9);
	withPayload.add(std::uint32_t{ 0 });
	EXPECT_EQ(Cast::Match::loadingProgress(world, withPayload.packet()).route, Route::Drop);
	Frame withExtra{ 257, 1, 9 };
	withExtra.from(loader);
	EXPECT_EQ(Cast::Match::loadingProgress(world, withExtra.packet()).route, Route::Drop);
	auto withMission = loading(9);
	withMission.withMission(1);
	EXPECT_EQ(Cast::Match::loadingProgress(world, withMission.packet()).route, Route::Drop);
	auto fromStranger = loading(9);
	fromStranger.from(9);
	EXPECT_EQ(Cast::Match::loadingProgress(world, fromStranger.packet()).route, Route::Drop);
	EXPECT_TRUE(world.sent.empty());
	EXPECT_TRUE(world.loaded.empty());
}
