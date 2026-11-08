#include <gtest/gtest.h>
#include "FakeWorld.h"
#include "Structures/Items/ItemPackets.h"

using namespace CombatTests;

namespace
{
	constexpr std::uint32_t requester = 3;

	Frame pickupRequest(std::uint32_t itemObjectId = 0x00020005, std::uint32_t capsuleItemId = 0)
	{
		Frame frame{ 96, 0, 0 };
		frame.from(requester).add(ItemPickupRequest{ itemObjectId, capsuleItemId });
		return frame;
	}

	Frame pickup(std::uint8_t extra, const UniqueId& player, std::uint32_t itemObjectId = 0x00020005, std::uint32_t itemId = 1300001)
	{
		Frame frame{ 262, extra, 0 };
		frame.add(ItemPickupPacket{ itemObjectId, itemId, player });
		return frame;
	}

	Frame useRequest()
	{
		Frame frame{ 102, 0, 0 };
		frame.from(requester).add(ItemUseRequest{ 77 });
		return frame;
	}

	Frame use(std::uint8_t extra, const UniqueId& player)
	{
		Frame frame{ 263, extra, 0 };
		frame.add(ItemUsePacket{ 77, player });
		return frame;
	}

	MapItemEntry mapItem(std::uint32_t itemObjectId = 0x00010003)
	{
		MapItemEntry entry{};
		entry.itemObjectId = itemObjectId;
		entry.owner = uid(requester);
		entry.kind.itemId = 1300001;
		entry.kind.category = 1;
		entry.x = 10.0f;
		entry.y = 20.0f;
		entry.z = 30.0f;
		entry.expireTick = 5000;
		return entry;
	}

	Frame spawn(std::uint8_t mission = 0)
	{
		Frame frame{ 260, 0, 1 };
		frame.withMission(mission).add(mapItem());
		return frame;
	}

	ItemGrantPacket grantPacket()
	{
		ItemGrantPacket packet{};
		packet.player = uid(requester);
		packet.item.itemId = 1700001;
		packet.item.granted = 1;
		return packet;
	}

	Frame grant(std::uint8_t mission, std::uint8_t extra, std::uint8_t option)
	{
		Frame frame{ 261, extra, option };
		frame.withMission(mission).add(grantPacket());
		return frame;
	}
}

TEST(ItemPickupRequest, GoesToHost)
{
	FakeWorld world;
	auto frame = pickupRequest();
	EXPECT_EQ(itemPickupRequest(world, frame.packet()).route, Route::Host);
}

TEST(ItemPickupRequest, DeadOrObserverSenderStillGoesToHost)
{
	FakeWorld world;
	world.players[requester].dead = true;
	auto dead = pickupRequest();
	EXPECT_EQ(itemPickupRequest(world, dead.packet()).route, Route::Host);
	world.players[requester].observer = true;
	auto observer = pickupRequest();
	EXPECT_EQ(itemPickupRequest(world, observer.packet()).route, Route::Host);
}

TEST(ItemPickupRequest, CapsuleMissionDropped)
{
	FakeWorld world;
	auto frame = pickupRequest(0x00020005, 2100001).withMission(1);
	EXPECT_EQ(itemPickupRequest(world, frame.packet()).route, Route::Drop);
}

TEST(ItemPickupRequest, MalformedDropped)
{
	FakeWorld world;
	auto noItem = pickupRequest(0);
	EXPECT_EQ(itemPickupRequest(world, noItem.packet()).route, Route::Drop);
	auto capsuleArgument = pickupRequest(0x00020005, 5);
	EXPECT_EQ(itemPickupRequest(world, capsuleArgument.packet()).route, Route::Drop);
	auto shorter = pickupRequest();
	shorter.data.resize(4);
	EXPECT_EQ(itemPickupRequest(world, shorter.packet()).route, Route::Drop);
	auto extra = pickupRequest();
	extra.extra = 1;
	EXPECT_EQ(itemPickupRequest(world, extra.packet()).route, Route::Drop);
	auto option = pickupRequest();
	option.option = 1;
	EXPECT_EQ(itemPickupRequest(world, option.packet()).route, Route::Drop);
}

TEST(ItemPickupRequest, FromHostNonMemberOrOutsideMatchDropped)
{
	FakeWorld world;
	auto host = pickupRequest().from(hostSession);
	EXPECT_EQ(itemPickupRequest(world, host.packet()).route, Route::Drop);
	auto stranger = pickupRequest().from(60);
	EXPECT_EQ(itemPickupRequest(world, stranger.packet()).route, Route::Drop);
	world.players[requester].inMatch = false;
	auto loading = pickupRequest();
	EXPECT_EQ(itemPickupRequest(world, loading.packet()).route, Route::Drop);
}

