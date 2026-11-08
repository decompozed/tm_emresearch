#include <gtest/gtest.h>
#include "FakeWorld.h"

using namespace CombatTests;

namespace
{
	Frame fall(std::uint32_t victim)
	{
		Frame frame{ 264, 0, 1 };
		frame.add(makeFallDeath(uid(victim), half3()));
		return frame;
	}

	Frame explosion(std::uint8_t extra, std::uint32_t source, std::initializer_list<DamageEntry> entries)
	{
		Frame frame{ 264, extra, static_cast<std::uint8_t>(entries.size()) };
		ExplosionDamageHeader header{};
		header.sourceId = source;
		header.position = half3();
		frame.add(header);
		for (const auto& value : entries)
		{
			frame.add(value);
		}
		return frame;
	}
}

TEST(ExplosionDamage, ServerBuiltFallDeathIs24BytesAndValid)
{
	FakeWorld world;
	auto frame = fall(3);
	ASSERT_EQ(frame.data.size(), 24u);
	EXPECT_EQ(frame.at<std::uint32_t>(0), raw(uid(3)));
	EXPECT_EQ(frame.at<std::uint32_t>(12), raw(uid(3)));
	EXPECT_EQ(frame.at<std::uint32_t>(16), 0u);

	const auto verdict = explosionDamage(world, frame.packet());
	EXPECT_EQ(verdict.route, Route::Match);
	ASSERT_EQ(world.events.size(), 1u);
	EXPECT_TRUE(world.events[0].death);
	EXPECT_FALSE(world.events[0].attacker);
	EXPECT_TRUE(world.players[3].dead);
}

TEST(ExplosionDamage, FallDeathFromNonHostDropped)
{
	FakeWorld world;
	auto frame = fall(3).from(2);
	EXPECT_EQ(explosionDamage(world, frame.packet()).route, Route::Drop);
	EXPECT_TRUE(world.events.empty());
}

TEST(ExplosionDamage, FallDeathWithUnknownHostDropped)
{
	FakeWorld world;
	world.host.reset();
	auto frame = fall(3);
	EXPECT_EQ(explosionDamage(world, frame.packet()).route, Route::Drop);
}

TEST(ExplosionDamage, OldTwentyBytePacketDropped)
{
	FakeWorld world;
	auto frame = fall(3);
	frame.data.resize(20);
	EXPECT_EQ(explosionDamage(world, frame.packet()).route, Route::Drop);
}

TEST(ExplosionDamage, FallDeathSourceMustBeTheVictim)
{
	FakeWorld world;
	auto frame = fall(3);
	const auto other = raw(uid(2));
	std::memcpy(frame.data.data(), &other, sizeof(other));
	EXPECT_EQ(explosionDamage(world, frame.packet()).route, Route::Drop);
}

TEST(ExplosionDamage, FallDeathWithHpDropped)
{
	FakeWorld world;
	auto frame = fall(3);
	const auto hp = hpField(50);
	std::memcpy(frame.data.data() + 16, &hp, sizeof(hp));
	EXPECT_EQ(explosionDamage(world, frame.packet()).route, Route::Drop);
}

TEST(ExplosionDamage, FallDeathOfDeadVictimForwardedWithoutDeath)
{
	FakeWorld world;
	world.players[3].dead = true;
	auto frame = fall(3);
	EXPECT_EQ(explosionDamage(world, frame.packet()).route, Route::Match);
	EXPECT_TRUE(world.events.empty());
}

TEST(ExplosionDamage, FallDeathWrongOptionDropped)
{
	FakeWorld world;
	auto frame = fall(3);
	frame.option = 0;
	EXPECT_EQ(explosionDamage(world, frame.packet()).route, Route::Drop);
}

TEST(ExplosionDamage, MultiEntryForwardedOnceWithEveryEntryApplied)
{
	FakeWorld world;
	world.explosives[42] = 2;
	auto frame = explosion(1, 42, { entry(3, 0), entry(4, 0), entry(1, 30) });
	const auto verdict = explosionDamage(world, frame.packet());
	EXPECT_EQ(verdict.route, Route::Match);
	EXPECT_EQ(verdict.option, 3);
	EXPECT_EQ(verdict.size, frame.data.size());
	ASSERT_EQ(world.events.size(), 3u);
	EXPECT_TRUE(world.events[0].death);
	EXPECT_TRUE(world.events[1].death);
	EXPECT_FALSE(world.events[2].death);
	EXPECT_EQ(world.events[0].attacker, 2u);
	EXPECT_EQ(world.events[0].cause, Cause::Explosive);
	EXPECT_TRUE(world.players[3].dead);
	EXPECT_TRUE(world.players[4].dead);
}

TEST(ExplosionDamage, WithoutTargetsForwarded)
{
	FakeWorld world;
	auto frame = explosion(1, 42, {});
	EXPECT_EQ(explosionDamage(world, frame.packet()).route, Route::Match);
}

TEST(ExplosionDamage, SizeNotMatchingOptionDropped)
{
	FakeWorld world;
	auto frame = explosion(1, 42, { entry(3, 0) });
	frame.option = 2;
	EXPECT_EQ(explosionDamage(world, frame.packet()).route, Route::Drop);
}

TEST(ExplosionDamage, UnknownExtraDropped)
{
	FakeWorld world;
	auto frame = explosion(3, 42, { entry(3, 0) });
	EXPECT_EQ(explosionDamage(world, frame.packet()).route, Route::Drop);
}

TEST(ExplosionDamage, MissionDropped)
{
	FakeWorld world;
	auto frame = explosion(1, 42, { entry(3, 0) }).withMission(1);
	EXPECT_EQ(explosionDamage(world, frame.packet()).route, Route::Drop);
}

