/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#ifndef COMMON_HAVE_API_MESSAGES_H
#define COMMON_HAVE_API_MESSAGES_H

#include <array>
#include <cstddef>
#include <cstdint>
#include "HaveApi/MessageHeader.h"
#include "Utils/Macros.h"

// Fixed-size payloads of the have_api messages (the bytes after the 40-byte header), byte-exact with the DLL
// reconstruction (protocol/wire/IncomingMessages.h and OutgoingMessages.h). Variable-size messages (texts,
// roster, map table, announcements) are built by MessageWriter.
namespace Common::HaveApi
{
	using SettingValues = std::array<std::int32_t, 6>;

	enum class RoomOptionKind : std::int32_t
	{
		MaxHealth = 1,
		WeaponBalance = 2,
		OptionCheckBox = 3,
	};

PACK_PUSH(1)
	// ---- server -> DLL ----

	struct ProfilePayload // 0x05
	{
		std::uint8_t forDetailDialog{};  // 0: writes the open E_DLG_INFO dialog; else: patches the detail dialog texts
		std::array<char, 0x40> title{};
		std::array<char, 0x40> subtitle{};
		std::int32_t level{};
		std::int32_t experience{};
		std::int32_t nextLevelExperience{};
		std::int32_t totalExperience{};
	};

	struct RoomOptionsPayload // 0x06 (server -> DLL: room options seed)
	{
		std::int32_t maxHealth{};
		std::int32_t weaponBalance{};
		std::int32_t optionEnabled{};
		std::int32_t optionAvailable{};
	};

	struct InventoryCapacityPayload // 0x09
	{
		std::int32_t capacity{};
		std::int32_t secondaryCapacity{};
	};

	struct PartySettingsPayload // 0x0A (both directions)
	{
		std::int32_t modeId{};
		SettingValues values{};
	};

	struct PlayerPayload // 0x0B add, 0x0C remove, 0x0D update
	{
		std::int32_t playerId{};
		std::int32_t team{};
		std::int32_t fallbackTeam{};
	};

	struct RosterEntry // element of 0x0E
	{
		std::int32_t playerId{};
		std::int32_t team{};
		std::int32_t points{};
		std::int32_t fallbackTeam{};
	};

	struct RosterPrefix // 0x0E, followed by `count` RosterEntry
	{
		std::uint8_t respawnAllowed{};
		std::int32_t roundSeconds{};
		std::int32_t elapsedMilliseconds{};
		std::int32_t count{};
	};

	// Element of 0x0F. The DLL matches `roomNumber` with bits 0..10 of the room-list entry's qword at +0x20, which is
	// the room number of the room list packet (SingleRoom +0x20).
	struct MapPartyMode
	{
		std::uint16_t roomNumber : 11 = 0;
		std::uint16_t partyMode : 5 = 0;
	};

	struct ModifiersHead
	{
		std::uint8_t canDamageOthers{};
		float outgoingDamage{};
		std::array<float, 6> attribute87Scale{};
		std::array<float, 6> attribute86Scale{};
	};

	struct ModifiersTail
	{
		std::int32_t movementSpeedOverride{};
		float movementSpeedScale{};
		std::int32_t maxHealthOverride{};
		float maxHealthScale{};
		std::uint8_t selfDamageAllowed{};
	};

	struct RoundStatePayload // 0x10
	{
		std::array<char, 0x20> roundBanner{};
		std::uint64_t roundLengthSeconds{};
		std::uint64_t elapsedMilliseconds{};
		std::uint8_t countdownSounds{};
		ModifiersHead modifiersHead{};
		std::uint8_t refreshWeaponSlots{};
		ModifiersTail modifiersTail{};
		std::uint8_t restoreFullHealth{};
		std::int32_t markerRange{};
		float incomingDamage{};
		std::int32_t health{}; // 0 = unchanged
		std::uint8_t revive{};
	};

	struct PlayerPointsPayload // 0x13
	{
		std::int32_t playerId{};
		std::int32_t points{};
		std::int32_t notification{}; // 0 = none, 1..12 = notification icon
	};

