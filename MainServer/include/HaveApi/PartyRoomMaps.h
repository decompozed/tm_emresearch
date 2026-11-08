/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#ifndef MAIN_HAVE_API_PARTY_ROOM_MAPS_H
#define MAIN_HAVE_API_PARTY_ROOM_MAPS_H

#include <cstdint>
#include <optional>
#include <random>
#include <string>
#include "Enums/RoomEnums.h"
#include "HaveApi/Catalog.h"
#include "HaveApi/PartyMaps.h"
#include "HaveApi/RoomState.h"

namespace Main::HaveApi
{
	[[nodiscard]] inline std::int32_t partyModeOf(const RoomState& state, std::uint32_t roomMode)
	{
		return roomMode == Common::HaveApi::partyRoomMode ? state.partySettings().modeId : 0;
	}

	[[nodiscard]] inline std::uint32_t allowedRoomMap(std::int32_t partyMode, std::uint32_t roomMode, std::uint32_t current,
		std::uint32_t requested)
	{
		if (roomMode != Common::HaveApi::partyRoomMode || Common::HaveApi::isPartyMapAllowed(partyMode, requested))
		{
			return requested;
		}
		if (Common::HaveApi::isPartyMapAllowed(partyMode, current))
		{
			return current;
		}
		return Common::HaveApi::fallbackPartyMap(partyMode).value_or(Common::Enums::Random);
	}

	[[nodiscard]] inline std::uint32_t allowedRoomMap(const RoomState& state, std::uint32_t roomMode, std::uint32_t current,
		std::uint32_t requested)
	{
		return allowedRoomMap(state.partySettings().modeId, roomMode, current, requested);
	}

	[[nodiscard]] inline std::string refusedMapText(std::int32_t partyMode)
	{
		if (partyMode == static_cast<std::int32_t>(Common::HaveApi::PartyModeId::BattleRoyale))
		{
			return "Battle Royale is played on the Big Map only";
		}
		if (partyMode == static_cast<std::int32_t>(Common::HaveApi::PartyModeId::Domination))
		{
			return "Domination: this map has no zones, choose one of the listed maps";
		}
		return "This map is for Battle Royale only, choose one of the listed maps";
	}

	[[nodiscard]] inline std::uint32_t randomDominationMap()
	{
		static std::mt19937 random{ std::random_device{}() };
		const auto& maps = Common::HaveApi::dominationMaps;
		return maps[std::uniform_int_distribution<std::size_t>{ 0, maps.size() - 1 }(random)].map;
	}

	[[nodiscard]] inline std::optional<std::uint32_t> partyStartMap(const RoomState& state, std::uint32_t roomMode, std::uint32_t map)
	{
		const std::int32_t partyMode = partyModeOf(state, roomMode);
		if (roomMode != Common::HaveApi::partyRoomMode || !state.hasPartyGame() || Common::HaveApi::isPartyMapAllowed(partyMode, map))
		{
			return std::nullopt;
		}
		if (partyMode == static_cast<std::int32_t>(Common::HaveApi::PartyModeId::Domination))
		{
			return randomDominationMap();
		}
		return Common::HaveApi::fallbackPartyMap(partyMode).value_or(Common::Enums::Random);
	}
}

#endif
