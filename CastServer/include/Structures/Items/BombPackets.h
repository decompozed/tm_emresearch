#ifndef CAST_STRUCTURES_BOMB_PACKETS_H
#define CAST_STRUCTURES_BOMB_PACKETS_H

#include <cstdint>
#include "Structures/Combat/CombatPackets.h"
#include "Utils/Macros.h"

namespace Cast::Structures
{
PACK_PUSH(1)
	struct BombActionRequest
	{
		std::uint32_t reserved{};
		Half3 position{};
		Half3 direction{};
	};
	static_assert(sizeof(BombActionRequest) == 16);

	struct BombActionPacket
	{
		BombActionRequest request{};
		UniqueId player{};
		std::uint8_t padding[12]{};
	};
	static_assert(sizeof(BombActionPacket) == 32);
PACK_POP()

	inline constexpr std::uint8_t bombActionCancel = 33; // sub_A9A350, sub_A9AA50, sub_A9FB40
	inline constexpr std::uint8_t bombActionStart = 38;
	inline constexpr std::uint8_t bombActionComplete = 41;
	inline constexpr std::uint8_t bombDefuse = 0;
	inline constexpr std::uint8_t bombPlant = 1;
}

#endif
