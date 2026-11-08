#include <gtest/gtest.h>
#include <cstring>
#include <array>
#include "Detail/CapsuleRules.h"

using Main::Structures::CapsuleList;
using Main::Structures::CapsuleListDatabase;
using namespace Main::Details;

namespace
{
	constexpr std::uint64_t start = 1'790'000'000;
	constexpr std::uint64_t end = start + 86'400;
	constexpr CapsuleListDatabase sale{ 45, 900, start, end }; // newRtPrice, newMpPrice

	std::array<std::uint32_t, 4> words(const CapsuleList& entry)
	{
		std::array<std::uint32_t, 4> w{};
		std::memcpy(w.data(), &entry, sizeof(entry));
		return w;
	}
}

TEST(CapsuleSale, ListRecordMatchesClientLayout)
{
	static_assert(sizeof(CapsuleList) == 16);
	CapsuleList entry{};
	entry.capsuleInfoId = 0x11223344;
	entry.price = 0x1FFFFF;
	entry.luckyPoints = 0x7FF;
	entry.saleStartDate = 0xAABBCCDD;
	entry.saleEndDate = 0x01020304;
	const auto w = words(entry);
	EXPECT_EQ(w[0], 0x11223344u);
	EXPECT_EQ(w[1], 0xFFFFFFFFu);
	EXPECT_EQ(w[2], 0xAABBCCDDu);
	EXPECT_EQ(w[3], 0x01020304u);

	entry.price = 1234;
	entry.luckyPoints = 7;
	EXPECT_EQ(words(entry)[1] & 0x1FFFFF, 1234u);
	EXPECT_EQ(words(entry)[1] >> 21, 7u);
}

TEST(CapsuleSale, ActiveWindowIsStrictLikeTheClient)
{
	EXPECT_FALSE(capsuleSaleActive(sale, start));
	EXPECT_TRUE(capsuleSaleActive(sale, start + 1));
	EXPECT_TRUE(capsuleSaleActive(sale, end - 1));
	EXPECT_FALSE(capsuleSaleActive(sale, end));
	EXPECT_FALSE(capsuleSaleActive(CapsuleListDatabase{}, 0));
	EXPECT_FALSE(capsuleSaleActive(CapsuleListDatabase{}, start));
}

TEST(CapsuleSale, ListEntryDuringSale)
{
	const auto rt = words(capsuleListEntry(1001, rockTotensCapsuleType, 60, 3, sale, start + 10));
	EXPECT_EQ(rt[0], 1001u);
	EXPECT_EQ(rt[1] & 0x1FFFFF, 45u);
	EXPECT_EQ(rt[1] >> 21, 3u);
	EXPECT_EQ(rt[2], static_cast<std::uint32_t>(start));
	EXPECT_EQ(rt[3], static_cast<std::uint32_t>(end));

	const auto mp = words(capsuleListEntry(2001, microPointsCapsuleType, 1500, 1, sale, start + 10));
	EXPECT_EQ(mp[1] & 0x1FFFFF, 900u);
	EXPECT_EQ(mp[2], static_cast<std::uint32_t>(start));
}

TEST(CapsuleSale, PendingSaleIsSentWithItsDates)
{
	const auto rt = words(capsuleListEntry(1001, rockTotensCapsuleType, 60, 3, sale, start - 100));
	EXPECT_EQ(rt[1] & 0x1FFFFF, 45u);
	EXPECT_EQ(rt[2], static_cast<std::uint32_t>(start));
	EXPECT_EQ(rt[3], static_cast<std::uint32_t>(end));
}

TEST(CapsuleSale, ListEntryOutsideSaleHasNormalPriceAndNoDates)
{
	for (const std::uint64_t now : { end, end + 1000 })
	{
		const auto rt = words(capsuleListEntry(1001, rockTotensCapsuleType, 60, 3, sale, now));
		EXPECT_EQ(rt[1] & 0x1FFFFF, 60u);
		EXPECT_EQ(rt[1] >> 21, 3u);
		EXPECT_EQ(rt[2], 0u);
		EXPECT_EQ(rt[3], 0u);
	}
	const auto coins = words(capsuleListEntry(3001, 0, 500, 2, sale, start + 10));
	EXPECT_EQ(coins[1] & 0x1FFFFF, 500u);
	EXPECT_EQ(coins[2], 0u);

	const CapsuleListDatabase rtOnly{ 45, 0, start, end };
	const auto mp = words(capsuleListEntry(2001, microPointsCapsuleType, 1500, 1, rtOnly, start + 10));
	EXPECT_EQ(mp[1] & 0x1FFFFF, 1500u);
	EXPECT_EQ(mp[3], 0u);

	const CapsuleListDatabase reversed{ 45, 900, end, start };
	EXPECT_EQ(words(capsuleListEntry(1001, rockTotensCapsuleType, 60, 3, reversed, start + 10))[1] & 0x1FFFFF, 60u);
}

