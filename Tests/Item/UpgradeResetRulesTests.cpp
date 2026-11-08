#include <gtest/gtest.h>
#include "Detail/UpgradeResetRules.h"

#include <map>

using Main::Details::hasUpgradeResetEffect;

namespace
{
	const std::map<std::uint32_t, std::uint32_t> effectKeys{ { 15010, 149 }, { 15020, 150 }, { 15048, 156 } };

	std::optional<std::uint32_t> keyOf(std::uint32_t effectId)
	{
		const auto it = effectKeys.find(effectId);
		return it == effectKeys.end() ? std::nullopt : std::optional{ it->second };
	}
}

TEST(UpgradeReset, ItemWithTheResetEffectIsAccepted)
{
	EXPECT_TRUE(hasUpgradeResetEffect({ 15048, 0, 0 }, keyOf));
	EXPECT_TRUE(hasUpgradeResetEffect({ 0, 0, 15048 }, keyOf));
}

TEST(UpgradeReset, OtherItemsAreRefused)
{
	EXPECT_FALSE(hasUpgradeResetEffect({ 15010, 0, 0 }, keyOf));
	EXPECT_FALSE(hasUpgradeResetEffect({ 15020, 15010, 0 }, keyOf));
	EXPECT_FALSE(hasUpgradeResetEffect({ 0, 0, 0 }, keyOf));
	EXPECT_FALSE(hasUpgradeResetEffect({ 99999, -1, 0 }, keyOf));
}