	struct PlayerModifiersPayload // 0x14
	{
		std::int32_t playerId{};
		ModifiersHead modifiersHead{};
		std::uint8_t refreshWeaponSlotsIfLocal{};
		ModifiersTail modifiersTail{};
		std::uint8_t restoreFullHealth{};
		std::int32_t markerRange{};
		float incomingDamage{};
		std::int32_t health{}; // 0 = unchanged
		std::uint8_t revive{};
	};

	struct AnnouncementPrefix // 0x16, followed by a NUL-terminated text
	{
		std::int32_t event{};
		std::int32_t durationSeconds{};
	};

	struct IntPayload // 0x12 spectate, 0x15 kill notice, 0x17 marked players, 0x1A chaser id, 0x1C kills, 0x1D deaths
	{
		std::int32_t value{};
		std::int32_t secondValue{};
	};

	// 0x36 (Elimination Shuffle): the round being played or loaded, the rounds of the match, the rounds each team won
	// (the DLL's round / score panel and its loading screen between the rounds)
	struct TeamRoundsPayload
	{
		std::int32_t round{};
		std::int32_t rounds{};
		std::int32_t redWins{};
		std::int32_t blueWins{};
	};

	struct ChaserTargetsPayload // 0x19
	{
		std::int32_t targetCount{};
		std::array<std::int8_t, 15> targetStates{}; // 0 entries are skipped by the DLL
	};

	struct BytePayload // 0x1B radar
	{
		std::uint8_t value{};
	};

	struct UdpSessionPayload // 0x1E
	{
		std::uint8_t tickRate{};
		std::uint8_t reroutingEnabled{};
		std::uint16_t port{};
		std::array<char, 0x20> host{};
		std::int32_t headerTag{};
		std::int32_t headerSessionId{};
	};

	struct CustomMessagePayload // 0x20
	{
		std::array<char, 0x200> text{}; // the DLL forces text[0x1FF] = 0
	};

	struct LavaFloorPayload // 0x23
	{
		std::uint8_t visible{};
		float height{};
		float riseSpeed{};
	};

	struct LavaWallsPayload // 0x24
	{
		float wallX1{};
		float wallX1Speed{};
		float wallX2{};
		float wallX2Speed{};
		float wallY1{};
		float wallY1Speed{};
		float wallY2{};
		float wallY2Speed{};
	};

	struct HardpointZonePayload // 0x27
	{
		std::int32_t visible{};
		float x{};
		float y{};
		std::int32_t state{}; // 0 nobody, 1 red team holds it, 2 blue team holds it, 3 contested
	};

	struct HardpointScorePayload // 0x28
	{
		std::int32_t redPoints{};
		std::int32_t bluePoints{};
		std::int32_t pointsLimit{};
	};

	// Headquarters: what the progress bar / status line shows.
	enum class HqProgressKind : std::int32_t
	{
		Hidden = 0,
		Capturing = 1,  // `team` is capturing the neutral HQ: bar
		Destroying = 2, // `team` is destroying the enemy HQ: bar (+ secondsLeft of the HQ)
		Held = 3,       // `team` holds the HQ: secondsLeft before it expires
		NextHq = 4,     // no HQ: the next one comes online in secondsLeft
	};

	struct HqProgressPayload // 0x29 (Headquarters)
	{
		std::int32_t kind{};               // HqProgressKind
		std::int32_t team{};               // 1 red, 2 blue
		std::int32_t permille{};           // bar: 0..1000
		std::int32_t permillePerSecond{};  // bar speed until the next message (0 = paused)
		std::int32_t secondsLeft{};
	};

	// ---- Domination (party game 10) ----
	// One zone: a square of half side halfSize around x / y on the floor z. owner 0 none / 1 red / 2 blue; capturer: the
	// team making progress on it (0 none); permille: its progress 0..1000, moving at permillePerSecond until the next
	// message (0: paused, e.g. contested); contested 1: both teams inside.
	struct DominationZone
	{
		float x{};
		float y{};
		float z{};
		std::int8_t owner{};
		std::int8_t capturer{};
		std::uint8_t contested{};
		std::uint8_t reserved{};
		std::int16_t permille{};
		std::int16_t permillePerSecond{};
	};

