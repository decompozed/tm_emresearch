/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#ifndef COMMON_HAVE_API_MESSAGE_ID_H
#define COMMON_HAVE_API_MESSAGE_ID_H

#include <cstdint>

// Message ids of the have_api.dll protocol (client extension DLL, packet order 81).
// Server -> DLL unless noted. Source: the DLL reconstruction, protocol/wire/MessageId.h.
namespace Common::HaveApi
{
	enum class MessageId : std::uint16_t
	{
		StatusText = 0x02,
		ResetSlotItems = 0x03,
		PlaySound = 0x04,
		Profile = 0x05,
		RoomOptions = 0x06,       // both directions
		Popup = 0x07,
		Ignored08 = 0x08,         // accepted by the DLL, no effect: never sent
		InventoryCapacity = 0x09,
		PartySettings = 0x0A,     // both directions
		AddPlayer = 0x0B,         // DLL -> server: UDP session lost
		RemovePlayer = 0x0C,
		UpdatePlayer = 0x0D,
		Roster = 0x0E,
		MapModeTable = 0x0F,
		RoundState = 0x10,
		RespawnOnce = 0x11,
		Spectate = 0x12,
		PlayerPoints = 0x13,
		PlayerModifiers = 0x14,
		KillNotice = 0x15,
		Announcement = 0x16,
		MarkedPlayers = 0x17,
		RequestRespawn = 0x18,
		ChaserTargets = 0x19,
		ChaserId = 0x1A,
		Radar = 0x1B,
		Kills = 0x1C,
		Deaths = 0x1D,
		UdpSession = 0x1E,
		UdpSessionActive = 0x1F,
		CustomMessage = 0x20,
		UdpReady = 0x21,
		CreatePartyMode = 0x22,
		LavaFloor = 0x23,
		LavaWalls = 0x24,
		LavaPhase = 0x25,
		PlayMusic = 0x26,
		HardpointZone = 0x27,  // party game 6 (not in the original DLL): zone position and state
		HardpointScore = 0x28, // party game 6: team points and the points limit (Headquarters too)
		HqProgress = 0x29,     // party game 7 (Headquarters): capture / destroy progress bar and HQ timer
		PropState = 0x2A,      // party game 8 (Prop Hunt): a player's object (model, rotation lock)
		PropEvent = 0x2B,      // Prop Hunt: whistle, decoy, flash, decoys cleared
		PropPhase = 0x2C,      // Prop Hunt: the round's phase and the receiver's ability uses
		PropAction = 0x2D,     // Prop Hunt, DLL -> server: an ability key (change, lock, decoy, flash)
		ItemLock = 0x2E, // both directions (not in the original DLL): an inventory item's lock
		DllCheck = 0x2F, // both directions (not in the original DLL): login check that have_api.dll runs
		ChatColor = 0x30, // server -> DLL (not in the original DLL): a player's custom chat colour
		GlowRequest = 0x31, // DLL -> server (not in the original DLL): glow colour chosen for a level 8-9 weapon
		WeaponGlow = 0x32, // server -> DLL: a player's weapon slot glow colour
		ItemGlow = 0x33, // server -> DLL: the glow colour of one of the player's items
		ChatColorRequest = 0x34, // DLL -> server: chat colour chosen in the chat box's palette
		TeamRounds = 0x36, // server -> DLL (not in the original DLL): round, rounds, rounds won by red / blue
		ItemOption = 0x35, // both directions (not in the original DLL): a set part's Speed / HP option swap
		DominationZones = 0x37, // server -> DLL (party game 10): the three zones (place, owner, capture progress)
		TeamBases = 0x38, // server -> DLL: both teams' base spawns and the base each team respawns at
		UpgradeTicket = 0x39, // not in the original DLL
		BattleZone = 0x3A,  // server -> DLL (party game 11): the storm's boxes
		StormDamage = 0x3B, // server -> DLL: storm damage of this second
		StormLethal = 0x3C, // DLL -> server, from the host: the storm damage would kill this player
		BattleSpawns = 0x3D, // server -> DLL: the arena each player spawns in
	};

	inline constexpr MessageId udpSessionLost = MessageId::AddPlayer; // same id, DLL -> server direction
	inline constexpr std::uint16_t firstMessageId = 0x02;
	inline constexpr std::uint16_t lastMessageId = 0x26;

	// Order-81 command fields that mark have_api traffic (the other order-81 packets are the HWID report).
	inline constexpr std::uint16_t order = 81;
	inline constexpr std::uint8_t serverToClientExtra = 0x24;
	inline constexpr std::uint8_t clientToServerExtra = 0x34;
	inline constexpr std::uint8_t option = 5;
}

#endif
