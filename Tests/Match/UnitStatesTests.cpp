#include <bit>
#include <gtest/gtest.h>
#include "../Combat/FakeWorld.h"
#include "Match/Rules.h"
#include "Structures/Match/JoinStatePackets.h"

using namespace CombatTests;

namespace
{
	constexpr std::uint32_t joiner = 3;

	UnitStateEntry unitState(std::uint32_t key)
	{
		UnitStateEntry entry{};
		entry.state = 12;
		entry.weaponType = 3;
		entry.unused = 0x7FFFFF;
		entry.hpMasked = 0x11111111;
		entry.hpMaskedNot = 0xEEEEEEEE;
		entry.hpKey = 0x22222222;
		entry.character = 5;
		entry.unusedFlags = 0x1FFFFFFF;
		entry.unit = std::bit_cast<decltype(entry.unit)>(key);
		entry.respawnTick = 77;
		return entry;
	}

	Frame unitStatesFrame(std::size_t count)
	{
		Frame frame{ 305, 0, static_cast<std::uint8_t>(count) };
		frame.to(joiner);
		for (std::size_t i = 0; i < count; ++i)
		{
			frame.add(unitState(1000000 + static_cast<std::uint32_t>(i)));
		}
		return frame;
	}
}

TEST(UnitStates, ForwardedWithOnlyTheFieldsTheClientReads)
{
	FakeWorld world;
	auto frame = unitStatesFrame(2);
	EXPECT_EQ(Cast::Match::unitStates(world, frame.packet()).route, Route::Recipient);
	const auto entry = frame.at<UnitStateEntry>(sizeof(UnitStateEntry));
	EXPECT_EQ(entry.state, 12u);
	EXPECT_EQ(entry.weaponType, 3u);
	EXPECT_EQ(entry.unused, 0u);
	EXPECT_EQ(entry.hpMasked, 0x11111111u);
	EXPECT_EQ(entry.hpMaskedNot, 0xEEEEEEEEu);
	EXPECT_EQ(entry.hpKey, 0x22222222u);
	EXPECT_EQ(entry.character, 5u);
	EXPECT_EQ(entry.unusedFlags, 0u);
	EXPECT_EQ(raw(entry.unit), 1000001u);
	EXPECT_EQ(entry.respawnTick, 77u);
}

TEST(UnitStates, LargestChunkAccepted)
{
	FakeWorld world;
	auto frame = unitStatesFrame(maxUnitStates);
	EXPECT_EQ(Cast::Match::unitStates(world, frame.packet()).route, Route::Recipient);
}

TEST(UnitStates, CountAndSizeMustMatch)
{
	FakeWorld world;
	auto tooMany = unitStatesFrame(maxUnitStates + 1);
	EXPECT_EQ(Cast::Match::unitStates(world, tooMany.packet()).route, Route::Drop);
	auto mismatch = unitStatesFrame(2);
	mismatch.option = 3;
	EXPECT_EQ(Cast::Match::unitStates(world, mismatch.packet()).route, Route::Drop);
	auto empty = unitStatesFrame(0);
	EXPECT_EQ(Cast::Match::unitStates(world, empty.packet()).route, Route::Drop);
	auto withExtra = unitStatesFrame(1);
	withExtra.extra = 37;
	EXPECT_EQ(Cast::Match::unitStates(world, withExtra.packet()).route, Route::Drop);
}

TEST(UnitStates, BadKeyDropped)
{
	FakeWorld world;
	Frame frame{ 305, 0, 1 };
	frame.to(joiner).add(unitState(0x80000001u));
	EXPECT_EQ(Cast::Match::unitStates(world, frame.packet()).route, Route::Drop);
}

TEST(UnitStates, OnlyFromTheHostToAnotherMember)
{
	FakeWorld world;
	auto fromGuest = unitStatesFrame(1);
	fromGuest.from(2);
	EXPECT_EQ(Cast::Match::unitStates(world, fromGuest.packet()).route, Route::Drop);
	auto toHost = unitStatesFrame(1);
	toHost.to(hostSession);
	EXPECT_EQ(Cast::Match::unitStates(world, toHost.packet()).route, Route::Drop);
}
