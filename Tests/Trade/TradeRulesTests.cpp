#include <gtest/gtest.h>
#include "Detail/TradeRules.h"

#include <map>
#include <optional>
#include <vector>

using Main::Details::microPointsAfterTradeFee;
using Main::Details::offerStillHeld;

namespace
{
	struct OfferedItem
	{
		struct { std::uint64_t itemNumber; } itemSerialInfo;
		struct { std::uint32_t itemId; } itemId;
	};

	auto heldBy(const std::map<std::uint32_t, std::uint32_t>& inventory)
	{
		return [&inventory](std::uint32_t itemNumber) -> std::optional<std::uint32_t>
			{
				const auto it = inventory.find(itemNumber);
				return it == inventory.end() ? std::nullopt : std::optional{ it->second };
			};
	}
}

TEST(TradeFee, IsTakenWhenTheMicroPointsCoverIt)
{
	EXPECT_EQ(microPointsAfterTradeFee(5000, 1), 0u);
	EXPECT_EQ(microPointsAfterTradeFee(12345, 2), 2345u);
	EXPECT_EQ(microPointsAfterTradeFee(700, 0), 700u);
}

TEST(TradeFee, RefusesInsteadOfWrappingBelowZero)
{
	EXPECT_EQ(microPointsAfterTradeFee(4999, 1), std::nullopt);
	EXPECT_EQ(microPointsAfterTradeFee(0, 10), std::nullopt);
	EXPECT_EQ(microPointsAfterTradeFee(49999, 10), std::nullopt);
}

TEST(TradeOffer, HeldWhenEveryItemIsStillThereWithTheSameId)
{
	const std::map<std::uint32_t, std::uint32_t> inventory{ { 7, 1001 }, { 9, 2002 }, { 12, 3003 } };
	const std::vector<OfferedItem> offer{ { { 7 }, { 1001 } }, { { 12 }, { 3003 } } };
	EXPECT_TRUE(offerStillHeld(offer, heldBy(inventory)));
	EXPECT_TRUE(offerStillHeld(std::vector<OfferedItem>{}, heldBy(inventory)));
}

TEST(TradeOffer, NotHeldWhenAnItemWasSoldOrDeleted)
{
	const std::map<std::uint32_t, std::uint32_t> inventory{ { 7, 1001 } };
	const std::vector<OfferedItem> offer{ { { 7 }, { 1001 } }, { { 12 }, { 3003 } } };
	EXPECT_FALSE(offerStillHeld(offer, heldBy(inventory)));
}

TEST(TradeOffer, NotHeldWhenAnItemChangedId)
{
	const std::map<std::uint32_t, std::uint32_t> inventory{ { 7, 1001 }, { 12, 3004 } };
	const std::vector<OfferedItem> offer{ { { 7 }, { 1001 } }, { { 12 }, { 3003 } } };
	EXPECT_FALSE(offerStillHeld(offer, heldBy(inventory)));
}
