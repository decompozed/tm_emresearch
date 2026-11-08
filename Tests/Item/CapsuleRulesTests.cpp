#include <gtest/gtest.h>
#include "Detail/CapsuleRules.h"

using Main::Details::capsuleSpinAllowed;

namespace
{
	constexpr std::uint32_t maxLucky = 1000;
}

TEST(CapsuleSpin, CurrencyCapsulesAreAlwaysAllowed)
{
	for (std::uint32_t type = 0; type < 3; ++type)
	{
		EXPECT_TRUE(capsuleSpinAllowed(type, 1, 0, maxLucky));
		EXPECT_TRUE(capsuleSpinAllowed(type, 20, 999, maxLucky));
	}
}

TEST(CapsuleSpin, LuckyCapsuleOnlyAsOneSpinWithFullLuckyPoints)
{
	EXPECT_TRUE(capsuleSpinAllowed(3, 1, 1000, maxLucky));
	EXPECT_TRUE(capsuleSpinAllowed(3, 1, 2047, maxLucky));
	EXPECT_FALSE(capsuleSpinAllowed(3, 1, 999, maxLucky));
	EXPECT_FALSE(capsuleSpinAllowed(3, 1, 0, maxLucky));
	EXPECT_FALSE(capsuleSpinAllowed(3, 2, 1000, maxLucky));
	EXPECT_FALSE(capsuleSpinAllowed(3, 20, 1000, maxLucky));
}

TEST(CapsuleSpin, UnknownTypesAreRefused)
{
	EXPECT_FALSE(capsuleSpinAllowed(4, 1, 1000, maxLucky));
	EXPECT_FALSE(capsuleSpinAllowed(0xFFFFFFFF, 1, 1000, maxLucky));
}
