#include <algorithm>
#include <gtest/gtest.h>
#include "FakeWorld.h"
#include "Match/Rules.h"
#include "Structures/Match/MatchFlowPackets.h"
#include "Structures/Respawn/RespawnPackets.h"

using namespace CombatTests;

namespace
{
	constexpr std::uint32_t shooter = 2;
	constexpr std::uint32_t victim = 3;
	constexpr std::uint32_t other = 4;

	Frame rifleHit(std::uint32_t attacker, std::uint32_t target, std::uint32_t hp)
	{
		SingleHitPacket shot{};
		shot.trace = trace();
		shot.attacker = uid(attacker);
		shot.target = uid(target);
		shot.hp = hpField(hp, 2);
		Frame frame{ 268, 1, 0 };
		frame.add(shot);
		return frame;
	}

	Frame shotgunHit(std::uint32_t attacker, std::initializer_list<DamageEntry> entries)
	{
		MultiHitHeader header{};
		header.trace = trace();
		header.attacker = uid(attacker);
		Frame frame{ 269, 1, static_cast<std::uint8_t>(entries.size()) };
		frame.add(header);
		for (const auto& value : entries)
		{
			frame.add(value);
		}
		return frame;
	}

	Frame rifleRequest(std::uint32_t sender, std::uint32_t target)
	{
		HitRequest request{};
		request.trace = trace();
		request.hit.target = uid(target);
		request.hit.hitPoint = half3();
		request.hit.hitPart = 1;
		Frame frame{ 152, 1, 0 };
		frame.add(request).from(sender);
		return frame;
	}

	Frame fall(std::uint32_t player)
	{
		Frame frame{ 264, 0, 1 };
		frame.add(makeFallDeath(uid(player), half3()));
		return frame;
	}

	Frame launch(std::uint32_t owner)
	{
		ProjectileLaunchPacket packet{};
		packet.trace = trace();
		packet.owner = uid(owner);
		packet.ammoId = 77;
		Frame frame{ 272, 0, 2 };
		frame.add(packet);
		return frame;
	}

	Frame reloadOf(std::uint32_t player)
	{
		ReloadPacket packet{};
		packet.request.magazineAmmo = 30;
		packet.player = uid(player);
		Frame frame{ 275, 0, 0 };
		frame.add(packet);
		return frame;
	}

	Frame stateKill(std::uint32_t attacker, std::uint32_t target)
	{
		StateKillPacket kill{};
		kill.attacker = uid(attacker);
		kill.victim = uid(target);
		kill.effectId = 130;
		Frame frame{ 265, 1, 0 };
		frame.add(kill);
		return frame;
	}

	Frame respawnOf(std::uint32_t player)
	{
		RespawnPacket packet{};
		packet.position = half3();
		packet.unit = uid(player);
		Frame frame{ 276, 0, 0 };
		frame.add(packet);
		return frame;
	}

	Frame loadingDoneOf(std::uint32_t player)
	{
		Frame frame{ 257, 0, loadingDone };
		frame.from(player);
		return frame;
	}

	void holdDead(FakeWorld& world, std::uint32_t player, bool stale)
	{
		world.players[player].dead = true;
		world.players[player].staleDeath = stale;
	}

	std::size_t deathsOf(const FakeWorld& world, std::uint32_t player)
	{
		return static_cast<std::size_t>(std::ranges::count_if(world.events, [&](const Event& event) { return event.death && event.target == player; }));
	}
}

TEST(DeadState, LethalHitOfAShooterHeldDeadIsForwardedAndRecorded)
{
	FakeWorld world;
	holdDead(world, shooter, true);
	auto frame = rifleHit(shooter, victim, 0);
	EXPECT_EQ(singleHit(world, frame.packet()).route, Route::Match);
	EXPECT_EQ(deathsOf(world, victim), 1u);
	EXPECT_FALSE(world.players[shooter].dead);
	EXPECT_EQ(world.revived, std::vector<std::uint32_t>{ shooter });
}

