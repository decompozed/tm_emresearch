/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#ifndef COMMON_HAVE_API_CAST_CONNECT_H
#define COMMON_HAVE_API_CAST_CONNECT_H

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include "Utils/Macros.h"

namespace Common::HaveApi
{
	inline constexpr std::uint32_t playerKeyMask = 0x7FFFFFFF;

	enum class CastConnectVerdict : std::uint8_t
	{
		Accepted,
		Malformed,
		UnknownAccount,
		WrongHashKey,
		WrongPlayerKey,
	};

PACK_PUSH(1)
	struct CastConnectCheck
	{
		std::uint32_t accountId{};
		std::uint32_t hashKey{};
		std::uint32_t playerKey{};
	};

	struct CastConnectAnswer
	{
		std::uint16_t session{};
		CastConnectVerdict verdict{ CastConnectVerdict::Malformed };
		std::uint8_t reserved{};
	};
PACK_POP()

	static_assert(sizeof(CastConnectCheck) == 12);
	static_assert(sizeof(CastConnectAnswer) == 4);

	template<class T>
	[[nodiscard]] std::optional<T> decodeExact(const std::uint8_t* data, std::size_t size) noexcept
	{
		if (data == nullptr || size != sizeof(T))
		{
			return std::nullopt;
		}
		T value{};
		std::memcpy(&value, data, sizeof(T));
		return value;
	}

	[[nodiscard]] constexpr CastConnectVerdict verifyCastConnect(const CastConnectCheck& check, std::uint32_t accountKey,
		std::uint32_t uniqueId) noexcept
	{
		if (check.hashKey == 0 || check.hashKey != accountKey)
		{
			return CastConnectVerdict::WrongHashKey;
		}
		if ((check.playerKey & playerKeyMask) != (uniqueId & playerKeyMask))
		{
			return CastConnectVerdict::WrongPlayerKey;
		}
		return CastConnectVerdict::Accepted;
	}
}

#endif