TEST(CapsuleSale, DatesBeyond32BitsAreClamped)
{
	EXPECT_EQ(capsuleClientDate(0), 0u);
	EXPECT_EQ(capsuleClientDate(start), static_cast<std::uint32_t>(start));
	EXPECT_EQ(capsuleClientDate(0x1'0000'0005ull), 0xFFFFFFFFu);
}

TEST(CapsuleSale, ChargeDuringSale)
{
	const auto rt = capsuleSpinCharge(rockTotensCapsuleType, 60, 45, sale, start + 10);
	EXPECT_EQ(rt.pricePerSpin, 45u);
	EXPECT_TRUE(rt.salePrice);
	EXPECT_FALSE(rt.refused);

	const auto mp = capsuleSpinCharge(microPointsCapsuleType, 1500, 900 | 0xFFE00000u, sale, start + 10);
	EXPECT_EQ(mp.pricePerSpin, 900u);
	EXPECT_TRUE(mp.salePrice);

	const auto normalShown = capsuleSpinCharge(rockTotensCapsuleType, 60, 60, sale, start + 10);
	EXPECT_EQ(normalShown.pricePerSpin, 60u);
	EXPECT_FALSE(normalShown.salePrice);
	EXPECT_FALSE(normalShown.refused);
}

TEST(CapsuleSale, ChargeOutsideSale)
{
	const auto rt = capsuleSpinCharge(rockTotensCapsuleType, 60, 60, sale, end + 5);
	EXPECT_EQ(rt.pricePerSpin, 60u);
	EXPECT_FALSE(rt.salePrice);
	EXPECT_FALSE(rt.refused);

	const auto coins = capsuleSpinCharge(0, 500, 45, sale, start + 10);
	EXPECT_EQ(coins.pricePerSpin, 500u);
	EXPECT_FALSE(coins.salePrice);
	EXPECT_FALSE(coins.refused);

	const auto lucky = capsuleSpinCharge(luckyCapsuleType, 0, 0, sale, start + 10);
	EXPECT_EQ(lucky.pricePerSpin, 0u);
	EXPECT_FALSE(lucky.refused);
}

TEST(CapsuleSale, StaleSalePriceIsRefused)
{
	for (const std::uint64_t now : { start, end, end + 60 })
	{
		const auto rt = capsuleSpinCharge(rockTotensCapsuleType, 60, 45, sale, now);
		EXPECT_TRUE(rt.refused);
		EXPECT_FALSE(rt.salePrice);
	}
}

TEST(CapsuleSale, ZeroOrSameSalePriceMeansNoSale)
{
	const CapsuleListDatabase none{ 0, 0, start, end };
	EXPECT_EQ(capsuleSalePrice(none, rockTotensCapsuleType, 60), 0u);
	const auto charge = capsuleSpinCharge(rockTotensCapsuleType, 60, 0, none, start + 10);
	EXPECT_EQ(charge.pricePerSpin, 60u);
	EXPECT_FALSE(charge.salePrice);
	EXPECT_FALSE(charge.refused);
	EXPECT_EQ(words(capsuleListEntry(1001, rockTotensCapsuleType, 60, 3, none, start + 10))[1] & 0x1FFFFF, 60u);

	const CapsuleListDatabase same{ 60, 1500, start, end };
	EXPECT_EQ(capsuleSalePrice(same, rockTotensCapsuleType, 60), 0u);
	EXPECT_FALSE(capsuleSpinCharge(rockTotensCapsuleType, 60, 60, same, start + 10).salePrice);

	const CapsuleListDatabase huge{ 0x200000, 0, start, end };
	EXPECT_EQ(capsuleSalePrice(huge, rockTotensCapsuleType, 60), 0u);
}

TEST(CapsuleSale, ChargedAmountMatchesWhatTheClientDeducts)
{
	// client 0xADBEE0: mission 1 deducts the cdb price, mission 2 the price of the order 83 record
	for (const std::uint64_t now : { start - 100, start + 10, end + 10 })
	{
		const auto entry = capsuleListEntry(1001, rockTotensCapsuleType, 60, 3, sale, now);
		const bool clientShowsSale = entry.saleStartDate < now && now < entry.saleEndDate;
		const std::uint32_t shown = clientShowsSale ? static_cast<std::uint32_t>(entry.price) : 60u;
		const auto charge = capsuleSpinCharge(rockTotensCapsuleType, 60, shown, sale, now);
		ASSERT_FALSE(charge.refused);
		EXPECT_EQ(charge.pricePerSpin, charge.salePrice ? static_cast<std::uint32_t>(entry.price) : 60u);
		EXPECT_EQ(charge.pricePerSpin, shown);
		EXPECT_EQ(charge.salePrice, clientShowsSale);
	}
}