TEST(DeadState, ShooterJustKilledStaysDeadButItsHitIsForwarded)
{
	FakeWorld world;
	holdDead(world, shooter, false);
	auto frame = rifleHit(shooter, victim, 40);
	EXPECT_EQ(singleHit(world, frame.packet()).route, Route::Match);
	EXPECT_TRUE(world.players[shooter].dead);
	EXPECT_TRUE(world.revived.empty());
	ASSERT_EQ(world.events.size(), 1u);
	EXPECT_FALSE(world.events[0].death);
}

TEST(DeadState, ShotgunHitOfAShooterHeldDeadIsForwarded)
{
	FakeWorld world;
	holdDead(world, shooter, true);
	auto frame = shotgunHit(shooter, { entry(victim, 0) });
	EXPECT_EQ(multiHit(world, frame.packet()).route, Route::Match);
	EXPECT_EQ(deathsOf(world, victim), 1u);
	EXPECT_FALSE(world.players[shooter].dead);
}

TEST(DeadState, RequestOfAPlayerHeldDeadGoesToTheHost)
{
	FakeWorld world;
	holdDead(world, victim, true);
	auto frame = rifleRequest(victim, other);
	EXPECT_EQ(singleHitRequest(world, frame.packet()).route, Route::Host);
	EXPECT_TRUE(world.revived.empty());
}

TEST(DeadState, RequestSentJustBeforeTheDeathGoesToTheHost)
{
	FakeWorld world;
	holdDead(world, victim, false);
	auto frame = rifleRequest(victim, other);
	EXPECT_EQ(singleHitRequest(world, frame.packet()).route, Route::Host);
	EXPECT_TRUE(world.players[victim].dead);
	EXPECT_TRUE(world.revived.empty());
}

TEST(DeadState, ObserverRequestStillDropped)
{
	FakeWorld world;
	world.players[victim].observer = true;
	auto frame = rifleRequest(victim, other);
	EXPECT_EQ(singleHitRequest(world, frame.packet()).route, Route::Drop);
}

TEST(DeadState, LethalHitOnATargetHeldDeadIsForwarded)
{
	FakeWorld world;
	holdDead(world, victim, true);
	auto frame = rifleHit(shooter, victim, 0);
	EXPECT_EQ(singleHit(world, frame.packet()).route, Route::Match);
	EXPECT_TRUE(world.events.empty());
	EXPECT_TRUE(world.players[victim].dead);
}

TEST(DeadState, HitOnAJustKilledTargetIsForwardedWithoutASecondDeath)
{
	FakeWorld world;
	holdDead(world, victim, false);
	auto frame = rifleHit(shooter, victim, 0);
	EXPECT_EQ(singleHit(world, frame.packet()).route, Route::Match);
	EXPECT_TRUE(world.events.empty());
}

TEST(DeadState, FallDeathOfAVictimHeldDeadIsForwardedOnce)
{
	FakeWorld world;
	auto first = fall(victim);
	EXPECT_EQ(explosionDamage(world, first.packet()).route, Route::Match);
	auto copy = fall(victim);
	EXPECT_EQ(explosionDamage(world, copy.packet()).route, Route::Match);
	EXPECT_EQ(deathsOf(world, victim), 1u);
}

TEST(DeadState, ExplosionKeepsTheEntriesOfDeadTargets)
{
	FakeWorld world;
	holdDead(world, other, false);
	Frame frame{ 264, 2, 2 };
	ExplosionDamageHeader header{};
	header.sourceId = raw(uid(shooter));
	header.position = half3();
	frame.add(header).add(entry(other, 0)).add(entry(victim, 0));
	const auto verdict = explosionDamage(world, frame.packet());
	ASSERT_EQ(verdict.route, Route::Match);
	EXPECT_EQ(verdict.option, 2);
	EXPECT_EQ(verdict.size, frame.data.size());
	EXPECT_EQ(deathsOf(world, victim), 1u);
	EXPECT_EQ(deathsOf(world, other), 0u);
}