	struct DominationZonesPayload // 0x37
	{
		std::int32_t count{}; // 0 = no zones (match over)
		float halfSize{};
		std::array<DominationZone, 3> zones{}; // A, B, C
	};

	// 0x38 (Hardpoint / Headquarters / Domination), to every player: the host picks every player's respawn
	// (HOST_USER 0xACC110), so every client has both bases. baseOf[team - 1]: the team (1 red, 2 blue) whose base the
	// players of that team respawn at (Domination swaps them); points[base - 1]: x / y of that base's spawn nodes
	// (counts[base - 1] of them; 0: the game's own entries of that base's list).
	struct TeamBasesPayload
	{
		std::array<std::int32_t, 2> baseOf{ 1, 2 };
		std::array<std::int32_t, 2> counts{};
		std::array<std::array<std::array<float, 2>, 8>, 2> points{};
	};

	// ---- Prop Hunt (party game 8) ----
	// A prop "on the wall" (key F4: hung like a picture until the player moves away): the object at x / y / z (its bottom
	// centre) turned by yaw (radians about z); it stays there while the player is within the DLL's leave distance of
	// anchorX / anchorY (where the player stood).
	struct PropWall
	{
		std::int32_t attached{};
		float x{};
		float y{};
		float z{};
		float yaw{};
		float anchorX{};
		float anchorY{};
	};

	struct PropStatePayload // 0x2A: the object a prop player is; model -1 = not a prop (its character is shown)
	{
		std::int32_t playerId{};
		std::int32_t model{};   // mapNNN/prop_MM.nif of the room's map
		std::int32_t locked{};  // the object keeps its rotation while the player turns
		PropWall wall{};
	};

	enum class PropEventKind : std::int32_t { Whistle = 1, Decoy = 2, Flash = 3, ClearDecoys = 4, RemoveDecoy = 5 };

	struct PropEventPayload // 0x2B
	{
		std::int32_t kind{};     // PropEventKind
		std::int32_t playerId{}; // whistle: the prop whistling (the sound plays at its position); decoy / flash: the user
		float x{};               // decoy / flash position
		float y{};
		float z{};
		std::int32_t model{};    // decoy: the object
		float radius{};          // flash: hunters closer than this are blinded
	};

	enum class PropPhaseKind : std::int32_t { Off = 0, Hiding = 1, Hunting = 2 };

	struct PropPhasePayload // 0x2C, to one player
	{
		std::int32_t phase{};        // PropPhaseKind
		std::int32_t secondsLeft{};  // of the phase
		std::int32_t map{};          // the room's map (the props' models are in its folder)
		std::int32_t changesLeft{};
		std::int32_t decoysLeft{};
		std::int32_t flashesLeft{};
	};

	// DecoyHit: a hunter's DLL shot a decoy often enough (x / y / z = the decoy's position): it is removed everywhere
	// PenaltyDeath: a hunter's DLL took its hit points to 0 with decoy hits (the hunters' penalty): it dies for everybody
	enum class PropActionKind : std::int32_t { Change = 1, Lock = 2, Decoy = 3, Flash = 4, Wall = 5, DecoyHit = 6, PenaltyDeath = 7 };

	struct PropActionPayload // 0x2D, DLL -> server
	{
		std::int32_t action{};       // PropActionKind
		float x{};                   // the player's position (decoy / flash); Wall: the object's position on the wall
		float y{};
		float z{};
		float yaw{};                 // Wall: the object's turn (radians)
		float anchorX{};             // Wall: where the player stands
		float anchorY{};
	};