TEST(ExplosionDamage, TargetOutsideRoomDropped)
{
	FakeWorld world;
	auto frame = explosion(1, 42, { entry(3, 0), entry(99, 0) });
	EXPECT_EQ(explosionDamage(world, frame.packet()).route, Route::Drop);
	EXPECT_TRUE(world.events.empty());
}

TEST(ExplosionDamage, TargetOfAnotherServerDropped)
{
	FakeWorld world;
	DamageEntry foreign = entry(3, 0);
	foreign.target = uid(3, server + 1);
	auto frame = explosion(1, 42, { foreign });
	EXPECT_EQ(explosionDamage(world, frame.packet()).route, Route::Drop);
}

TEST(ExplosionDamage, ObserverTargetDropped)
{
	FakeWorld world;
	world.players[3].observer = true;
	auto frame = explosion(1, 42, { entry(3, 0) });
	EXPECT_EQ(explosionDamage(world, frame.packet()).route, Route::Drop);
}

TEST(ExplosionDamage, RepeatedPlayerTargetForwarded)
{
	FakeWorld world;
	auto frame = explosion(1, 42, { entry(3, 10), entry(4, 10), entry(3, 0) });
	EXPECT_EQ(explosionDamage(world, frame.packet()).route, Route::Match);
}

TEST(ExplosionDamage, RepeatedNpcTargetForwarded)
{
	FakeWorld world;
	world.roomMode = Common::Enums::BossBattle;
	DamageEntry part{};
	part.target = uid(500, 0);
	auto frame = explosion(1, 42, { part, part, entry(3, 10) });
	EXPECT_EQ(explosionDamage(world, frame.packet()).route, Route::Match);
}

TEST(ExplosionDamage, ReservedHpBitsDropped)
{
	FakeWorld world;
	DamageEntry bad = entry(3, 0);
	bad.hp.reserved = 1;
	auto frame = explosion(1, 42, { bad });
	EXPECT_EQ(explosionDamage(world, frame.packet()).route, Route::Drop);
}

TEST(ExplosionDamage, NonFinitePositionDropped)
{
	FakeWorld world;
	auto frame = explosion(1, 42, { entry(3, 0) });
	const std::uint16_t nan = 0x7E00;
	std::memcpy(frame.data.data() + 4, &nan, sizeof(nan));
	EXPECT_EQ(explosionDamage(world, frame.packet()).route, Route::Drop);
}

TEST(ExplosionDamage, EntriesOnDeadTargetsKeptWithoutEvents)
{
	FakeWorld world;
	world.players[3].dead = true;
	auto frame = explosion(2, raw(uid(2)), { entry(3, 0), entry(4, 25) });
	const auto verdict = explosionDamage(world, frame.packet());
	ASSERT_EQ(verdict.route, Route::Match);
	EXPECT_EQ(verdict.option, 2);
	EXPECT_EQ(verdict.size, sizeof(ExplosionDamageHeader) + 2 * sizeof(DamageEntry));
	EXPECT_EQ(raw(frame.at<DamageEntry>(sizeof(ExplosionDamageHeader)).target), raw(uid(3)));
	ASSERT_EQ(world.events.size(), 1u);
	EXPECT_EQ(world.events[0].target, 4u);
	EXPECT_EQ(world.events[0].attacker, 2u);
}

TEST(ExplosionDamage, EveryTargetDeadForwarded)
{
	FakeWorld world;
	world.players[3].dead = true;
	auto frame = explosion(1, 42, { entry(3, 0) });
	EXPECT_EQ(explosionDamage(world, frame.packet()).route, Route::Match);
	EXPECT_TRUE(world.events.empty());
}

TEST(ExplosionDamage, AreaAttackerOutsideRoomDropped)
{
	FakeWorld world;
	auto frame = explosion(2, raw(uid(99)), { entry(3, 0) });
	EXPECT_EQ(explosionDamage(world, frame.packet()).route, Route::Drop);
}

TEST(ExplosionDamage, NpcTargetsOnlyInAiModes)
{
	FakeWorld world;
	DamageEntry npc{};
	npc.target = uid(500, 0);
	auto frame = explosion(1, 42, { npc });
	EXPECT_EQ(explosionDamage(world, frame.packet()).route, Route::Drop);

	world.roomMode = Common::Enums::AiBattle;
	EXPECT_EQ(explosionDamage(world, frame.packet()).route, Route::Match);
	EXPECT_TRUE(world.events.empty());
}

TEST(ExplosionDamage, LavaSparesLethalExplosionOfAnotherPlayer)
{
	FakeWorld world;
	world.spares = true;
	world.explosives[42] = 2;
	auto frame = explosion(1, 42, { entry(3, 0) });
	ASSERT_EQ(explosionDamage(world, frame.packet()).route, Route::Match);
	EXPECT_EQ(frame.at<DamageEntry>(sizeof(ExplosionDamageHeader)).hp.hp, 1u);
	ASSERT_EQ(world.events.size(), 1u);
	EXPECT_TRUE(world.events[0].spared);
	EXPECT_FALSE(world.players[3].dead);
}

TEST(ExplosionDamage, LavaDoesNotSpareOwnExplosion)
{
	FakeWorld world;
	world.spares = true;
	world.explosives[42] = 3;
	auto frame = explosion(1, 42, { entry(3, 0) });
	ASSERT_EQ(explosionDamage(world, frame.packet()).route, Route::Match);
	EXPECT_TRUE(world.players[3].dead);
}
