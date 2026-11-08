#include <gtest/gtest.h>
#include "FakeWorld.h"

using namespace CombatTests;

namespace
{
	Frame single(std::uint16_t order, std::uint32_t attacker, std::uint32_t target, std::uint32_t hp, std::uint8_t option = 0)
	{
		SingleHitPacket shot{};
		shot.trace = trace();
		shot.attacker = uid(attacker);
		shot.target = uid(target);
		shot.hp = hpField(hp, 2);
		Frame frame{ order, 1, option };
		frame.add(shot);
		return frame;
	}

	Frame multi(std::uint16_t order, std::uint32_t attacker, std::initializer_list<DamageEntry> entries)
	{
		MultiHitHeader header{};
		header.trace = trace();
		header.attacker = uid(attacker);
		Frame frame{ order, 1, static_cast<std::uint8_t>(entries.size()) };
		frame.add(header);
		for (const auto& value : entries)
		{
			frame.add(value);
		}
		return frame;
	}
}

TEST(SingleHit, LethalHitKillsTargetAndIsForwarded)
{
	FakeWorld world;
	auto frame = single(268, 2, 3, 0);
	EXPECT_EQ(singleHit(world, frame.packet()).route, Route::Match);
	ASSERT_EQ(world.events.size(), 1u);
	EXPECT_TRUE(world.events[0].death);
	EXPECT_EQ(world.events[0].attacker, 2u);
	EXPECT_EQ(world.events[0].cause, Cause::Weapon);
	EXPECT_TRUE(world.players[3].dead);
}

TEST(SingleHit, DamageReportsHit)
{
	FakeWorld world;
	auto frame = single(270, 2, 3, 40, 1);
	EXPECT_EQ(singleHit(world, frame.packet()).route, Route::Match);
	ASSERT_EQ(world.events.size(), 1u);
	EXPECT_FALSE(world.events[0].death);
	EXPECT_FALSE(world.players[3].dead);
}

TEST(SingleHit, FromNonHostDropped)
{
	FakeWorld world;
	auto frame = single(268, 2, 3, 0).from(2);
	EXPECT_EQ(singleHit(world, frame.packet()).route, Route::Drop);
	EXPECT_FALSE(world.players[3].dead);
}

TEST(SingleHit, MissExtraDropped)
{
	FakeWorld world;
	auto frame = single(268, 2, 3, 0);
	frame.extra = 0;
	EXPECT_EQ(singleHit(world, frame.packet()).route, Route::Drop);
}

TEST(SingleHit, WrongSizeDropped)
{
	FakeWorld world;
	auto frame = single(267, 2, 3, 0);
	frame.data.resize(28);
	EXPECT_EQ(singleHit(world, frame.packet()).route, Route::Drop);
}

TEST(SingleHit, DeadTargetForwardedWithoutEvents)
{
	FakeWorld world;
	world.players[3].dead = true;
	auto frame = single(267, 2, 3, 0);
	EXPECT_EQ(singleHit(world, frame.packet()).route, Route::Match);
	EXPECT_TRUE(world.events.empty());
}

TEST(SingleHit, TargetOutsideRoomDropped)
{
	FakeWorld world;
	auto frame = single(267, 2, 99, 0);
	EXPECT_EQ(singleHit(world, frame.packet()).route, Route::Drop);
}

TEST(SingleHit, DeadAttackerForwarded)
{
	FakeWorld world;
	world.players[2].dead = true;
	auto frame = single(267, 2, 3, 0);
	EXPECT_EQ(singleHit(world, frame.packet()).route, Route::Match);
	EXPECT_TRUE(world.players[2].dead);
}

TEST(SingleHit, SelfHitDropped)
{
	FakeWorld world;
	auto frame = single(268, 3, 3, 0);
	EXPECT_EQ(singleHit(world, frame.packet()).route, Route::Drop);
}

TEST(SingleHit, InfectionMissionOnlyForMeleeInZombieMode)
{
	FakeWorld world;
	auto melee = single(267, 2, 3, 1).withMission(1);
	EXPECT_EQ(singleHit(world, melee.packet()).route, Route::Drop);

	world.roomMode = Common::Enums::ZombieMode;
	EXPECT_EQ(singleHit(world, melee.packet()).route, Route::Match);
	EXPECT_FALSE(world.players[3].dead);

	auto rifle = single(268, 2, 4, 1).withMission(1);
	EXPECT_EQ(singleHit(world, rifle.packet()).route, Route::Drop);
}

TEST(SingleHit, RifleOptionMustBeZero)
{
	FakeWorld world;
	auto frame = single(268, 2, 3, 10, 1);
	EXPECT_EQ(singleHit(world, frame.packet()).route, Route::Drop);
}

TEST(SingleHit, MeleeOptionAboveOneDropped)
{
	FakeWorld world;
	auto frame = single(267, 2, 3, 10, 2);
	EXPECT_EQ(singleHit(world, frame.packet()).route, Route::Drop);
}

TEST(SingleHit, TraceTagDropped)
{
	FakeWorld world;
	auto frame = single(268, 2, 3, 10);
	const std::uint32_t tag = 5;
	std::memcpy(frame.data.data(), &tag, sizeof(tag));
	EXPECT_EQ(singleHit(world, frame.packet()).route, Route::Drop);
}

TEST(SingleHit, NonFiniteTraceDropped)
{
	FakeWorld world;
	auto frame = single(268, 2, 3, 10);
	const std::uint16_t infinity = 0x7C00;
	std::memcpy(frame.data.data() + 10, &infinity, sizeof(infinity));
	EXPECT_EQ(singleHit(world, frame.packet()).route, Route::Drop);
}

