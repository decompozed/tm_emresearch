/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#ifndef COMMON_HAVE_API_CATALOG_H
#define COMMON_HAVE_API_CATALOG_H

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include "HaveApi/Messages.h"

// What the DLL lets a room master choose, copied from the DLL (party/settings/PartyGameCatalog.cpp: party games,
// settings, allowed values, defaults; ui/roomScreen/RoomOptionCombos.cpp and party/state/PartyRules.cpp: room
// options). The server accepts nothing outside these tables.
namespace Common::HaveApi
{
	enum class PartyModeId : std::int32_t
	{
		None = 0,
		ChickenRun = 1,
		Chaser = 2,
		Arena = 3,
		TeamClash = 4,
		LavaEscape = 5,
		Hardpoint = 6, // not in the original DLL (added to the refactored DLL and the client files)
		Headquarters = 7, // not in the original DLL either
		PropHunt = 8,     // not in the original DLL either
		EliminationShuffle = 9, // not in the original DLL either
		Domination = 10, // not in the original DLL either
		BattleRoyale = 11,
	};

	// The room mode in which the DLL shows the party games button (SquareMode).
	inline constexpr std::uint32_t partyRoomMode = 12;

	struct SettingRule
	{
		std::span<const std::int32_t> allowedValues;
		std::int32_t defaultValue;
	};

	struct PartyModeRules
	{
		PartyModeId id;
		std::span<const SettingRule> settings; // settings[i] is values[i] of message 0x0A; unused slots are 0
	};

	namespace Detail
	{
		inline constexpr std::array onOff = { 1, 2 };                                // 1 = On, 2 = Off
		inline constexpr std::array chickenRoundTime = { 10, 15, 20, 25, 30 };        // tenths of a minute
		inline constexpr std::array chaserRoundTime = { 15, 20, 25, 30 };
		inline constexpr std::array teamClashRoundTime = { 20, 30, 40 };
		inline constexpr std::array teamCount = { 3, 4, 5 };
		inline constexpr std::array lavaSpeed = { 5, 8, 10, 15 };                     // Slow, Normal, Fast, Extreme
		inline constexpr std::array lavaGracePeriod = { 1, 2, 3 };                    // Off, Normal, Long

		inline constexpr std::array chickenSettings = {
			SettingRule{ chickenRoundTime, 20 }, SettingRule{ onOff, 2 } /* explosives */, SettingRule{ onOff, 1 } /* bonus */ };
		inline constexpr std::array chaserSettings = { SettingRule{ chaserRoundTime, 20 } };
		inline constexpr std::array arenaSettings = {
			SettingRule{ chaserRoundTime, 20 }, SettingRule{ onOff, 2 } /* heal winner */, SettingRule{ onOff, 1 } /* radar phase */ };
		inline constexpr std::array teamClashSettings = {
			SettingRule{ teamClashRoundTime, 30 }, SettingRule{ onOff, 2 } /* keep teams */, SettingRule{ onOff, 1 } /* radar phase */,
			SettingRule{ teamCount, 3 } };
		inline constexpr std::array lavaSettings = {
			SettingRule{ lavaSpeed, 8 }, SettingRule{ lavaGracePeriod, 2 }, SettingRule{ onOff, 1 } /* walls */,
			SettingRule{ onOff, 1 } /* hot phase */, SettingRule{ onOff, 1 } /* sleep phase */ };

		inline constexpr std::array hardpointZoneTime = { 30, 45, 60 };             // seconds
		inline constexpr std::array hardpointPoints = { 100, 200, 300 };
		inline constexpr std::array hardpointTimeLimit = { 1, 5, 10, 15 };          // 1 = Off, else minutes
		inline constexpr std::array hardpointSettings = {
			SettingRule{ hardpointZoneTime, 45 }, SettingRule{ hardpointPoints, 200 }, SettingRule{ hardpointTimeLimit, 1 } };

		inline constexpr std::array hqCaptureTime = { 5, 10, 15 };                  // seconds
		inline constexpr std::array hqLifetime = { 45, 60, 90 };                    // seconds
		inline constexpr std::array hqPoints = { 100, 200, 250 };
		inline constexpr std::array hqSettings = {
			SettingRule{ hqCaptureTime, 10 }, SettingRule{ hqLifetime, 60 }, SettingRule{ hqPoints, 200 },
			SettingRule{ hardpointTimeLimit, 1 } };

		inline constexpr std::array propHideTime = { 20, 30 };                      // seconds
		inline constexpr std::array propRoundTime = { 3, 4, 5 };                    // minutes
		inline constexpr std::array propRounds = { 3, 5 };
		inline constexpr std::array propChanges = { 2, 3, 5, 8, 10, 15, 20, 30 };    // object changes per round
		inline constexpr std::array propDecoys = { 2, 3, 4, 5, 6, 8, 10 };            // decoys per round
		inline constexpr std::array propHuntSettings = {
			SettingRule{ propHideTime, 30 }, SettingRule{ propRoundTime, 3 }, SettingRule{ propRounds, 3 },
			SettingRule{ propChanges, 5 }, SettingRule{ propDecoys, 3 } };

		// Elimination Shuffle: the official Elimination's round times (2 / 4 minutes, in tenths of a minute as the other
		// round times) and rounds (roomoptionInfo.cdb 206001..206002, 406001..406004)
		inline constexpr std::array shuffleRoundTime = { 20, 40 };
		inline constexpr std::array shuffleRounds = { 3, 5, 7, 11 };
		inline constexpr std::array shuffleSettings = { SettingRule{ shuffleRoundTime, 20 }, SettingRule{ shuffleRounds, 5 } };

