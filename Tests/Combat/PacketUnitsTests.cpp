#include <gtest/gtest.h>
#include "Combat/PacketUnits.h"
#include "FakeWorld.h"

using namespace CombatTests;

TEST(PacketUnits, SingleHitNamesTheShooterNotTheSender)
{
	SingleHitPacket shot{};
	shot.trace = trace();
	shot.attacker = uid(5);
	shot.target = uid(6);
	Frame frame{ 268, 1, 0 };
	frame.add(shot);
	const auto units = packetUnits(frame.from(hostSession).packet());
	ASSERT_TRUE(units.attacker);
	EXPECT_EQ(units.attacker->session, 5u);
	ASSERT_TRUE(units.target);
	EXPECT_EQ(units.target->session, 6u);
}

TEST(PacketUnits, MultiHitNamesTheShooterAndTheFirstTarget)
{
	MultiHitHeader header{};
	header.trace = trace();
	header.attacker = uid(3);
	Frame frame{ 269, 1, 2 };
	frame.add(header).add(entry(8, 100)).add(entry(9, 100));
	const auto units = packetUnits(frame.packet());
	ASSERT_TRUE(units.attacker);
	EXPECT_EQ(units.attacker->session, 3u);
	ASSERT_TRUE(units.target);
	EXPECT_EQ(units.target->session, 8u);
}

TEST(PacketUnits, ProjectileExplosionsNameTheAmmo)
{
	ExplosionDamageHeader header{};
	header.sourceId = 1234;
	header.position = half3();
	Frame projectile{ 264, 1, 1 };
	projectile.add(header).add(entry(4, 0));
	const auto byAmmo = packetUnits(projectile.packet());
	EXPECT_FALSE(byAmmo.attacker);
	EXPECT_EQ(byAmmo.ammoId, 1234u);
	ASSERT_TRUE(byAmmo.target);
	EXPECT_EQ(byAmmo.target->session, 4u);

	header.sourceId = raw(uid(2));
	Frame area{ 264, 2, 1 };
	area.add(header);
	const auto byUnit = packetUnits(area.packet());
	ASSERT_TRUE(byUnit.attacker);
	EXPECT_EQ(byUnit.attacker->session, 2u);
	EXPECT_FALSE(byUnit.target);
}

TEST(PacketUnits, ShortOrUnknownPacketsNameNobody)
{
	Frame truncated{ 267, 1, 0 };
	truncated.add(std::uint32_t{ 7 });
	EXPECT_FALSE(packetUnits(truncated.packet()).attacker);
	Frame other{ 146, 0, 0 };
	other.add(std::uint64_t{ 1 });
	const auto units = packetUnits(other.packet());
	EXPECT_FALSE(units.attacker);
	EXPECT_FALSE(units.ammoId);
	EXPECT_FALSE(units.target);
}
