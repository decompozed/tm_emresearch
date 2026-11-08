#ifndef CAST_STRUCTURES_CONNECTION_PACKETS_H
#define CAST_STRUCTURES_CONNECTION_PACKETS_H

#include <cstdint>
#include "Structures/Combat/CombatPackets.h"
#include "Utils/Macros.h"

namespace Cast::Structures
{
PACK_PUSH(1)
	struct ConnectRequest
	{
		UniqueId playerKey{};
		std::uint32_t accountId{};
		std::uint32_t hashKey{};
	};
	static_assert(sizeof(ConnectRequest) == 12);
PACK_POP()

	struct CommandFields
	{
		std::uint16_t order{};
		std::uint8_t mission{};
		std::uint8_t extra{};
		std::uint8_t option{};
	};

	inline constexpr CommandFields castConnected{ 501, 0, 32, 0 };
	inline constexpr CommandFields pong{ 72, 1, 0, 0 };
	inline constexpr CommandFields castJoinDenied{ 278, 0, 35, 0 };
}

#endif
