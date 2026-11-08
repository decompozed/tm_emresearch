#ifndef TRADE_RULES_H
#define TRADE_RULES_H

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace Main
{
	namespace Details
	{
		inline constexpr std::uint32_t tradeFeePerItem = 5000;

		inline std::optional<std::uint32_t> microPointsAfterTradeFee(std::uint32_t microPoints, std::size_t offeredItems)
		{
			const std::uint64_t fee = std::uint64_t{ tradeFeePerItem } * offeredItems;
			if (fee > microPoints) return std::nullopt;
			return static_cast<std::uint32_t>(microPoints - fee);
		}

		template<typename Offer, typename TradeableItemId>
		bool offerStillHeld(const Offer& offer, TradeableItemId&& tradeableItemId)
		{
			return std::ranges::all_of(offer, [&](const auto& item)
				{
					const std::optional<std::uint32_t> heldId = tradeableItemId(static_cast<std::uint32_t>(item.itemSerialInfo.itemNumber));
					return heldId && *heldId == static_cast<std::uint32_t>(item.itemId.itemId);
				});
		}
	}
}

#endif