TEST(ItemPickup, HostOutcomeGoesToWholeMatch)
{
	FakeWorld world;
	for (std::uint8_t extra = 0; extra <= 2; ++extra)
	{
		auto frame = pickup(extra, uid(requester));
		EXPECT_EQ(itemPickup(world, frame.packet()).route, Route::Match);
	}
	auto own = pickup(0, uid(hostSession));
	EXPECT_EQ(itemPickup(world, own.packet()).route, Route::Match);
}

TEST(ItemPickup, FromNonHostDropped)
{
	FakeWorld world;
	auto frame = pickup(0, uid(requester)).from(requester);
	EXPECT_EQ(itemPickup(world, frame.packet()).route, Route::Drop);
}

TEST(ItemPickup, InventoryLoadMissionDropped)
{
	FakeWorld world;
	auto frame = pickup(0, uid(requester)).withMission(1);
	EXPECT_EQ(itemPickup(world, frame.packet()).route, Route::Drop);
}

TEST(ItemPickup, MalformedDropped)
{
	FakeWorld world;
	auto inventoryFull = pickup(7, uid(requester));
	EXPECT_EQ(itemPickup(world, inventoryFull.packet()).route, Route::Drop);
	auto option = pickup(0, uid(requester));
	option.option = 1;
	EXPECT_EQ(itemPickup(world, option.packet()).route, Route::Drop);
	auto noObject = pickup(0, uid(requester), 0);
	EXPECT_EQ(itemPickup(world, noObject.packet()).route, Route::Drop);
	auto negativeItem = pickup(0, uid(requester), 0x00020005, 0x80000001);
	EXPECT_EQ(itemPickup(world, negativeItem.packet()).route, Route::Drop);
	auto noItem = pickup(0, uid(requester), 0x00020005, 0);
	EXPECT_EQ(itemPickup(world, noItem.packet()).route, Route::Drop);
	auto longer = pickup(0, uid(requester));
	longer.data.resize(16);
	EXPECT_EQ(itemPickup(world, longer.packet()).route, Route::Drop);
}

TEST(ItemPickup, PlayerOutsideRoomDropped)
{
	FakeWorld world;
	auto stranger = pickup(0, uid(60));
	EXPECT_EQ(itemPickup(world, stranger.packet()).route, Route::Drop);
	auto marked = uid(requester);
	marked.unknown = 1;
	auto bit31 = pickup(0, marked);
	EXPECT_EQ(itemPickup(world, bit31.packet()).route, Route::Drop);
}

TEST(ItemUseRequest, GoesToHost)
{
	FakeWorld world;
	auto frame = useRequest();
	EXPECT_EQ(itemUseRequest(world, frame.packet()).route, Route::Host);
}

TEST(ItemUseRequest, ObserverOrHostDroppedDeadSenderGoesToHost)
{
	FakeWorld world;
	auto host = useRequest().from(hostSession);
	EXPECT_EQ(itemUseRequest(world, host.packet()).route, Route::Drop);
	world.players[requester].dead = true;
	auto dead = useRequest();
	EXPECT_EQ(itemUseRequest(world, dead.packet()).route, Route::Host);
	world.players[requester] = PlayerState{ true, true, false };
	auto observer = useRequest();
	EXPECT_EQ(itemUseRequest(world, observer.packet()).route, Route::Drop);
}

TEST(ItemUseRequest, MalformedDropped)
{
	FakeWorld world;
	auto mainVariant = useRequest().withMission(1);
	EXPECT_EQ(itemUseRequest(world, mainVariant.packet()).route, Route::Drop);
	auto longer = useRequest();
	longer.data.resize(12);
	EXPECT_EQ(itemUseRequest(world, longer.packet()).route, Route::Drop);
	auto extra = useRequest();
	extra.extra = 26;
	EXPECT_EQ(itemUseRequest(world, extra.packet()).route, Route::Drop);
}

TEST(ItemUse, HostOutcomeGoesToWholeMatch)
{
	FakeWorld world;
	auto used = use(1, uid(requester));
	EXPECT_EQ(itemUse(world, used.packet()).route, Route::Match);
	auto failed = use(2, uid(requester));
	EXPECT_EQ(itemUse(world, failed.packet()).route, Route::Match);
}

TEST(ItemUse, MalformedOrForeignDropped)
{
	FakeWorld world;
	auto fromPlayer = use(1, uid(requester)).from(requester);
	EXPECT_EQ(itemUse(world, fromPlayer.packet()).route, Route::Drop);
	auto zeroExtra = use(0, uid(requester));
	EXPECT_EQ(itemUse(world, zeroExtra.packet()).route, Route::Drop);
	auto mission = use(1, uid(requester)).withMission(1);
	EXPECT_EQ(itemUse(world, mission.packet()).route, Route::Drop);
	auto option = use(1, uid(requester));
	option.option = 3;
	EXPECT_EQ(itemUse(world, option.packet()).route, Route::Drop);
	auto shorter = use(1, uid(requester));
	shorter.data.resize(4);
	EXPECT_EQ(itemUse(world, shorter.packet()).route, Route::Drop);
	auto stranger = use(1, uid(60));
	EXPECT_EQ(itemUse(world, stranger.packet()).route, Route::Drop);
}

