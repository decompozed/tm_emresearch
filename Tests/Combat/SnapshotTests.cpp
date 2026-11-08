#include <gtest/gtest.h>
#include "FakeWorld.h"
#include "Structures/Items/ItemPackets.h"

using namespace CombatTests;

namespace
{
	constexpr std::uint32_t joiner = 4;

	MapItemEntry mapItem(std::uint32_t itemObjectId)
	{
		MapItemEntry entry{};
		entry.itemObjectId = itemObjectId;
		entry.kind.itemId = 1300001;
		entry.expireTick = 9000;
		return entry;
	}

	Frame mapItems(std::uint8_t extra, std::uint8_t count)
	{
		Frame frame{ 90, extra, count };
		frame.to(joiner);
		for (std::uint8_t i = 0; i < count; ++i)
		{
			frame.add(mapItem(0x00010000u + i + 1));
		}
		return frame;
	}

	Frame inventories()
	{
		Frame frame{ 94, 0, 2 };
		frame.to(joiner);
		frame.add(InventoryBlockHeader{ uid(2), 2 });
		frame.add(InventoryItem{ 11, 1700001, 0 });
		frame.add(InventoryItem{ 12, 1700002, 0x100 });
		frame.add(InventoryBlockHeader{ uid(3), 0 });
		return frame;
	}

	UnitEffectEntry effect(const UniqueId& unit)
	{
		UnitEffectEntry entry{};
		entry.unit = unit;
		entry.source = uid(5);
		entry.effectId = 128;
		entry.remainingTime[0] = 0x10;
		entry.sourceItemId = 1300001;
		return entry;
	}

	Frame effects(std::uint8_t count)
	{
		Frame frame{ 78, 0, count };
		frame.to(joiner);
		for (std::uint8_t i = 0; i < count; ++i)
		{
			frame.add(effect(uid(2)));
		}
		return frame;
	}
}

TEST(MapItemSnapshot, GoesOnlyToTheRecipient)
{
	FakeWorld world;
	auto first = mapItems(mapItemSnapshotFirstPart, 3);
	const auto verdict = mapItemSnapshot(world, first.packet());
	EXPECT_EQ(verdict.route, Route::Recipient);
	auto next = mapItems(0, maxMapItemSnapshotEntries);
	EXPECT_EQ(mapItemSnapshot(world, next.packet()).route, Route::Recipient);
}

TEST(MapItemSnapshot, ObserverOrLoadingRecipientAccepted)
{
	FakeWorld world;
	world.players[joiner] = PlayerState{ false, true, true };
	auto frame = mapItems(mapItemSnapshotFirstPart, 1);
	EXPECT_EQ(mapItemSnapshot(world, frame.packet()).route, Route::Recipient);
}

TEST(MapItemSnapshot, FromNonHostOrToBadRecipientDropped)
{
	FakeWorld world;
	auto fromPlayer = mapItems(mapItemSnapshotFirstPart, 1).from(2);
	EXPECT_EQ(mapItemSnapshot(world, fromPlayer.packet()).route, Route::Drop);
	auto toHost = mapItems(mapItemSnapshotFirstPart, 1).to(hostSession);
	EXPECT_EQ(mapItemSnapshot(world, toHost.packet()).route, Route::Drop);
	auto toStranger = mapItems(mapItemSnapshotFirstPart, 1).to(60);
	EXPECT_EQ(mapItemSnapshot(world, toStranger.packet()).route, Route::Drop);
}

TEST(MapItemSnapshot, MalformedDropped)
{
	FakeWorld world;
	auto empty = mapItems(mapItemSnapshotFirstPart, 0);
	EXPECT_EQ(mapItemSnapshot(world, empty.packet()).route, Route::Drop);
	auto tooMany = mapItems(0, maxMapItemSnapshotEntries + 1);
	EXPECT_EQ(mapItemSnapshot(world, tooMany.packet()).route, Route::Drop);
	Frame shorter{ 90, 0, 2 };
	shorter.to(joiner).add(mapItem(0x00010001u));
	EXPECT_EQ(mapItemSnapshot(world, shorter.packet()).route, Route::Drop);
	auto extra = mapItems(1, 1);
	EXPECT_EQ(mapItemSnapshot(world, extra.packet()).route, Route::Drop);
	auto mission = mapItems(0, 1).withMission(1);
	EXPECT_EQ(mapItemSnapshot(world, mission.packet()).route, Route::Drop);
	Frame noObject{ 90, 0, 1 };
	noObject.to(joiner).add(mapItem(0));
	EXPECT_EQ(mapItemSnapshot(world, noObject.packet()).route, Route::Drop);
}

