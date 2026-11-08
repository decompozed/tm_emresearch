#ifndef CAPSULE_LIST_STRUCT_H
#define CAPSULE_LIST_STRUCT_H

#include <cstdint>
#include "Macros.h"

namespace Main
{
	namespace Structures
	{
PACK_PUSH(1)
		struct CapsuleList
		{
			std::uint32_t capsuleInfoId = 0;
			std::uint32_t price : 21 = 0;
			std::uint32_t luckyPoints : 11 = 0;
			std::uint32_t saleStartDate{};
			std::uint32_t saleEndDate{};
		};
PACK_POP()
		static_assert(sizeof(CapsuleList) == 16, "order 83 record: client 0xAD72D0 reads 16 bytes");

PACK_PUSH(1)
		struct CapsuleListDatabase
		{
			std::uint32_t newRtPrice{};
			std::uint32_t newMpPrice{};
			std::uint64_t saleEventStartDate{};
			std::uint64_t saleEventEndDate{};
		};
PACK_POP()

PACK_PUSH(1)
		struct EventMissionInfo
		{
			std::uint64_t startDate{};
			std::uint64_t endDate{};
		};
PACK_POP()

PACK_PUSH(1)
		struct ExpMpBonusInfo
		{
			std::uint64_t startDate{};
			std::uint64_t endDate{};
			std::uint32_t expBonusPercent{};
			std::uint32_t mpBonusPercent{};
		};
PACK_POP()
	}
}

#endif