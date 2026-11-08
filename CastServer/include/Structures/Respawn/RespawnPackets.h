#ifndef CAST_STRUCTURES_RESPAWN_PACKETS_H
#define CAST_STRUCTURES_RESPAWN_PACKETS_H

#include <cstdint>
#include "Structures/Combat/CombatPackets.h"
#include "Utils/Macros.h"

namespace Cast::Structures
{
PACK_PUSH(1)
	struct RespawnRequest
	{
		Half3 position{};
		std::uint16_t yaw{};
	};
	static_assert(sizeof(RespawnRequest) == 8);

	struct RespawnPacket
	{
		Half3 position{};
		std::uint16_t yaw{};
		UniqueId unit{};
	};
	static_assert(sizeof(RespawnPacket) == 12);
PACK_POP()

	inline constexpr std::uint8_t hostPickedSpawn = 0;
	inline constexpr std::uint8_t chosenSpawn = 3;
}

#endif