TEST(InventorySnapshot, GoesOnlyToTheRecipient)
{
	FakeWorld world;
	auto frame = inventories();
	EXPECT_EQ(inventorySnapshot(world, frame.packet()).route, Route::Recipient);
}

TEST(InventorySnapshot, EmptyAndBotInventoriesAccepted)
{
	FakeWorld world;
	Frame empty{ 94, 0, 0 };
	empty.to(joiner);
	EXPECT_EQ(inventorySnapshot(world, empty.packet()).route, Route::Recipient);
	Frame bot{ 94, 0, 1 };
	bot.to(joiner).add(InventoryBlockHeader{ uid(1000000 & 0xFFFF, 1000000 >> 16), 1 }).add(InventoryItem{ 3, 1700001, 0 });
	EXPECT_EQ(inventorySnapshot(world, bot.packet()).route, Route::Recipient);
}

TEST(InventorySnapshot, BlocksMustFillThePacket)
{
	FakeWorld world;
	auto truncated = inventories();
	truncated.data.resize(truncated.data.size() - 4);
	EXPECT_EQ(inventorySnapshot(world, truncated.packet()).route, Route::Drop);
	auto trailing = inventories().add(std::uint32_t{ 0 });
	EXPECT_EQ(inventorySnapshot(world, trailing.packet()).route, Route::Drop);
	auto moreBlocks = inventories();
	moreBlocks.option = 3;
	EXPECT_EQ(inventorySnapshot(world, moreBlocks.packet()).route, Route::Drop);
	Frame hugeCount{ 94, 0, 1 };
	hugeCount.to(joiner).add(InventoryBlockHeader{ uid(2), 0xFFFFFFFF }).add(InventoryItem{});
	EXPECT_EQ(inventorySnapshot(world, hugeCount.packet()).route, Route::Drop);
}

TEST(InventorySnapshot, MalformedOrForeignDropped)
{
	FakeWorld world;
	auto tooMany = inventories();
	tooMany.option = maxInventoryBlocks + 1;
	EXPECT_EQ(inventorySnapshot(world, tooMany.packet()).route, Route::Drop);
	auto extra = inventories();
	extra.extra = 1;
	EXPECT_EQ(inventorySnapshot(world, extra.packet()).route, Route::Drop);
	auto fromPlayer = inventories().from(2);
	EXPECT_EQ(inventorySnapshot(world, fromPlayer.packet()).route, Route::Drop);
	auto toHost = inventories().to(hostSession);
	EXPECT_EQ(inventorySnapshot(world, toHost.packet()).route, Route::Drop);
	auto marked = uid(2);
	marked.unknown = 1;
	Frame bit31{ 94, 0, 1 };
	bit31.to(joiner).add(InventoryBlockHeader{ marked, 0 });
	EXPECT_EQ(inventorySnapshot(world, bit31.packet()).route, Route::Drop);
}

TEST(EffectSnapshot, GoesOnlyToTheRecipient)
{
	FakeWorld world;
	auto frame = effects(2);
	EXPECT_EQ(effectSnapshot(world, frame.packet()).route, Route::Recipient);
	auto full = effects(maxEffectSnapshotEntries);
	EXPECT_EQ(effectSnapshot(world, full.packet()).route, Route::Recipient);
}

TEST(EffectSnapshot, MalformedOrForeignDropped)
{
	FakeWorld world;
	auto empty = effects(0);
	EXPECT_EQ(effectSnapshot(world, empty.packet()).route, Route::Drop);
	auto tooMany = effects(maxEffectSnapshotEntries + 1);
	EXPECT_EQ(effectSnapshot(world, tooMany.packet()).route, Route::Drop);
	auto shorter = effects(2);
	shorter.data.resize(30);
	EXPECT_EQ(effectSnapshot(world, shorter.packet()).route, Route::Drop);
	auto extra = effects(1);
	extra.extra = 1;
	EXPECT_EQ(effectSnapshot(world, extra.packet()).route, Route::Drop);
	auto fromPlayer = effects(1).from(3);
	EXPECT_EQ(effectSnapshot(world, fromPlayer.packet()).route, Route::Drop);
	auto toStranger = effects(1).to(60);
	EXPECT_EQ(effectSnapshot(world, toStranger.packet()).route, Route::Drop);

	Frame ownEffect{ 78, 0, 1 };
	ownEffect.to(joiner).add(effect(uid(joiner)));
	EXPECT_EQ(effectSnapshot(world, ownEffect.packet()).route, Route::Drop);

	auto reserved = effect(uid(2));
	reserved.reserved = 1;
	Frame reservedBits{ 78, 0, 1 };
	reservedBits.to(joiner).add(reserved);
	EXPECT_EQ(effectSnapshot(world, reservedBits.packet()).route, Route::Drop);
}
