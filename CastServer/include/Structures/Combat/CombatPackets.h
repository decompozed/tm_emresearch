#ifndef CAST_STRUCTURES_COMBAT_PACKETS_H
#define CAST_STRUCTURES_COMBAT_PACKETS_H

#include <bit>
#include <cstddef>
#include <cstdint>
#include "Structures/AccountInfo/MainAccountUniqueId.h"
#include "Utils/Macros.h"

namespace Cast::Structures
{
	using UniqueId = Main::Structures::UniqueId;

PACK_PUSH(1)
	struct Half3
	{
		std::uint16_t x{};
		std::uint16_t y{};
		std::uint16_t z{};
	};
	static_assert(sizeof(Half3) == 6);

	struct HpField
	{
		std::uint32_t hp : 20 = 0;
		std::uint32_t hitPart : 5 = 0;
		std::uint32_t reserved : 7 = 0;
	};
	static_assert(sizeof(HpField) == 4);

	struct DamageEntry
	{
		UniqueId target{};
		HpField hp{};
		std::uint32_t matchTime{};
	};
	static_assert(sizeof(DamageEntry) == 12);

	struct ExplosionDamageHeader
	{
		std::uint32_t sourceId{};
		Half3 position{};
		std::uint16_t weaponId{};
	};
	static_assert(sizeof(ExplosionDamageHeader) == 12);

	struct FallDeathPacket
	{
		ExplosionDamageHeader header{};
		DamageEntry victim{};
	};
	static_assert(sizeof(FallDeathPacket) == 24);

	struct ItemEffectPacket
	{
		UniqueId target{};
		std::uint32_t itemId{};
		UniqueId user{};
	};
	static_assert(sizeof(ItemEffectPacket) == 12);

	struct StateKillPacket
	{
		UniqueId attacker{};
		UniqueId victim{};
		std::uint32_t effectId{};
	};
	static_assert(sizeof(StateKillPacket) == 12);

	struct ShotTrace
	{
		std::uint32_t tag{};
		Half3 start{};
		Half3 direction{};
	};
	static_assert(sizeof(ShotTrace) == 16);

	struct SingleHitPacket
	{
		ShotTrace trace{};
		UniqueId attacker{};
		UniqueId target{};
		HpField hp{};
		std::uint32_t matchTime{};
	};
	static_assert(sizeof(SingleHitPacket) == 32);

	struct MultiHitHeader
	{
		ShotTrace trace{};
		UniqueId attacker{};
	};
	static_assert(sizeof(MultiHitHeader) == 20);

	struct ProjectileLaunchPacket
	{
		ShotTrace trace{};
		UniqueId owner{};
		std::uint32_t ammoId{};
	};
	static_assert(sizeof(ProjectileLaunchPacket) == 24);

	struct ReloadRequest
	{
		std::uint8_t weaponField{};
		std::uint8_t pelletsPerShot{};
		std::uint16_t magazineAmmo{};
	};
	static_assert(sizeof(ReloadRequest) == 4);

	struct ReloadPacket
	{
		ReloadRequest request{};
		UniqueId player{};
	};
	static_assert(sizeof(ReloadPacket) == 8);

	struct ItemDamageRequest
	{
		UniqueId target{};
		std::uint32_t itemId{};
	};
	static_assert(sizeof(ItemDamageRequest) == 8);

	struct HitRequestEntry
	{
		UniqueId target{};
		Half3 hitPoint{};
		std::uint16_t hitPart : 5 = 0;
		std::uint16_t unused : 11 = 0;
	};
	static_assert(sizeof(HitRequestEntry) == 12);

	struct HitRequest
	{
		ShotTrace trace{};
		HitRequestEntry hit{};
	};
	static_assert(sizeof(HitRequest) == 28);
PACK_POP()

	[[nodiscard]] inline FallDeathPacket makeFallDeath(const UniqueId& victim, const Half3& position) noexcept
	{
		FallDeathPacket death{};
		death.header.sourceId = std::bit_cast<std::uint32_t>(victim) & 0x7FFFFFFF;
		death.header.position = position;
		death.victim.target = victim;
		return death;
	}

	inline constexpr std::size_t maxPayloadSize = 0x594;
	inline constexpr std::uint8_t maxShotEntries = 116; // sub_AC9280, sub_ACA0A0, sub_B370E0: option < 0x75
}

#endif