TEST(DeadState, ExplosionOnlyOnDeadTargetsIsForwarded)
{
	FakeWorld world;
	holdDead(world, victim, false);
	auto frame = shotgunHit(shooter, { entry(victim, 0) });
	EXPECT_EQ(multiHit(world, frame.packet()).route, Route::Match);
	EXPECT_TRUE(world.events.empty());
}

TEST(DeadState, LaunchOfAnOwnerHeldDeadIsForwarded)
{
	FakeWorld world;
	holdDead(world, shooter, true);
	auto frame = launch(shooter);
	EXPECT_EQ(projectileLaunch(world, frame.packet()).route, Route::Match);
	EXPECT_EQ(world.explosives[77], shooter);
}

TEST(DeadState, ReloadOfAPlayerHeldDeadIsForwarded)
{
	FakeWorld world;
	holdDead(world, shooter, true);
	auto frame = reloadOf(shooter);
	EXPECT_EQ(reload(world, frame.packet()).route, Route::Match);
}

TEST(DeadState, StateKillOfAVictimHeldDeadIsForwarded)
{
	FakeWorld world;
	holdDead(world, victim, false);
	auto frame = stateKill(shooter, victim);
	EXPECT_EQ(effectOutcome(world, frame.packet()).route, Route::Match);
	EXPECT_TRUE(world.events.empty());
}

TEST(DeadState, OutOfTheMatchShooterStillDropped)
{
	FakeWorld world;
	world.players[shooter].inMatch = false;
	auto frame = rifleHit(shooter, victim, 0);
	EXPECT_EQ(singleHit(world, frame.packet()).route, Route::Drop);
	EXPECT_TRUE(world.events.empty());
}

TEST(DeadState, LoadingDoneAfterTheRespawnDoesNotKillAgain)
{
	FakeWorld world;
	auto respawn = respawnOf(victim);
	ASSERT_EQ(playerRespawn(world, respawn.packet()).route, Route::Match);
	auto loaded = loadingDoneOf(victim);
	EXPECT_EQ(Cast::Match::loadingProgress(world, loaded.packet()).route, Route::Handled);
	EXPECT_TRUE(world.loaded.empty());
	EXPECT_FALSE(world.players[victim].dead);
	auto kill = rifleHit(victim, other, 0);
	EXPECT_EQ(singleHit(world, kill.packet()).route, Route::Match);
	EXPECT_EQ(deathsOf(world, other), 1u);
}

TEST(DeadState, LoadingDoneOfAPlayerNotInTheMatchMarksItLoaded)
{
	FakeWorld world;
	world.players[victim].inMatch = false;
	auto loaded = loadingDoneOf(victim);
	EXPECT_EQ(Cast::Match::loadingProgress(world, loaded.packet()).route, Route::Handled);
	EXPECT_EQ(world.loaded, std::vector<std::uint32_t>{ victim });
}

TEST(DeadState, HeldDeadPlayerResynchronisesOverAWholeLife)
{
	FakeWorld world;
	holdDead(world, shooter, true);
	auto request = rifleRequest(shooter, victim);
	EXPECT_EQ(singleHitRequest(world, request.packet()).route, Route::Host);
	auto kill = rifleHit(shooter, victim, 0);
	EXPECT_EQ(singleHit(world, kill.packet()).route, Route::Match);
	auto killed = rifleHit(other, shooter, 0);
	EXPECT_EQ(singleHit(world, killed.packet()).route, Route::Match);
	EXPECT_EQ(deathsOf(world, victim), 1u);
	EXPECT_EQ(deathsOf(world, shooter), 1u);
	auto respawn = respawnOf(shooter);
	EXPECT_EQ(playerRespawn(world, respawn.packet()).route, Route::Match);
	EXPECT_FALSE(world.players[shooter].dead);
}
