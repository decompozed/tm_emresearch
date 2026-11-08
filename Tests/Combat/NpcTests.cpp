#include <gtest/gtest.h>
#include "FakeWorld.h"
#include "Structures/Npc/NpcPackets.h"

using namespace CombatTests;

namespace
{
	Frame npcMoveFrame(std::uint32_t count, std::size_t entries)
	{
		Frame frame{ 282, 0, 0 };
		frame.add(count);
		for (std::size_t i = 0; i < entries; ++i)
		{
			NpcMoveEntry entry{};
			entry.npcKey = 500 + static_cast<std::uint32_t>(i);
			entry.position = half3();
			frame.add(entry);
		}
		return frame;
	}

	Frame projectileFrame(std::uint8_t attack, std::uint32_t projectileId = 9)
	{
		NpcProjectilePacket packet{};
		packet.position = half3();
		packet.direction = half3(0x3C00, 0, 0);
		packet.shooter.session = 700;
		packet.projectileId = projectileId;
		Frame frame{ 326, 0, attack };
		frame.add(packet);
		return frame;
	}

	Frame spawnFrame(std::uint32_t key)
	{
		MonsterSpawnPacket packet{};
		packet.monsterKey = key;
		packet.position = half3();
		packet.scale = 0x3C00;
		packet.monsterInfoId = 7;
		Frame frame{ 328, 0, 0 };
		frame.add(packet);
		return frame;
	}

	Frame scheduleFrame(std::uint8_t kind, std::uint16_t count, std::uint16_t countCopy)
	{
		Frame frame{ 331, 0, kind };
		frame.add(BossScheduleHeader{ count, countCopy });
		frame.data.resize(bossScheduleSize);
		return frame;
	}
}

TEST(NpcMoves, HostListGoesToEveryoneElse)
{
	FakeWorld world;
	auto frame = npcMoveFrame(2, 2);
	EXPECT_EQ(npcMoves(world, frame.packet()).route, Route::MatchExceptSender);
}

TEST(NpcMoves, FortyFiveEntriesAccepted)
{
	FakeWorld world;
	auto frame = npcMoveFrame(45, 45);
	EXPECT_EQ(npcMoves(world, frame.packet()).route, Route::MatchExceptSender);
}

TEST(NpcMoves, CountOutOfRangeDropped)
{
	FakeWorld world;
	auto empty = npcMoveFrame(0, 0);
	EXPECT_EQ(npcMoves(world, empty.packet()).route, Route::Drop);
	auto tooMany = npcMoveFrame(46, 46);
	EXPECT_EQ(npcMoves(world, tooMany.packet()).route, Route::Drop);
}

TEST(NpcMoves, CountBeyondPayloadDropped)
{
	FakeWorld world;
	auto frame = npcMoveFrame(40, 1);
	EXPECT_EQ(npcMoves(world, frame.packet()).route, Route::Drop);
}

TEST(NpcMoves, TruncatedDropped)
{
	FakeWorld world;
	auto frame = npcMoveFrame(1, 0);
	frame.data.resize(2);
	EXPECT_EQ(npcMoves(world, frame.packet()).route, Route::Drop);
}

TEST(NpcMoves, FromNonHostDropped)
{
	FakeWorld world;
	auto frame = npcMoveFrame(1, 1).from(4);
	EXPECT_EQ(npcMoves(world, frame.packet()).route, Route::Drop);
}

TEST(NpcMoves, NonZeroHeaderDropped)
{
	FakeWorld world;
	auto frame = npcMoveFrame(1, 1);
	frame.extra = 3;
	EXPECT_EQ(npcMoves(world, frame.packet()).route, Route::Drop);
}

TEST(NpcProjectile, BothAttackIdsAccepted)
{
	FakeWorld world;
	auto first = projectileFrame(0);
	EXPECT_EQ(npcProjectile(world, first.packet()).route, Route::Match);
	auto second = projectileFrame(1);
	EXPECT_EQ(npcProjectile(world, second.packet()).route, Route::Match);
}

TEST(NpcProjectile, UnknownAttackIdDropped)
{
	FakeWorld world;
	auto frame = projectileFrame(2);
	EXPECT_EQ(npcProjectile(world, frame.packet()).route, Route::Drop);
}