TEST(SingleHit, ReservedHpBitsDropped)
{
	FakeWorld world;
	auto frame = single(268, 2, 3, 10);
	auto shot = frame.at<SingleHitPacket>(0);
	shot.hp.reserved = 0x40;
	std::memcpy(frame.data.data(), &shot, sizeof(shot));
	EXPECT_EQ(singleHit(world, frame.packet()).route, Route::Drop);
}

TEST(SingleHit, DisarmedAttackerDropped)
{
	FakeWorld world;
	world.disarmedPlayers.insert(2);
	auto frame = single(267, 2, 3, 0);
	EXPECT_EQ(singleHit(world, frame.packet()).route, Route::Drop);
}

TEST(SingleHit, LavaSpareRewritesLethalHit)
{
	FakeWorld world;
	world.spares = true;
	auto frame = single(268, 2, 3, 0);
	ASSERT_EQ(singleHit(world, frame.packet()).route, Route::Match);
	const auto shot = frame.at<SingleHitPacket>(0);
	EXPECT_EQ(shot.hp.hp, 1u);
	EXPECT_EQ(shot.hp.hitPart, 2u);
	ASSERT_EQ(world.events.size(), 1u);
	EXPECT_TRUE(world.events[0].spared);
	EXPECT_FALSE(world.players[3].dead);
}

TEST(MultiHit, ForwardedOnceAndEveryPelletApplied)
{
	FakeWorld world;
	auto frame = multi(269, 2, { entry(3, 20), entry(3, 0), entry(4, 50) });
	const auto verdict = multiHit(world, frame.packet());
	EXPECT_EQ(verdict.route, Route::Match);
	EXPECT_EQ(verdict.option, 3);
	ASSERT_EQ(world.events.size(), 3u);
	EXPECT_FALSE(world.events[0].death);
	EXPECT_TRUE(world.events[1].death);
	EXPECT_EQ(world.events[2].target, 4u);
	EXPECT_TRUE(world.players[3].dead);
}

TEST(MultiHit, PelletsAfterTheKillKeptWithoutSecondDeath)
{
	FakeWorld world;
	auto frame = multi(269, 2, { entry(3, 0), entry(3, 0) });
	const auto verdict = multiHit(world, frame.packet());
	EXPECT_EQ(verdict.route, Route::Match);
	EXPECT_EQ(verdict.option, 2);
	EXPECT_EQ(world.events.size(), 1u);
}

TEST(MultiHit, OptionZeroDropped)
{
	FakeWorld world;
	auto frame = multi(266, 2, {});
	EXPECT_EQ(multiHit(world, frame.packet()).route, Route::Drop);
}

TEST(MultiHit, SizeNotMatchingOptionDropped)
{
	FakeWorld world;
	auto frame = multi(266, 2, { entry(3, 10), entry(4, 10) });
	frame.data.resize(frame.data.size() - 4);
	EXPECT_EQ(multiHit(world, frame.packet()).route, Route::Drop);
}

TEST(MultiHit, AttackerAmongTargetsDropped)
{
	FakeWorld world;
	auto frame = multi(266, 2, { entry(3, 10), entry(2, 10) });
	EXPECT_EQ(multiHit(world, frame.packet()).route, Route::Drop);
}

TEST(MultiHit, FromNonHostDropped)
{
	FakeWorld world;
	auto frame = multi(266, 2, { entry(3, 0) }).from(3);
	EXPECT_EQ(multiHit(world, frame.packet()).route, Route::Drop);
	EXPECT_FALSE(world.players[3].dead);
}

TEST(MultiHit, TargetOutsideRoomDropped)
{
	FakeWorld world;
	auto frame = multi(266, 2, { entry(3, 0), entry(77, 0) });
	EXPECT_EQ(multiHit(world, frame.packet()).route, Route::Drop);
	EXPECT_FALSE(world.players[3].dead);
}

TEST(MultiHit, DeadTargetEntriesKept)
{
	FakeWorld world;
	world.players[4].dead = true;
	auto frame = multi(266, 2, { entry(4, 0), entry(3, 15) });
	const auto verdict = multiHit(world, frame.packet());
	ASSERT_EQ(verdict.route, Route::Match);
	EXPECT_EQ(verdict.option, 2);
	EXPECT_EQ(verdict.size, sizeof(MultiHitHeader) + 2 * sizeof(DamageEntry));
	ASSERT_EQ(world.events.size(), 1u);
	EXPECT_EQ(world.events[0].target, 3u);
}

TEST(MultiHit, ShotgunMissForwardedWithoutEffects)
{
	FakeWorld world;
	auto frame = multi(269, 2, {});
	frame.extra = 0;
	EXPECT_EQ(multiHit(world, frame.packet()).route, Route::Match);
	EXPECT_TRUE(world.events.empty());
}

TEST(MultiHit, GatlingMissDropped)
{
	FakeWorld world;
	auto frame = multi(266, 2, {});
	frame.extra = 0;
	EXPECT_EQ(multiHit(world, frame.packet()).route, Route::Drop);
}

TEST(MultiHit, LavaSparesEveryEntry)
{
	FakeWorld world;
	world.spares = true;
	auto frame = multi(266, 2, { entry(3, 0), entry(4, 0) });
	ASSERT_EQ(multiHit(world, frame.packet()).route, Route::Match);
	EXPECT_EQ(frame.at<DamageEntry>(sizeof(MultiHitHeader)).hp.hp, 1u);
	EXPECT_EQ(frame.at<DamageEntry>(sizeof(MultiHitHeader) + sizeof(DamageEntry)).hp.hp, 1u);
	EXPECT_FALSE(world.players[3].dead);
	EXPECT_FALSE(world.players[4].dead);
}
