#ifndef CAPSULE_RULES_H
#define CAPSULE_RULES_H

#include <cstdint>
#include <algorithm>
#include <limits>
#include "Structures/Capsule/CapsuleList.h"

namespace Main
{
	namespace Details
	{
		// gachaponinfo gi_type 3: the client's lucky capsule (CGachaponInfoList +36, 0x8CA0E0), spun once by the lucky button (0x621000)
		inline constexpr std::uint32_t luckyCapsuleType = 3;
		inline constexpr std::uint32_t rockTotensCapsuleType = 1;
		inline constexpr std::uint32_t microPointsCapsuleType = 2;
		inline constexpr std::uint32_t capsulePriceMask = 0x1FFFFF;
		inline constexpr std::uint32_t capsuleLuckyPointsMask = 0x7FF;

		inline bool capsuleSpinAllowed(std::uint32_t capsuleType, std::uint32_t spins, std::uint32_t luckyPoints, std::uint32_t maxLuckyPoints)
		{
			if (capsuleType < luckyCapsuleType) return true;
			return capsuleType == luckyCapsuleType && spins == 1 && luckyPoints >= maxLuckyPoints;
		}

		// client 0x6CFFE0: the sale price applies only while start < now < end (client _time64, UTC seconds)
		constexpr bool capsuleSaleActive(const Main::Structures::CapsuleListDatabase& sale, std::uint64_t now) noexcept
		{
			return sale.saleEventStartDate < now && now < sale.saleEventEndDate;
		}

		constexpr std::uint32_t capsuleSalePrice(const Main::Structures::CapsuleListDatabase& sale, std::uint32_t capsuleType, std::uint32_t normalPrice) noexcept
		{
			if (normalPrice == 0) return 0;
			const std::uint32_t price = capsuleType == rockTotensCapsuleType ? sale.newRtPrice
				: capsuleType == microPointsCapsuleType ? sale.newMpPrice : 0;
			return price <= capsulePriceMask && price != normalPrice ? price : 0;
		}

		constexpr std::uint32_t capsuleClientDate(std::uint64_t seconds) noexcept
		{
			return static_cast<std::uint32_t>(std::min<std::uint64_t>(seconds, std::numeric_limits<std::uint32_t>::max()));
		}

		constexpr Main::Structures::CapsuleList capsuleListEntry(std::uint32_t capsuleId, std::uint32_t capsuleType, std::uint32_t normalPrice,
			std::uint32_t luckyPoints, const Main::Structures::CapsuleListDatabase& sale, std::uint64_t now) noexcept
		{
			Main::Structures::CapsuleList entry{};
			entry.capsuleInfoId = capsuleId;
			entry.price = normalPrice & capsulePriceMask;
			entry.luckyPoints = std::min(luckyPoints, capsuleLuckyPointsMask);

			const std::uint32_t salePrice = capsuleSalePrice(sale, capsuleType, normalPrice);
			if (salePrice != 0 && sale.saleEventStartDate < sale.saleEventEndDate && now < sale.saleEventEndDate)
			{ // a pending sale too: the client switches price on its own clock
				entry.price = salePrice;
				entry.saleStartDate = capsuleClientDate(sale.saleEventStartDate);
				entry.saleEndDate = capsuleClientDate(sale.saleEventEndDate);
			}
			return entry;
		}

		struct CapsuleSpinCharge
		{
			std::uint32_t pricePerSpin{};
			bool salePrice = false;
			bool refused = false;
		};

		// clientPrice: order 92 request, the per-spin price the client shows (0xB11410)
		constexpr CapsuleSpinCharge capsuleSpinCharge(std::uint32_t capsuleType, std::uint32_t normalPrice, std::uint32_t clientPrice,
			const Main::Structures::CapsuleListDatabase& sale, std::uint64_t now) noexcept
		{
			const std::uint32_t salePrice = capsuleSalePrice(sale, capsuleType, normalPrice);
			if (salePrice != 0 && (clientPrice & capsulePriceMask) == salePrice)
			{
				if (capsuleSaleActive(sale, now)) return { salePrice, true, false };
				return { normalPrice, false, true };
			}
			return { normalPrice, false, false };
		}
	}
}

#endif
