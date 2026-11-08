/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#ifndef COMMON_HAVE_API_PARTY_MAPS_H
#define COMMON_HAVE_API_PARTY_MAPS_H

#include <cstdint>
#include <optional>
#include "Enums/RoomEnums.h"
#include "HaveApi/Catalog.h"
#include "HaveApi/DominationZones.h"

namespace Common::HaveApi
{
	inline constexpr std::uint32_t battleRoyaleMap = 58;

	[[nodiscard]] inline bool isPartyMapAllowed(std::int32_t partyMode, std::uint32_t map)
	{
		if (map == Common::Enums::Random)
		{
			return true;
		}
		if (partyMode == static_cast<std::int32_t>(PartyModeId::BattleRoyale))
		{
			return map == battleRoyaleMap;
		}
		if (map == battleRoyaleMap)
		{
			return false;
		}
		return partyMode != static_cast<std::int32_t>(PartyModeId::Domination) || findDominationMap(map) != nullptr;
	}

	[[nodiscard]] inline std::optional<std::uint32_t> fallbackPartyMap(std::int32_t partyMode)
	{
		if (partyMode == static_cast<std::int32_t>(PartyModeId::BattleRoyale))
		{
			return battleRoyaleMap;
		}
		if (partyMode == static_cast<std::int32_t>(PartyModeId::Domination))
		{
			return dominationMaps.front().map;
		}
		return std::nullopt;
	}
}

#endif