TEST(NpcProjectile, ExtraDropped)
{
	FakeWorld world;
	auto frame = projectileFrame(0);
	frame.extra = 1;
	EXPECT_EQ(npcProjectile(world, frame.packet()).route, Route::Drop);
}

TEST(NpcProjectile, MissingProjectileIdDropped)
{
	FakeWorld world;
	auto frame = projectileFrame(0, 0);
	EXPECT_EQ(npcProjectile(world, frame.packet()).route, Route::Drop);
}

TEST(NpcProjectile, UnusedFieldSetDropped)
{
	FakeWorld world;
	auto frame = projectileFrame(0);
	frame.data[0] = 1;
	EXPECT_EQ(npcProjectile(world, frame.packet()).route, Route::Drop);
}

TEST(NpcProjectile, WrongSizeDropped)
{
	FakeWorld world;
	auto frame = projectileFrame(0);
	frame.data.resize(20);
	EXPECT_EQ(npcProjectile(world, frame.packet()).route, Route::Drop);
}

TEST(NpcProjectile, FromNonHostDropped)
{
	FakeWorld world;
	auto frame = projectileFrame(0).from(2);
	EXPECT_EQ(npcProjectile(world, frame.packet()).route, Route::Drop);
}

TEST(MonsterSpawn, HostSpawnGoesToWholeMatch)
{
	FakeWorld world;
	auto frame = spawnFrame(4000);
	EXPECT_EQ(monsterSpawn(world, frame.packet()).route, Route::Match);
}

TEST(MonsterSpawn, SameKeyAgainAccepted)
{
	FakeWorld world;
	auto first = spawnFrame(4000);
	EXPECT_EQ(monsterSpawn(world, first.packet()).route, Route::Match);
	auto again = spawnFrame(4000);
	EXPECT_EQ(monsterSpawn(world, again.packet()).route, Route::Match);
}

TEST(MonsterSpawn, WrongSizeDropped)
{
	FakeWorld world;
	auto frame = spawnFrame(4000);
	frame.data.resize(24);
	EXPECT_EQ(monsterSpawn(world, frame.packet()).route, Route::Drop);
}

TEST(MonsterSpawn, NonZeroHeaderDropped)
{
	FakeWorld world;
	auto frame = spawnFrame(4000).withMission(1);
	EXPECT_EQ(monsterSpawn(world, frame.packet()).route, Route::Drop);
}

TEST(MonsterSpawn, FromNonHostDropped)
{
	FakeWorld world;
	auto frame = spawnFrame(4000).from(3);
	EXPECT_EQ(monsterSpawn(world, frame.packet()).route, Route::Drop);
}

TEST(BossSchedule, BothKindsGoToWholeMatch)
{
	FakeWorld world;
	auto strikes = scheduleFrame(0, 5, 5);
	EXPECT_EQ(bossSchedule(world, strikes.packet()).route, Route::Match);
	auto targets = scheduleFrame(1, 118, 118);
	EXPECT_EQ(bossSchedule(world, targets.packet()).route, Route::Match);
}

TEST(BossSchedule, CountBeyondPacketDropped)
{
	FakeWorld world;
	auto frame = scheduleFrame(0, 119, 119);
	EXPECT_EQ(bossSchedule(world, frame.packet()).route, Route::Drop);
}

TEST(BossSchedule, CountCopyMismatchDropped)
{
	FakeWorld world;
	auto frame = scheduleFrame(0, 5, 6);
	EXPECT_EQ(bossSchedule(world, frame.packet()).route, Route::Drop);
}

TEST(BossSchedule, UnknownKindDropped)
{
	FakeWorld world;
	auto frame = scheduleFrame(2, 5, 5);
	EXPECT_EQ(bossSchedule(world, frame.packet()).route, Route::Drop);
}

TEST(BossSchedule, ShortPacketDropped)
{
	FakeWorld world;
	auto frame = scheduleFrame(0, 1, 1);
	frame.data.resize(16);
	EXPECT_EQ(bossSchedule(world, frame.packet()).route, Route::Drop);
}

TEST(BossSchedule, FromNonHostDropped)
{
	FakeWorld world;
	auto frame = scheduleFrame(0, 1, 1).from(2);
	EXPECT_EQ(bossSchedule(world, frame.packet()).route, Route::Drop);
}
