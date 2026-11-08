/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#ifndef COMMON_HAVE_API_BATTLE_MESSAGES_H
#define COMMON_HAVE_API_BATTLE_MESSAGES_H

#include <algorithm>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>
#include "HaveApi/MessageWriter.h"
#include "HaveApi/Messages.h"

// Messages of the party battle dialog (E_DLG_BATTLE_PARTYGAME). The DLL applies them to that dialog without
// checking that it exists: they may only be sent to a player who is in a party battle.
namespace Common::HaveApi
{
	// Gameplay modifiers of one player (or the round default), as the DLL hooks use them.
	struct Modifiers
	{
		ModifiersHead head{};
		ModifiersTail tail{};
		std::int32_t markerRange{};  // 0 = no overhead marker
		float incomingDamage{};
	};

	// Modifiers that leave the game's behaviour unchanged (all-zero blocks freeze the players: the damage and
	// speed multipliers become 0).
	[[nodiscard]] constexpr Modifiers neutralModifiers()
	{
		Modifiers modifiers{};
		modifiers.head.canDamageOthers = 1;
		modifiers.head.outgoingDamage = 1.0f;
		modifiers.head.attribute87Scale.fill(1.0f);
		modifiers.head.attribute86Scale.fill(1.0f);
		modifiers.tail.movementSpeedOverride = 0;
		modifiers.tail.movementSpeedScale = 1.0f;
		modifiers.tail.maxHealthOverride = 0;
		modifiers.tail.maxHealthScale = 1.0f;
		modifiers.tail.selfDamageAllowed = 1;
		modifiers.markerRange = 0;
		modifiers.incomingDamage = 1.0f;
		return modifiers;
	}

	// One-shot actions carried by 0x10 (on every player) and 0x14 (on one player).
	struct PlayerActions
	{
		bool refreshWeaponSlots = false;
		bool restoreFullHealth = false;
		std::int32_t health = 0; // 0 = unchanged
		bool revive = false;
	};

	// The DLL maps a player id to its character with the id of the game's player data: the unique id without
	// its top bit.
	[[nodiscard]] constexpr std::int32_t playerIdFromUniqueId(std::uint32_t uniqueIdBits)
	{
		return static_cast<std::int32_t>(uniqueIdBits & 0x7FFFFFFFu);
	}

	[[nodiscard]] inline std::vector<std::uint8_t> encodePlayer(MessageId id, std::int32_t playerId, std::int32_t team, std::int32_t fallbackTeam)
	{
		return encode(id, PlayerPayload{ playerId, team, fallbackTeam });
	}

	[[nodiscard]] inline std::vector<std::uint8_t> encodeRoster(bool respawnAllowed, std::int32_t roundSeconds, std::int32_t elapsedMilliseconds,
		std::span<const RosterEntry> players)
	{
		const RosterPrefix prefix{ static_cast<std::uint8_t>(respawnAllowed ? 1 : 0), roundSeconds, elapsedMilliseconds,
			static_cast<std::int32_t>(players.size()) };
		return MessageWriter{ MessageId::Roster }.append(prefix).appendRange(players).finish();
	}

	struct RoundState
	{
		std::string_view banner;           // "Round N" while a round runs (the lava floor and the arena rules key on it)
		std::uint64_t lengthSeconds = 0;
		std::uint64_t elapsedMilliseconds = 0;
		bool countdownSounds = false;      // 5..1 s countdown before the round end
		Modifiers defaultModifiers = neutralModifiers();
		PlayerActions actions{};
	};

	[[nodiscard]] inline std::vector<std::uint8_t> encodeRoundState(const RoundState& state)
	{
		RoundStatePayload payload{};
		const std::size_t length = (std::min)(state.banner.size(), payload.roundBanner.size() - 1);
		std::copy_n(state.banner.begin(), length, payload.roundBanner.begin());
		payload.roundLengthSeconds = state.lengthSeconds;
		payload.elapsedMilliseconds = state.elapsedMilliseconds;
		payload.countdownSounds = state.countdownSounds ? 1 : 0;
		payload.modifiersHead = state.defaultModifiers.head;
		payload.refreshWeaponSlots = state.actions.refreshWeaponSlots ? 1 : 0;
		payload.modifiersTail = state.defaultModifiers.tail;
		payload.restoreFullHealth = state.actions.restoreFullHealth ? 1 : 0;
		payload.markerRange = state.defaultModifiers.markerRange;
		payload.incomingDamage = state.defaultModifiers.incomingDamage;
		payload.health = state.actions.health;
		payload.revive = state.actions.revive ? 1 : 0;
		return encode(MessageId::RoundState, payload);
	}

