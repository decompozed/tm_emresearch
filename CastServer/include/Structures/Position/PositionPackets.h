#ifndef CAST_STRUCTURES_POSITION_PACKETS_H
#define CAST_STRUCTURES_POSITION_PACKETS_H

#include <cstddef>
#include <cstdint>
#include "Structures/Combat/CombatPackets.h"
#include "Utils/Macros.h"

namespace Cast::Structures
{
PACK_PUSH(1)
	struct PositionTime
	{
		std::uint32_t matchTick : 31 = 0;
		std::uint32_t hasShot : 1 = 0;
	};
	static_assert(sizeof(PositionTime) == 4);

	struct PlayerMotion
	{
		std::uint32_t actionState : 7 = 0;
		std::uint32_t moveState : 6 = 0;
		std::uint32_t weaponSlot : 4 = 0;
		std::uint32_t yawDegrees : 9 = 0;
		std::uint32_t jumpToggle : 1 = 0;
		std::uint32_t hasJump : 1 = 0;
		std::uint32_t reserved : 4 = 0;
	};
	static_assert(sizeof(PlayerMotion) == 4);

	struct PositionReport
	{
		Half3 position{};
		Half3 velocity{};
		PositionTime time{};
		PlayerMotion motion{};
	};
	static_assert(sizeof(PositionReport) == 20);

	struct ShotInfo
	{
		std::uint16_t weaponSlot : 4 = 0;
		std::uint16_t sniperFlag : 1 = 0;
		std::uint16_t reserved : 11 = 0;
	};
	static_assert(sizeof(ShotInfo) == 2);

	struct PositionShot
	{
		Half3 target{};
		ShotInfo info{};
	};
	static_assert(sizeof(PositionShot) == 8);

	struct PositionJump
	{
		std::uint16_t startZ{};
		std::uint16_t elapsedCentiseconds{};
	};
	static_assert(sizeof(PositionJump) == 4);

	struct PositionEntryFlags
	{
		std::uint32_t sessionId : 14 = 0;
		std::uint32_t hasMovement : 1 = 0;
		std::uint32_t hasShot : 1 = 0;
		std::uint32_t actionState : 7 = 0;
		std::uint32_t hasRotation : 1 = 0;
		std::uint32_t moveState : 6 = 0;
		std::uint32_t jumpToggle : 1 = 0;
		std::uint32_t hasJump : 1 = 0;
	};
	static_assert(sizeof(PositionEntryFlags) == 4);

	struct PositionMovement
	{
		Half3 position{};
		Half3 velocity{};
	};
	static_assert(sizeof(PositionMovement) == 12);

	struct PositionRotation
	{
		std::int8_t pitchDegrees{};
		std::int8_t tiltDegrees{};
		std::uint16_t yawDegrees : 9 = 0;
		std::uint16_t weaponSlot : 4 = 0;
		std::uint16_t reserved : 3 = 0;
	};
	static_assert(sizeof(PositionRotation) == 4);

	struct PositionBatchHeader
	{
		std::uint32_t roomTickCentiseconds{};
	};
	static_assert(sizeof(PositionBatchHeader) == 4);
PACK_POP()

	struct PlayerPosition
	{
		PositionReport report{};
		std::int8_t pitchDegrees{};
		std::int8_t tiltDegrees{};
		PositionShot shot{};
		PositionJump jump{};
	};

	inline constexpr std::uint8_t positionShotFirstSlot = 1;
	inline constexpr std::uint8_t positionShotLastSlot = 4;
	inline constexpr std::size_t minPositionEntrySize = sizeof(PositionEntryFlags) + sizeof(PositionMovement) + sizeof(PositionRotation);
	inline constexpr std::size_t maxPositionEntrySize = minPositionEntrySize + sizeof(PositionShot) + sizeof(PositionJump);
	static_assert(maxPositionEntrySize == 32);
	static_assert((maxPayloadSize - sizeof(PositionBatchHeader)) / minPositionEntrySize <= 0xFF);
}

#endif