	// 0x2E. DLL -> server: lock / unlock request for one of the player's items (reset 0). Server -> DLL: an item's lock
	// state; at login reset 1 first (the DLL forgets every lock; itemNumber 0 = nothing more), then one per locked item.
	struct ItemLockPayload
	{
		std::uint32_t itemNumber{}; // ItemSerialInfo::itemNumber (the game item's serial, low 20 bits)
		std::uint8_t locked{};
		std::uint8_t reset{};
		std::uint16_t reserved{};
	};

	// 0x35. DLL -> server: about one of the player's items (the item window shows it): action 0 = its option, 1 = swap it
	// (Speed <-> HP). Server -> DLL: the item's option now (0 = no swap: no twin, "No option", not a set part; 1 Speed,
	// 2 HP) and whether it is equipped (the swap needs it in the inventory).
	struct ItemOptionPayload
	{
		std::uint32_t itemNumber{}; // ItemSerialInfo::itemNumber
		std::uint8_t action{};      // DLL -> server
		std::uint8_t option{};      // server -> DLL
		std::uint8_t equipped{};    // server -> DLL
		std::uint8_t reserved{};
	};

	// action 0 = state, 1 = use; allowed bit (branch - 1) * 2 + (level 9): that ticket may be used
	struct UpgradeTicketPayload
	{
		std::uint32_t itemNumber{};
		std::uint8_t action{};
		std::uint8_t branch{};
		std::uint8_t level{};
		std::uint8_t equipped{};
		std::uint8_t upgradable{};
		std::uint8_t tickets8{};
		std::uint8_t tickets9{};
		std::uint8_t allowed{};
		std::array<std::array<char, 20>, 3> branchNames{};
	};

	// 0x2F: the server's challenge (HaveApi/DllProof.h) at login and every 2..5 minutes; the DLL answers with the
	// nonce and the proof. No valid answer in time: the server shows the disconnect box 73 / code 5 (msg.cdb
	// ID_QUIT_BUSY, "have_api.dll is missing in your game's Bin folder!", shown without the DLL) and closes the connection.
	struct DllChallengePayload // server -> DLL
	{
		std::uint32_t nonce{};
		std::uint32_t version{};
		std::uint8_t regionCount{};          // at most 4
		std::uint8_t probeCount{};           // at most 6
		std::uint16_t features{};
		std::array<std::uint32_t, 8> regions{}; // rva, size pairs: the DLL's own code, hashed from its memory
		std::array<std::uint32_t, 6> probes{};  // game addresses: their first byte (the DLL's hooks)
	};
	struct DllCheckPayload // DLL -> server
	{
		std::uint32_t nonce{};
		std::uint32_t version{};
		std::array<std::uint8_t, 32> proof{};
	};
	inline constexpr std::uint32_t dllProtocolVersion = 6;
	inline constexpr std::uint32_t battleRoyaleDllProtocol = 6;
	enum ServerFeature : std::uint16_t
	{
		BattleRoyaleFeature = 1 << 0,
	};
	[[nodiscard]] constexpr std::uint16_t serverFeatures(bool battleRoyaleEnabled)
	{
		return battleRoyaleEnabled ? BattleRoyaleFeature : 0;
	}
	inline constexpr std::uint32_t dllCheckSeconds = 10;          // the login check's answer time
	// Then again and again while the player is online (a proof sent once at login is not enough): a new nonce every
	// 2..5 minutes (random), 30 s to answer (the DLL answers at once; a long map load must not disconnect anybody).
	inline constexpr std::uint32_t dllRecheckMinSeconds = 120;
	inline constexpr std::uint32_t dllRecheckMaxSeconds = 300;
	inline constexpr std::uint32_t dllRecheckAnswerSeconds = 30;  // first wait; attempt n waits n x 30 s (backoff)
	inline constexpr std::uint32_t dllRecheckAttempts = 5;         // a periodic check unanswered 5 times: disconnected

	// 0x30: the chat colour of the player named `nickname` (0xRRGGBB with bit 24 set; 0 = the game's colour);
	// reset 1 = forget every colour first (at login, then one message per coloured player).
	struct ChatColorPayload
	{
		std::array<char, 16> nickname{};
		std::uint32_t color{};
		std::uint8_t reset{};
		std::array<std::uint8_t, 3> reserved{};
	};