	[[nodiscard]] inline std::vector<std::uint8_t> encodePlayerModifiers(std::int32_t playerId, const Modifiers& modifiers, const PlayerActions& actions,
		bool refreshWeaponSlotsIfLocal = false)
	{
		PlayerModifiersPayload payload{};
		payload.playerId = playerId;
		payload.modifiersHead = modifiers.head;
		payload.refreshWeaponSlotsIfLocal = refreshWeaponSlotsIfLocal ? 1 : 0;
		payload.modifiersTail = modifiers.tail;
		payload.restoreFullHealth = actions.restoreFullHealth ? 1 : 0;
		payload.markerRange = modifiers.markerRange;
		payload.incomingDamage = modifiers.incomingDamage;
		payload.health = actions.health;
		payload.revive = actions.revive ? 1 : 0;
		return encode(MessageId::PlayerModifiers, payload);
	}

	[[nodiscard]] inline std::vector<std::uint8_t> encodePoints(std::int32_t playerId, std::int32_t points, std::int32_t notification = 0)
	{
		return encode(MessageId::PlayerPoints, PlayerPointsPayload{ playerId, points, notification });
	}

	// 0x16: event 1..27 of the DLL's announcement table; the text is shown by the events that have a text label.
	[[nodiscard]] inline std::vector<std::uint8_t> encodeAnnouncement(std::int32_t event, std::int32_t durationSeconds, std::string_view text = {})
	{
		return MessageWriter{ MessageId::Announcement }.append(AnnouncementPrefix{ event, durationSeconds }).appendText(text).finish();
	}

	// 0x19 (Chaser): number of target markers and the state of each (the DLL shows icon 513 for 1, 514 for 2, 515 for
	// a marker without state).
	[[nodiscard]] inline std::vector<std::uint8_t> encodeChaserTargets(std::span<const std::int8_t> states)
	{
		ChaserTargetsPayload payload;
		const std::size_t count = (std::min)(states.size(), payload.targetStates.size());
		payload.targetCount = static_cast<std::int32_t>(count);
		std::copy_n(states.begin(), count, payload.targetStates.begin());
		return encode(MessageId::ChaserTargets, payload);
	}

	// Hardpoint: 0x27 zone, 0x28 team points.
	[[nodiscard]] inline std::vector<std::uint8_t> encodeHardpointZone(bool visible, float x, float y, std::int32_t state)
	{
		return encode(MessageId::HardpointZone, HardpointZonePayload{ visible ? 1 : 0, x, y, state });
	}

	[[nodiscard]] inline std::vector<std::uint8_t> encodeHardpointScore(std::int32_t red, std::int32_t blue, std::int32_t limit)
	{
		return encode(MessageId::HardpointScore, HardpointScorePayload{ red, blue, limit });
	}

	[[nodiscard]] inline std::vector<std::uint8_t> encodeHqProgress(const HqProgressPayload& progress)
	{
		return encode(MessageId::HqProgress, progress);
	}

	// Domination: 0x37 zones, 0x38 the receiver's spawns.
	[[nodiscard]] inline std::vector<std::uint8_t> encodeDominationZones(const DominationZonesPayload& zones)
	{
		return encode(MessageId::DominationZones, zones);
	}

	[[nodiscard]] inline std::vector<std::uint8_t> encodeTeamBases(const TeamBasesPayload& bases)
	{
		return encode(MessageId::TeamBases, bases);
	}

	// Prop Hunt: 0x2A object, 0x2B events, 0x2C phase.
	[[nodiscard]] inline std::vector<std::uint8_t> encodePropState(std::int32_t playerId, std::int32_t model, bool locked,
		const PropWall& wall = {})
	{
		return encode(MessageId::PropState, PropStatePayload{ playerId, model, locked ? 1 : 0, wall });
	}

	[[nodiscard]] inline std::vector<std::uint8_t> encodePropEvent(const PropEventPayload& event)
	{
		return encode(MessageId::PropEvent, event);
	}

	[[nodiscard]] inline std::vector<std::uint8_t> encodePropPhase(const PropPhasePayload& phase)
	{
		return encode(MessageId::PropPhase, phase);
	}

	// Lava Escape: 0x23 floor, 0x24 walls, 0x25 phase icons (hot / sleep).
	[[nodiscard]] inline std::vector<std::uint8_t> encodeLavaFloor(bool visible, float height, float speed)
	{
		return encode(MessageId::LavaFloor, LavaFloorPayload{ static_cast<std::uint8_t>(visible ? 1 : 0), height, speed });
	}

	[[nodiscard]] inline std::vector<std::uint8_t> encodeLavaWalls(const LavaWallsPayload& walls)
	{
		return encode(MessageId::LavaWalls, walls);
	}

	[[nodiscard]] inline std::vector<std::uint8_t> encodeLavaPhase(bool hot, bool sleeping)
	{
		return encode(MessageId::LavaPhase, LavaPhasePayload{ static_cast<std::uint8_t>(hot ? 1 : 0), static_cast<std::uint8_t>(sleeping ? 1 : 0) });
	}

	// 0x12 spectate, 0x15 kill notice (duration), 0x17 marked players, 0x1A chaser id, 0x1C kills, 0x1D deaths.
	[[nodiscard]] inline std::vector<std::uint8_t> encodeValues(MessageId id, std::int32_t value, std::int32_t secondValue = 0)
	{
		return encode(id, IntPayload{ value, secondValue });
	}
}

#endif
