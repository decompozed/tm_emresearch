#include <gtest/gtest.h>
#include "Detail/SellPrice.h"

using Main::Details::clientSellPrice;

TEST(SellPrice, PermanentItemGetsTheSellPoint)
{
	EXPECT_EQ(clientSellPrice(1500, 0, 0, 1'700'000'000), 1500);
	EXPECT_EQ(clientSellPrice(1500, 604800, 3, 1'700'000'000), 1500);
}

TEST(SellPrice, TimedItemIsProratedByWholeRemainingHours)
{
	constexpr std::int64_t now = 1'700'000'000;
	EXPECT_EQ(clientSellPrice(700, 7 * 86400, static_cast<std::int32_t>(now + 7 * 86400), now), 700);
	EXPECT_EQ(clientSellPrice(700, 7 * 86400, static_cast<std::int32_t>(now + 84 * 3600 + 3599), now), 350);
	EXPECT_EQ(clientSellPrice(100, 3 * 3600, static_cast<std::int32_t>(now + 3600), now), 33);
	EXPECT_EQ(clientSellPrice(700, 7 * 86400, static_cast<std::int32_t>(now + 3599), now), 0);
}

TEST(SellPrice, ExpiredTimedItemIsNegativeLikeTheClient)
{
	constexpr std::int64_t now = 1'700'000'000;
	EXPECT_EQ(clientSellPrice(240, 24 * 3600, static_cast<std::int32_t>(now - 2 * 3600), now), -20);
}

TEST(SellPrice, DurationUnderOneHourGivesZero)
{
	constexpr std::int64_t now = 1'700'000'000;
	EXPECT_EQ(clientSellPrice(500, 1800, static_cast<std::int32_t>(now + 7200), now), 0);
}