	// 0x34, DLL -> server: the chat colour chosen in the chat box's palette (0 = the game's, else the palette colour with bit 24)
	struct ChatColorRequestPayload
	{
		std::uint32_t color{};
	};

	// 0x31, DLL -> server: glow colour for the player's weapon `itemNumber` (0 = original, else the palette colour with bit 24)
	struct GlowRequestPayload
	{
		std::uint32_t itemNumber{};
		std::uint32_t color{};
	};
	// 0x32: the glow colour of the weapon `nickname` has in weapon slot `slot` (10..16), and that weapon's item id
	// (the cgd id: the DLL recognises the weapon objects of a battle by it); reset 1 = forget every one first
	struct WeaponGlowPayload
	{
		std::array<char, 16> nickname{};
		std::uint32_t color{};
		std::uint8_t slot{};
		std::uint8_t reset{};
		std::uint16_t reserved{};
		std::uint32_t itemId{};
	};
	// 0x33: the glow colour of one of the player's items; reset 1 = forget every one first
	struct ItemGlowPayload
	{
		std::uint32_t itemNumber{};
		std::uint32_t color{};
		std::uint8_t reset{};
		std::array<std::uint8_t, 3> reserved{};
		std::uint32_t itemId{}; // the item's cgd id: the DLL colours the player's own weapons by it at once (the server
		                        // learns a new equipment only when the player leaves the inventory)
	};

	inline constexpr std::size_t maxBattleZoneBoxes = 16;

	enum BattleZoneSide : std::uint8_t
	{
		SideX1 = 1 << 0,
		SideX2 = 1 << 1,
		SideY1 = 1 << 2,
		SideY2 = 1 << 3,
	};

	struct BattleZoneBox
	{
		float x1{}, x1Speed{}, x2{}, x2Speed{};
		float y1{}, y1Speed{}, y2{}, y2Speed{};
		float moveSeconds{};
		float zFloor{};
		std::uint8_t kind{}; // 0 arena, 1 corridor
		std::uint8_t drawSides{}; // BattleZoneSide
		std::uint16_t reserved{};
	};

	struct BattleZonePayload // 0x3A
	{
		std::int32_t active{};
		std::int32_t phase{};
		std::int32_t boxCount{};
		std::int32_t phaseEndsInMs{};
		std::int32_t damagePerSecond{};
		std::array<char, 32> label{};
		std::array<BattleZoneBox, maxBattleZoneBoxes> boxes{};
	};

	struct StormHit
	{
		std::int32_t playerId{};
		std::int32_t damage{};
	};

	struct StormDamagePayload // 0x3B
	{
		std::int32_t count{};
		std::array<StormHit, 16> hits{};
	};

	inline constexpr std::size_t maxBattleSpawnBoxes = 12;
	inline constexpr std::size_t maxBattleSpawnPlayers = 16;

	struct BattleSpawnBox
	{
		float x1{}, x2{}, y1{}, y2{};
		std::int32_t arena{};
	};

	struct BattleSpawnPlayer
	{
		std::int32_t playerId{};
		std::int32_t arena{};
	};

	struct BattleSpawnsPayload // 0x3D: each player spawns at a spawn of its arena (inside one of the arena's boxes)
	{
		std::int32_t boxCount{};
		std::array<BattleSpawnBox, maxBattleSpawnBoxes> boxes{};
		std::int32_t playerCount{};
		std::array<BattleSpawnPlayer, maxBattleSpawnPlayers> players{};
	};

	struct LavaPhasePayload // 0x25
	{
		std::uint8_t hotPhase{};
		std::uint8_t sleepPhase{};
	};

	// ---- DLL -> server ----

	struct RoomOptionPayload // 0x06: option changed by the room master
	{
		RoomOptionKind kind{};
		std::int32_t value{};
	};

	struct UdpSessionLostPayload // 0x0B
	{
		std::int32_t reason{};
	};

