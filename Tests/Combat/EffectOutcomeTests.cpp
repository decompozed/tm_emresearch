#include <gtest/gtest.h>
#include "FakeWorld.h"

using namespace CombatTests;

namespace
{
	Frame itemEffectFrame(std::uint32_t target, std::uint32_t user)
	{
		ItemEffectPacket effect{};
		effect.target = uid(target);
		effect.itemId = 1234;
		effect.user = uid(user);
		Frame frame{ 265, 0, 0 };
		frame.add(effect);
		return frame;
	}

	Frame stateKillFrame(std::uint32_t attacker, std::uint32_t victim, std::uint32_t effectId = 130)
	{
		StateKillPacket kill{};
		kill.attacker = uid(attacker);
		kill.victim = uid(victim);
		kill.effectId = effectId;
		Frame frame{ 265, 1, 0 };
		frame.add(kill);
		return frame;
	}
}

TEST(ItemEffect, ForwardedToMatch)
{
	FakeWorld world;
	auto frame = itemEffectFrame(3, 2);
	EXPECT_EQ(effectOutcome(world, frame.packet()).route, Route::Match);
	EXPECT_TRUE(world.events.empty());
}

TEST(ItemEffect, FromNonHostDropped)
{
	FakeWorld world;
	auto frame = itemEffectFrame(3, 2).from(2);
	EXPECT_EQ(effectOutcome(world, frame.packet()).route, Route::Drop);
}

TEST(ItemEffect, WrongSizeDropped)
{
	FakeWorld world;
	auto frame = itemEffectFrame(3, 2);
	frame.data.resize(8);
	EXPECT_EQ(effectOutcome(world, frame.packet()).route, Route::Drop);
}

TEST(ItemEffect, OptionDropped)
{
	FakeWorld world;
	auto frame = itemEffectFrame(3, 2);
	frame.option = 1;
	EXPECT_EQ(effectOutcome(world, frame.packet()).route, Route::Drop);
}

TEST(ItemEffect, DeadTargetForwarded)
{
	FakeWorld world;
	world.players[3].dead = true;
	auto frame = itemEffectFrame(3, 2);
	EXPECT_EQ(effectOutcome(world, frame.packet()).route, Route::Match);
}

TEST(ItemEffect, UserOutsideRoomDropped)
{
	FakeWorld world;
	auto frame = itemEffectFrame(3, 50);
	EXPECT_EQ(effectOutcome(world, frame.packet()).route, Route::Drop);
}

TEST(StateKill, KillsVictimWithAttacker)
{
	FakeWorld world;
	auto frame = stateKillFrame(2, 3);
	EXPECT_EQ(effectOutcome(world, frame.packet()).route, Route::Match);
	ASSERT_EQ(world.events.size(), 1u);
	EXPECT_TRUE(world.events[0].death);
	EXPECT_EQ(world.events[0].attacker, 2u);
	EXPECT_TRUE(world.players[3].dead);
}

TEST(StateKill, UnknownEffectDropped)
{
	FakeWorld world;
	auto frame = stateKillFrame(2, 3, 131);
	EXPECT_EQ(effectOutcome(world, frame.packet()).route, Route::Drop);
	EXPECT_FALSE(world.players[3].dead);
}

TEST(StateKill, DeadVictimForwardedWithoutDeath)
{
	FakeWorld world;
	world.players[3].dead = true;
	auto frame = stateKillFrame(2, 3);
	EXPECT_EQ(effectOutcome(world, frame.packet()).route, Route::Match);
	EXPECT_TRUE(world.events.empty());
}

TEST(StateKill, FromNonHostDropped)
{
	FakeWorld world;
	auto frame = stateKillFrame(2, 3).from(2);
	EXPECT_EQ(effectOutcome(world, frame.packet()).route, Route::Drop);
}

TEST(EffectOutcome, UnknownExtraDropped)
{
	FakeWorld world;
	auto frame = stateKillFrame(2, 3);
	frame.extra = 2;
	EXPECT_EQ(effectOutcome(world, frame.packet()).route, Route::Drop);
}