TEST(MapItemSpawn, HostSpawnGoesToWholeMatch)
{
	FakeWorld world;
	auto plain = spawn(0);
	EXPECT_EQ(mapItemSpawn(world, plain.packet()).route, Route::Match);
	auto flagged = spawn(1);
	EXPECT_EQ(mapItemSpawn(world, flagged.packet()).route, Route::Match);
}

TEST(MapItemSpawn, OwnerlessAndUnexpiringAccepted)
{
	FakeWorld world;
	auto entry = mapItem();
	entry.owner = UniqueId{};
	entry.expireTick = 0;
	Frame frame{ 260, 0, 1 };
	frame.add(entry);
	EXPECT_EQ(mapItemSpawn(world, frame.packet()).route, Route::Match);
}

TEST(MapItemSpawn, MalformedOrFromPlayerDropped)
{
	FakeWorld world;
	auto fromPlayer = spawn().from(requester);
	EXPECT_EQ(mapItemSpawn(world, fromPlayer.packet()).route, Route::Drop);
	auto mission = spawn(2);
	EXPECT_EQ(mapItemSpawn(world, mission.packet()).route, Route::Drop);
	auto extra = spawn();
	extra.extra = 1;
	EXPECT_EQ(mapItemSpawn(world, extra.packet()).route, Route::Drop);
	auto option = spawn();
	option.option = 0;
	EXPECT_EQ(mapItemSpawn(world, option.packet()).route, Route::Drop);
	auto shorter = spawn();
	shorter.data.resize(24);
	EXPECT_EQ(mapItemSpawn(world, shorter.packet()).route, Route::Drop);
	auto marked = mapItem();
	marked.owner.unknown = 1;
	Frame bit31{ 260, 0, 1 };
	bit31.add(marked);
	EXPECT_EQ(mapItemSpawn(world, bit31.packet()).route, Route::Drop);
}

TEST(ItemGrant, BothClientVariantsGoToWholeMatch)
{
	FakeWorld world;
	auto modeGrant = grant(1, 1, 4);
	EXPECT_EQ(itemGrant(world, modeGrant.packet()).route, Route::Match);
	auto plainGrant = grant(0, 0, 0);
	EXPECT_EQ(itemGrant(world, plainGrant.packet()).route, Route::Match);
}

TEST(ItemGrant, BotUnitAccepted)
{
	FakeWorld world;
	world.roomMode = Common::Enums::AiBattle;
	auto packet = grantPacket();
	packet.player = uid(1000000 & 0xFFFF, 1000000 >> 16);
	Frame frame{ 261, 0, 0 };
	frame.add(packet);
	EXPECT_EQ(itemGrant(world, frame.packet()).route, Route::Match);
}

TEST(ItemGrant, MalformedOrFromPlayerDropped)
{
	FakeWorld world;
	auto fromPlayer = grant(0, 0, 0).from(requester);
	EXPECT_EQ(itemGrant(world, fromPlayer.packet()).route, Route::Drop);
	auto mixed = grant(1, 0, 0);
	EXPECT_EQ(itemGrant(world, mixed.packet()).route, Route::Drop);
	auto plainWithOption = grant(0, 0, 2);
	EXPECT_EQ(itemGrant(world, plainWithOption.packet()).route, Route::Drop);
	auto ignoredExtra = grant(0, 37, 0);
	EXPECT_EQ(itemGrant(world, ignoredExtra.packet()).route, Route::Drop);
	auto shorter = grant(0, 0, 0);
	shorter.data.resize(4);
	EXPECT_EQ(itemGrant(world, shorter.packet()).route, Route::Drop);

	auto notGranted = grantPacket();
	notGranted.item.granted = 0;
	Frame missingBit{ 261, 0, 0 };
	missingBit.add(notGranted);
	EXPECT_EQ(itemGrant(world, missingBit.packet()).route, Route::Drop);

	auto reserved = grantPacket();
	reserved.item.reserved = 1;
	Frame reservedBits{ 261, 0, 0 };
	reservedBits.add(reserved);
	EXPECT_EQ(itemGrant(world, reservedBits.packet()).route, Route::Drop);

	auto marked = grantPacket();
	marked.player.unknown = 1;
	Frame bit31{ 261, 0, 0 };
	bit31.add(marked);
	EXPECT_EQ(itemGrant(world, bit31.packet()).route, Route::Drop);
}