	struct StormLethalPayload // 0x3C
	{
		std::int32_t playerId{};
	};
PACK_POP()

	// Offsets checked against the DLL (absolute, header included).
	static_assert(payloadOffset + offsetof(ProfilePayload, title) == 0x29);
	static_assert(payloadOffset + offsetof(ProfilePayload, subtitle) == 0x69);
	static_assert(payloadOffset + offsetof(ProfilePayload, level) == 0xA9);
	static_assert(payloadOffset + offsetof(ProfilePayload, totalExperience) == 0xB5);
	static_assert(payloadOffset + offsetof(PartySettingsPayload, values) == 0x2C);
	static_assert(payloadOffset + sizeof(PartySettingsPayload) == 0x44);
	static_assert(payloadOffset + sizeof(RosterPrefix) == 0x35);
	static_assert(sizeof(RosterEntry) == 16);
	static_assert(sizeof(MapPartyMode) == 2);
	static_assert(payloadOffset + offsetof(RoundStatePayload, modifiersHead) == 0x59);
	static_assert(payloadOffset + offsetof(RoundStatePayload, refreshWeaponSlots) == 0x8E);
	static_assert(payloadOffset + offsetof(RoundStatePayload, restoreFullHealth) == 0xA0);
	static_assert(payloadOffset + offsetof(RoundStatePayload, revive) == 0xAD);
	static_assert(payloadOffset + offsetof(PlayerModifiersPayload, refreshWeaponSlotsIfLocal) == 0x61);
	static_assert(payloadOffset + offsetof(PlayerModifiersPayload, restoreFullHealth) == 0x73);
	static_assert(payloadOffset + offsetof(PlayerModifiersPayload, revive) == 0x80);
	static_assert(payloadOffset + offsetof(UdpSessionPayload, headerTag) == 0x4C);
	static_assert(payloadOffset + offsetof(LavaFloorPayload, height) == 0x29);
	static_assert(payloadOffset + offsetof(LavaWallsPayload, wallY2Speed) == 0x44);
	static_assert(payloadOffset + sizeof(RoomOptionPayload) == 48);
	static_assert(payloadOffset + sizeof(UdpSessionLostPayload) == 44);
	static_assert(payloadOffset + sizeof(CustomMessagePayload) == 0x228);
	static_assert(payloadOffset + sizeof(ItemLockPayload) == 48);
	static_assert(payloadOffset + sizeof(DllCheckPayload) == 80);
	static_assert(payloadOffset + sizeof(DllChallengePayload) == 108);
	static_assert(payloadOffset + sizeof(ChatColorPayload) == 64);
	static_assert(payloadOffset + sizeof(GlowRequestPayload) == 48);
	static_assert(payloadOffset + sizeof(ChatColorRequestPayload) == 44);
	static_assert(payloadOffset + sizeof(WeaponGlowPayload) == 68);
	static_assert(payloadOffset + sizeof(ItemGlowPayload) == 56);
	static_assert(payloadOffset + sizeof(ItemOptionPayload) == 48);
	static_assert(payloadOffset + sizeof(UpgradeTicketPayload) == 112);
	static_assert(payloadOffset + sizeof(TeamRoundsPayload) == 56);
	static_assert(sizeof(DominationZone) == 20);
	static_assert(payloadOffset + sizeof(DominationZonesPayload) == 40 + 68);
	static_assert(payloadOffset + sizeof(TeamBasesPayload) == 40 + 144);
	static_assert(sizeof(BattleZoneBox) == 44);
	static_assert(payloadOffset + sizeof(BattleZonePayload) == 40 + 52 + 44 * maxBattleZoneBoxes);
	static_assert(payloadOffset + sizeof(BattleZonePayload) <= 1440);
	static_assert(payloadOffset + sizeof(StormDamagePayload) == 40 + 132);
	static_assert(payloadOffset + sizeof(StormLethalPayload) == 44);
	static_assert(payloadOffset + sizeof(BattleSpawnsPayload) == 40 + 376);
}

#endif
