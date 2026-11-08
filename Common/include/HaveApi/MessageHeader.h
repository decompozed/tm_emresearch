/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#ifndef COMMON_HAVE_API_MESSAGE_HEADER_H
#define COMMON_HAVE_API_MESSAGE_HEADER_H

#include <array>
#include <cstddef>
#include <cstdint>
#include "HaveApi/MessageId.h"
#include "Utils/Macros.h"

namespace Common::HaveApi
{
PACK_PUSH(1)
	// 40-byte header of every have_api message. Only the id (bits 15..27 of the qword at +0x10) is used;
	// the other bytes are zero.
	struct MessageHeader
	{
		std::array<std::uint8_t, 0x10> prefix{};
		std::uint64_t idBits = 0;
		std::array<std::uint8_t, 0x10> suffix{};

		[[nodiscard]] constexpr std::uint16_t id() const noexcept
		{
			return static_cast<std::uint16_t>((idBits >> idShift) & idMask);
		}

		constexpr void setId(MessageId messageId) noexcept
		{
			idBits = static_cast<std::uint64_t>(static_cast<std::uint16_t>(messageId) & idMask) << idShift;
		}

		static constexpr unsigned idShift = 15;
		static constexpr std::uint64_t idMask = 0x1FFF;
	};
PACK_POP()

	static_assert(sizeof(MessageHeader) == 0x28);
	static_assert(offsetof(MessageHeader, idBits) == 0x10);

	// Payload offsets in the DLL documentation are measured from the start of the message (header included).
	inline constexpr std::size_t payloadOffset = sizeof(MessageHeader);
}

#endif
