#include <algorithm>
#include <array>
#include <bit>
#include <gtest/gtest.h>
#include "../Combat/FakeWorld.h"
#include "Match/Rules.h"
#include "Structures/Match/JoinStatePackets.h"

using namespace CombatTests;

namespace
{
	constexpr std::uint32_t joiner = 3;

	template<class T>
	Frame modeStateFrame(std::uint8_t modeType, const T& state)
	{
		Frame frame{ 309, joinStateExtra, modeType };
		frame.to(joiner).add(state);
		return frame;
	}

	Frame modeSyncRequest()
	{
		Frame frame{ 304, modeSyncRequestExtra, 0 };
		frame.from(joiner).to(hostSession);
		return frame;
	}

	template<class Header>
	Frame modeSyncReply(std::uint8_t count, const Header& header)
	{
		Frame frame{ 304, 0, count };
		frame.to(joiner).add(header);
		for (std::uint8_t i = 0; i < count; ++i)
		{
			ModeSyncEntry entry{};
			entry.player = uid(2);
			frame.add(entry);
		}
		return frame;
	}

	Frame modeSyncReply(std::uint8_t count)
	{
		return modeSyncReply(count, TeamModeSyncHeader{ 1, 2, count, 1 });
	}

	template<class T>
	Route modeStateIn(std::uint32_t mode, const T& state)
	{
		FakeWorld world;
		world.roomMode = mode;
		auto frame = modeStateFrame(static_cast<std::uint8_t>(mode), state);
		return Cast::Match::modeState(world, frame.packet()).route;
	}
}

TEST(ModeState, EveryLayoutAcceptedWithItsSize)
{
	TeamModeState team{};
	team.redScore = 3;
	team.flags.state = 2;
	team.weaponLimit = 7;
	EXPECT_EQ(modeStateIn(0, team), Route::Recipient);
	EXPECT_EQ(modeStateIn(1, SoloModeState{}), Route::Recipient);
	EXPECT_EQ(modeStateIn(9, PointModeState{}), Route::Recipient);
	BombModeState bomb{};
	bomb.plantTime = 5;
	bomb.planter = uid(2);
	EXPECT_EQ(modeStateIn(10, bomb), Route::Recipient);
	EXPECT_EQ(modeStateIn(19, BombModeState{}), Route::Recipient);
	EXPECT_EQ(modeStateIn(13, PveModeState{}), Route::Recipient);
}

TEST(ModeState, ModeTypeMustBeTheRoomMode)
{
	FakeWorld world;
	auto ffaInTdm = modeStateFrame(1, SoloModeState{});
	EXPECT_EQ(Cast::Match::modeState(world, ffaInTdm.packet()).route, Route::Drop);
	world.knownMode = false;
	EXPECT_EQ(Cast::Match::modeState(world, ffaInTdm.packet()).route, Route::Recipient);
}

TEST(ModeState, PlanterKeyBit31Dropped)
{
	BombModeState bomb{};
	bomb.planter = uid(2);
	bomb.planter.unknown = 1;
	EXPECT_EQ(modeStateIn(10, bomb), Route::Drop);
}

TEST(ModeState, SizeMustMatchTheModeType)
{
	FakeWorld world;
	auto soloAsTeam = modeStateFrame(0, SoloModeState{});
	EXPECT_EQ(Cast::Match::modeState(world, soloAsTeam.packet()).route, Route::Drop);
	auto teamAsSolo = modeStateFrame(1, TeamModeState{});
	EXPECT_EQ(Cast::Match::modeState(world, teamAsSolo.packet()).route, Route::Drop);
	auto teamAsBomb = modeStateFrame(10, TeamModeState{});
	EXPECT_EQ(Cast::Match::modeState(world, teamAsBomb.packet()).route, Route::Drop);
	auto unknownType = modeStateFrame(20, TeamModeState{});
	EXPECT_EQ(Cast::Match::modeState(world, unknownType.packet()).route, Route::Drop);
}

TEST(ModeState, ReservedBitsDropped)
{
	TeamModeState team{};
	team.reserved = 1;
	EXPECT_EQ(modeStateIn(0, team), Route::Drop);
	SoloModeState solo{};
	solo.reserved = 1;
	EXPECT_EQ(modeStateIn(1, solo), Route::Drop);
	PointModeState point{};
	point.reserved = 1;
	EXPECT_EQ(modeStateIn(9, point), Route::Drop);
	BombModeState bomb{};
	bomb.team.reserved = 1;
	EXPECT_EQ(modeStateIn(10, bomb), Route::Drop);
	PveModeState pve{};
	pve.reserved[19] = 1;
	EXPECT_EQ(modeStateIn(13, pve), Route::Drop);
	PveModeState pveBits{};
	pveBits.reservedBits = 1;
	EXPECT_EQ(modeStateIn(13, pveBits), Route::Drop);
}

TEST(ModeState, OnlyTheJoinExtraFromTheHost)
{
	FakeWorld world;
	auto requestReply = modeStateFrame(0, TeamModeState{});
	requestReply.extra = 52;
	EXPECT_EQ(Cast::Match::modeState(world, requestReply.packet()).route, Route::Drop);
	auto fromGuest = modeStateFrame(0, TeamModeState{});
	fromGuest.from(2);
	EXPECT_EQ(Cast::Match::modeState(world, fromGuest.packet()).route, Route::Drop);
	auto toHost = modeStateFrame(0, TeamModeState{});
	toHost.to(hostSession);
	EXPECT_EQ(Cast::Match::modeState(world, toHost.packet()).route, Route::Drop);
	auto withMission = modeStateFrame(0, TeamModeState{});
	withMission.withMission(1);
	EXPECT_EQ(Cast::Match::modeState(world, withMission.packet()).route, Route::Drop);
}

