#ifndef CAST_STRUCTURES_NPC_PACKETS_H
#define CAST_STRUCTURES_NPC_PACKETS_H

#include <cstddef>
#include <cstdint>
#include "Structures/Combat/CombatPackets.h"
#include "Utils/Macros.h"

namespace Cast::Structures
{
PACK_PUSH(1)
	struct BotMoveState
	{
		std::uint32_t weaponState : 7 = 0;
		std::uint32_t animation : 6 = 0;
		std::uint32_t slot : 4 = 0;
		std::uint32_t yawDegrees : 9 = 0;
		std::uint32_t flag : 1 = 0;
		std::uint32_t hasExtra : 1 = 0;
		std::uint32_t reserved : 4 = 0;
	};
	static_assert(sizeof(BotMoveState) == 4);

	struct BotMovePacket
	{
		Half3 position{};
		Half3 velocity{};
		std::uint32_t timeMs : 31 = 0;
		std::uint32_t hasAim : 1 = 0;
		BotMoveState state{};
		Half3 aim{};
		std::uint16_t aimWeapon : 4 = 0;
		std::uint16_t aimFlag : 1 = 0;
		std::uint16_t aimUnused : 11 = 0;
		std::uint16_t extraValue{};
		std::uint16_t extraElapsedMs{};
		UniqueId bot{};
	};
	static_assert(sizeof(BotMovePacket) == 36);

	struct NpcMoveEntry
	{
		std::uint32_t npcKey{};
		Half3 position{};
		Half3 vectorA{};
		Half3 vectorB{};
		std::uint8_t action{};
		std::uint8_t animation{};
		std::uint16_t yawDegrees{};
		std::uint16_t unused{};
		std::uint8_t actionData[4]{};
	};
	static_assert(sizeof(NpcMoveEntry) == 32);

	struct NpcProjectilePacket
	{
		std::uint32_t unused{};
		Half3 position{};
		Half3 direction{};
		UniqueId shooter{};
		std::uint32_t projectileId{};
	};
	static_assert(sizeof(NpcProjectilePacket) == 24);

	struct MonsterSpawnPacket
	{
		std::uint32_t monsterKey{};
		Half3 position{};
		Half3 direction{};
		std::uint16_t scale{};
		std::uint16_t monsterInfoId{};
	};
	static_assert(sizeof(MonsterSpawnPacket) == 20);

	struct BossScheduleHeader
	{
		std::uint16_t count{};
		std::uint16_t countCopy{};
	};
	static_assert(sizeof(BossScheduleHeader) == 4);
PACK_POP()

	inline constexpr std::size_t maxNpcMoveEntries = 45; // sub_B1F970: "out of MAX_PACKET_NPC_MOVE_LIST"
	inline constexpr std::size_t bossScheduleSize = 1420;
	inline constexpr std::size_t bossScheduleEntrySize = 12;
	inline constexpr std::size_t maxBossScheduleEntries = (bossScheduleSize - sizeof(BossScheduleHeader)) / bossScheduleEntrySize;
}

#endif