		// Domination: points limit 100..350 (default 350), time limit as Headquarters
		inline constexpr std::array dominationPoints = { 100, 150, 200, 250, 300, 350 };
		inline constexpr std::array dominationSettings = { SettingRule{ dominationPoints, 350 }, SettingRule{ hardpointTimeLimit, 1 } };

		inline constexpr std::array battleRoyaleMatchTime = { 8, 10, 12 };
		inline constexpr std::array battleRoyaleFinalArena = { 1, 2, 3, 4, 5 }; // Random, Castle, Paper, PVC, Studio
		inline constexpr std::array battleRoyaleStormDamage = { 1, 2 };        // Timer, Gradual
		inline constexpr std::array battleRoyaleSettings = { SettingRule{ battleRoyaleMatchTime, 10 },
			SettingRule{ battleRoyaleFinalArena, 1 }, SettingRule{ battleRoyaleStormDamage, 1 } };

		inline constexpr std::array partyModes = {
			PartyModeRules{ PartyModeId::ChickenRun, chickenSettings },
			PartyModeRules{ PartyModeId::Chaser, chaserSettings },
			PartyModeRules{ PartyModeId::Arena, arenaSettings },
			PartyModeRules{ PartyModeId::TeamClash, teamClashSettings },
			PartyModeRules{ PartyModeId::LavaEscape, lavaSettings },
			PartyModeRules{ PartyModeId::Hardpoint, hardpointSettings },
			PartyModeRules{ PartyModeId::Headquarters, hqSettings },
			PartyModeRules{ PartyModeId::PropHunt, propHuntSettings },
			PartyModeRules{ PartyModeId::EliminationShuffle, shuffleSettings },
			PartyModeRules{ PartyModeId::Domination, dominationSettings },
			PartyModeRules{ PartyModeId::BattleRoyale, battleRoyaleSettings },
		};

		inline constexpr std::array healthOptions = { 2000, 1500, 1000 };
		inline constexpr std::array weaponBalanceOptions = { 1, 2 }; // 1 = Balanced, 2 = Legacy
		inline constexpr std::array checkBoxOptions = { 0, 1 };
		// Room modes in which the DLL applies the room options (party battles excluded).
		inline constexpr std::array roomOptionModes = { 6u, 0u, 5u, 3u, 1u, 2u, 10u, 11u, 12u, 4u, 8u, 18u, 17u, 19u, 16u };

		[[nodiscard]] constexpr bool contains(std::span<const std::int32_t> values, std::int32_t value)
		{
			return std::ranges::find(values, value) != values.end();
		}
	}

	[[nodiscard]] constexpr const PartyModeRules* findPartyMode(std::int32_t modeId)
	{
		const auto found = std::ranges::find(Detail::partyModes, static_cast<PartyModeId>(modeId), &PartyModeRules::id);
		return found != Detail::partyModes.end() ? &*found : nullptr;
	}

	[[nodiscard]] constexpr bool isValidPartySettings(const PartySettingsPayload& settings)
	{
		if (settings.modeId == static_cast<std::int32_t>(PartyModeId::None))
		{
			return std::ranges::all_of(settings.values, [](std::int32_t value) { return value == 0; });
		}
		const PartyModeRules* rules = findPartyMode(settings.modeId);
		if (rules == nullptr)
		{
			return false;
		}
		for (std::size_t i = 0; i < settings.values.size(); ++i)
		{
			const bool valid = i < rules->settings.size() ? Detail::contains(rules->settings[i].allowedValues, settings.values[i])
			                                              : settings.values[i] == 0;
			if (!valid)
			{
				return false;
			}
		}
		return true;
	}

	[[nodiscard]] constexpr SettingValues defaultSettings(PartyModeId modeId)
	{
		SettingValues values{};
		if (const PartyModeRules* rules = findPartyMode(static_cast<std::int32_t>(modeId)))
		{
			for (std::size_t i = 0; i < rules->settings.size(); ++i)
			{
				values[i] = rules->settings[i].defaultValue;
			}
		}
		return values;
	}

	[[nodiscard]] constexpr bool isPartyModeOffered(std::int32_t modeId, bool battleRoyaleEnabled)
	{
		return modeId != static_cast<std::int32_t>(PartyModeId::BattleRoyale) || battleRoyaleEnabled;
	}

	[[nodiscard]] constexpr std::uint32_t minimumDllProtocol(std::int32_t modeId)
	{
		return modeId == static_cast<std::int32_t>(PartyModeId::BattleRoyale) ? battleRoyaleDllProtocol : 0u;
	}

	[[nodiscard]] constexpr bool isValidRoomOption(RoomOptionKind kind, std::int32_t value)
	{
		switch (kind)
		{
		case RoomOptionKind::MaxHealth:
			return Detail::contains(Detail::healthOptions, value);
		case RoomOptionKind::WeaponBalance:
			return Detail::contains(Detail::weaponBalanceOptions, value);
		case RoomOptionKind::OptionCheckBox:
			return Detail::contains(Detail::checkBoxOptions, value);
		}
		return false;
	}

	[[nodiscard]] constexpr bool roomOptionsApplyIn(std::uint32_t roomMode)
	{
		return std::ranges::find(Detail::roomOptionModes, roomMode) != Detail::roomOptionModes.end();
	}
}

#endif