TEST(ModeSync, RequestGoesToTheHost)
{
	FakeWorld world;
	auto frame = modeSyncRequest();
	EXPECT_EQ(Cast::Match::modeSync(world, frame.packet()).route, Route::Host);
}

TEST(ModeSync, BadRequestDropped)
{
	FakeWorld world;
	auto noExtra = modeSyncRequest();
	noExtra.extra = 0;
	EXPECT_EQ(Cast::Match::modeSync(world, noExtra.packet()).route, Route::Drop);
	auto withPayload = modeSyncRequest();
	withPayload.add(std::uint32_t{ 0 });
	EXPECT_EQ(Cast::Match::modeSync(world, withPayload.packet()).route, Route::Drop);
	auto toMainServer = modeSyncRequest();
	toMainServer.withMission(1);
	EXPECT_EQ(Cast::Match::modeSync(world, toMainServer.packet()).route, Route::Drop);
	auto fromStranger = modeSyncRequest();
	fromStranger.from(9);
	EXPECT_EQ(Cast::Match::modeSync(world, fromStranger.packet()).route, Route::Drop);
}

TEST(ModeSync, ReplyGoesToTheRequester)
{
	FakeWorld world;
	auto withEntries = modeSyncReply(2);
	EXPECT_EQ(Cast::Match::modeSync(world, withEntries.packet()).route, Route::Recipient);
	auto empty = modeSyncReply(0);
	EXPECT_EQ(Cast::Match::modeSync(world, empty.packet()).route, Route::Recipient);
	auto full = modeSyncReply(16);
	EXPECT_EQ(Cast::Match::modeSync(world, full.packet()).route, Route::Recipient);
}

TEST(ModeSync, BadReplyDropped)
{
	FakeWorld world;
	auto tooMany = modeSyncReply(17);
	EXPECT_EQ(Cast::Match::modeSync(world, tooMany.packet()).route, Route::Drop);
	auto mismatch = modeSyncReply(2);
	mismatch.option = 3;
	EXPECT_EQ(Cast::Match::modeSync(world, mismatch.packet()).route, Route::Drop);
	auto withExtra = modeSyncReply(1);
	withExtra.extra = modeSyncRequestExtra;
	EXPECT_EQ(Cast::Match::modeSync(world, withExtra.packet()).route, Route::Drop);
	auto toHost = modeSyncReply(1);
	toHost.to(hostSession);
	EXPECT_EQ(Cast::Match::modeSync(world, toHost.packet()).route, Route::Drop);
}

TEST(ModeSync, CountFieldMustMatchTheOption)
{
	FakeWorld world;
	auto teamMismatch = modeSyncReply(2, TeamModeSyncHeader{ 0, 0, 3, 0 });
	EXPECT_EQ(Cast::Match::modeSync(world, teamMismatch.packet()).route, Route::Drop);
	world.roomMode = Common::Enums::FreeForAll;
	auto teamInFfa = modeSyncReply(2);
	EXPECT_EQ(Cast::Match::modeSync(world, teamInFfa.packet()).route, Route::Drop);
	auto solo = modeSyncReply(2, std::uint32_t{ 2 });
	EXPECT_EQ(Cast::Match::modeSync(world, solo.packet()).route, Route::Recipient);
	world.knownMode = false;
	EXPECT_EQ(Cast::Match::modeSync(world, solo.packet()).route, Route::Drop);
}

TEST(ModeSync, ScrimmageNeedsItsEightByteHeader)
{
	FakeWorld world;
	world.roomMode = Common::Enums::Scrimmage;
	ScrimmageModeSyncHeader header{};
	header.count = 2;
	auto moc = modeSyncReply(2, std::uint32_t{ 0 });
	const auto bytes = std::bit_cast<std::array<std::uint8_t, sizeof(header)>>(header);
	std::copy(bytes.begin(), bytes.end(), moc.data.begin());
	EXPECT_EQ(Cast::Match::modeSync(world, moc.packet()).route, Route::Recipient);
	auto empty = modeSyncReply(0, std::uint32_t{ 0 });
	EXPECT_EQ(Cast::Match::modeSync(world, empty.packet()).route, Route::Drop);
}

TEST(ModeSync, ListReadUntilEmptyEntryGetsOne)
{
	FakeWorld world;
	world.roomMode = Common::Enums::BombBattle;
	auto bmb = modeSyncReply(2);
	EXPECT_EQ(Cast::Match::modeSync(world, bmb.packet()).route, Route::Handled);
	ASSERT_EQ(world.sent.size(), 1u);
	EXPECT_EQ(world.sent[0].route, Route::Recipient);
	EXPECT_EQ(world.sent[0].recipient, joiner);
	EXPECT_EQ(world.sent[0].option, 2);
	ASSERT_EQ(world.sent[0].payload.size(), 4u + 3 * sizeof(ModeSyncEntry));
	EXPECT_EQ(world.sent[0].at<ModeSyncEntry>(4 + 2 * sizeof(ModeSyncEntry)).player.session, 0u);
	auto full = modeSyncReply(16);
	EXPECT_EQ(Cast::Match::modeSync(world, full.packet()).route, Route::Recipient);
}
