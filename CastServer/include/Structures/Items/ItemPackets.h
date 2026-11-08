#ifndef CAST_STRUCTURES_ITEM_PACKETS_H
#define CAST_STRUCTURES_ITEM_PACKETS_H

#include <cstddef>
#include <cstdint>
#include "Structures/Combat/CombatPackets.h"
#include "Utils/Macros.h"

namespace Cast::Structures
{
PACK_PUSH(1)
	struct ItemPickupRequest
	{
		std::uint32_t itemObjectId{};
		std::uint32_t capsuleItemId{};
	};
	static_assert(sizeof(ItemPickupRequest) == 8);

	struct ItemPickupPacket
	{
		std::uint32_t itemObjectId{};
		std::uint32_t itemId{};
		UniqueId player{};
	};
	static_assert(sizeof(ItemPickupPacket) == 12);

	struct ItemUseRequest
	{
		std::uint32_t inventoryKey{};
	};
	static_assert(sizeof(ItemUseRequest) == 4);

	struct ItemUsePacket
	{
		std::uint32_t inventoryKey{};
		UniqueId player{};
	};
	static_assert(sizeof(ItemUsePacket) == 8);

	struct MapItemKind
	{
		std::uint32_t itemId : 23 = 0;
		std::uint32_t quantity : 5 = 0;
		std::uint32_t hidden : 1 = 0;
		std::uint32_t category : 3 = 0;
	};
	static_assert(sizeof(MapItemKind) == 4);

	struct MapItemEntry
	{
		std::uint32_t itemObjectId{};
		UniqueId owner{};
		MapItemKind kind{};
		float x{};
		float y{};
		float z{};
		std::uint32_t expireTick{};
	};
	static_assert(sizeof(MapItemEntry) == 28);

	struct ItemGrantField
	{
		std::uint32_t itemId : 23 = 0;
		std::uint32_t granted : 1 = 0;
		std::uint32_t reserved : 8 = 0;
	};
	static_assert(sizeof(ItemGrantField) == 4);

	struct ItemGrantPacket
	{
		UniqueId player{};
		ItemGrantField item{};
	};
	static_assert(sizeof(ItemGrantPacket) == 8);

	struct InventoryBlockHeader
	{
		UniqueId unit{};
		std::uint32_t count{};
	};
	static_assert(sizeof(InventoryBlockHeader) == 8);

	struct InventoryItem
	{
		std::uint32_t inventoryKey{};
		std::uint32_t itemId : 23 = 0;
		std::uint32_t quantity : 9 = 0;
	};
	static_assert(sizeof(InventoryItem) == 8);

	struct UnitEffectEntry
	{
		UniqueId unit{};
		UniqueId source{};
		std::uint16_t effectId{};
		std::uint8_t remainingTime[3]{};
		std::uint8_t nextTickIn[3]{};
		std::uint32_t sourceItemId : 23 = 0;
		std::uint32_t reserved : 9 = 0;
	};
	static_assert(sizeof(UnitEffectEntry) == 20);
PACK_POP()

	inline constexpr std::uint8_t mapItemSnapshotFirstPart = 37;
	inline constexpr std::uint8_t maxMapItemSnapshotEntries = 51; // sub_E12350
	inline constexpr std::uint8_t maxInventoryBlocks = 16;
	inline constexpr std::uint8_t maxEffectSnapshotEntries = maxPayloadSize / sizeof(UnitEffectEntry);
}

#endif
