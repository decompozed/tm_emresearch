#ifndef CAST_STRUCTURES_JOIN_STATE_PACKETS_H
#define CAST_STRUCTURES_JOIN_STATE_PACKETS_H

#include <array>
#include <cstdint>
#include "Structures/Combat/CombatPackets.h"
#include "Utils/Macros.h"

namespace Cast::Structures
{
PACK_PUSH(1)
	struct HostClock
	{
		std::uint64_t hostTick{};
		std::uint32_t modStartTick{};
	};
	static_assert(sizeof(HostClock) == 12);

	struct MatchStats
	{
		std::uint8_t meleeKills{};
		std::uint8_t rifleKills{};
		std::uint8_t shotgunKills{};
		std::uint8_t sniperKills{};
		std::uint8_t mgKills{};
		std::uint8_t bazookaKills{};
		std::uint8_t grenadeKills{};
		std::uint8_t killstreak{};
		std::uint8_t totalKills{};
		std::uint8_t totalDeaths{};
		std::uint8_t headshots{};
		std::uint8_t assists{};
		std::uint8_t modeCounter{};
		std::uint8_t roundsWon : 4 = 0;
		std::uint8_t unusedBits : 4 = 0;
		std::array<std::uint8_t, 2> unused{};
	};
	static_assert(sizeof(MatchStats) == 16);

	struct PlayerJoinStats
	{
		MatchStats stats{};
		UniqueId player{};
		std::uint32_t respawnTick{};
	};
	static_assert(sizeof(PlayerJoinStats) == 24);

	struct PlayerJoinState
	{
		UniqueId player{};
		std::uint32_t hp : 20 = 0;
		std::uint32_t weaponSlot : 5 = 0;
		std::uint32_t state : 4 = 0;
		std::uint32_t unused : 3 = 0;
	};
	static_assert(sizeof(PlayerJoinState) == 8);

	struct UnitStateEntry
	{
		std::uint32_t state : 4 = 0;
		std::uint32_t weaponType : 5 = 0;
		std::uint32_t unused : 23 = 0;
		std::uint32_t hpMasked{};
		std::uint32_t hpMaskedNot{};
		std::uint32_t hpKey{};
		std::uint32_t character : 3 = 0;
		std::uint32_t unusedFlags : 29 = 0;
		UniqueId unit{};
		std::uint32_t respawnTick{};
	};
	static_assert(sizeof(UnitStateEntry) == 28);

	struct ModeStateFlags
	{
		std::uint8_t state : 2 = 0;
		std::uint8_t kitDrop : 1 = 0;
		std::uint8_t timeLimit : 5 = 0;
	};
	static_assert(sizeof(ModeStateFlags) == 1);

	struct TeamModeState
	{
		std::uint8_t redScore{};
		std::uint8_t blueScore{};
		std::uint8_t winRule{};
		ModeStateFlags flags{};
		std::uint32_t weaponLimit : 4 = 0;
		std::uint32_t reserved : 28 = 0;
	};
	static_assert(sizeof(TeamModeState) == 8);

	struct SoloModeState
	{
		std::uint32_t timeLimit : 5 = 0;
		std::uint32_t winRule : 5 = 0;
		std::uint32_t kitDrop : 1 = 0;
		std::uint32_t state : 2 = 0;
		std::uint32_t weaponLimit : 4 = 0;
		std::uint32_t reserved : 15 = 0;
	};
	static_assert(sizeof(SoloModeState) == 4);

	struct PointModeState
	{
		std::uint64_t redPool : 20 = 0;
		std::uint64_t bluePool : 20 = 0;
		std::uint64_t winRule : 5 = 0;
		std::uint64_t state : 2 = 0;
		std::uint64_t timeLimit : 5 = 0;
		std::uint64_t weaponLimit : 4 = 0;
		std::uint64_t reserved : 8 = 0;
	};
	static_assert(sizeof(PointModeState) == 8);

	struct BombModeState
	{
		std::uint32_t plantTime{};
		UniqueId planter{};
		Half3 bombPosition{};
		Half3 bombDirection{};
		TeamModeState team{};
	};
	static_assert(sizeof(BombModeState) == 28);

	struct PveModeState
	{
		std::array<std::uint8_t, 20> reserved{};
		std::uint32_t winRule : 8 = 0;
		std::uint32_t state : 2 = 0;
		std::uint32_t kitDrop : 1 = 0;
		std::uint32_t timeLimit : 5 = 0;
		std::uint32_t weaponLimit : 4 = 0;
		std::uint32_t reservedBits : 12 = 0;
	};
	static_assert(sizeof(PveModeState) == 24);

	struct ModeSyncEntry
	{
		MatchStats stats{};
		UniqueId player{};
	};
	static_assert(sizeof(ModeSyncEntry) == 20);

	struct TeamModeSyncHeader
	{
		std::uint8_t redScore{};
		std::uint8_t blueScore{};
		std::uint8_t count{};
		std::uint8_t winner{};
	};
	static_assert(sizeof(TeamModeSyncHeader) == 4);

	struct ScrimmageModeSyncHeader
	{
		std::uint64_t redPool : 20 = 0;
		std::uint64_t bluePool : 20 = 0;
		std::uint64_t winner : 8 = 0;
		std::uint64_t count : 8 = 0;
		std::uint64_t unused : 8 = 0;
	};
	static_assert(sizeof(ScrimmageModeSyncHeader) == 8);
PACK_POP()

	enum class ModeStateLayout : std::uint8_t
	{
		Team,
		Solo,
		Point,
		Bomb,
		Pve,
	};

	inline constexpr std::array<ModeStateLayout, 20> modeStateLayouts{
		ModeStateLayout::Team, ModeStateLayout::Solo, ModeStateLayout::Team, ModeStateLayout::Team, ModeStateLayout::Team,
		ModeStateLayout::Team, ModeStateLayout::Team, ModeStateLayout::Team, ModeStateLayout::Solo, ModeStateLayout::Point,
		ModeStateLayout::Bomb, ModeStateLayout::Team, ModeStateLayout::Solo, ModeStateLayout::Pve, ModeStateLayout::Team,
		ModeStateLayout::Solo, ModeStateLayout::Team, ModeStateLayout::Team, ModeStateLayout::Team, ModeStateLayout::Bomb,
	};

	enum class ModeSyncLayout : std::uint8_t
	{
		Team,
		TeamUntilEmptyEntry,
		Solo,
		Scrimmage,
		Unread,
	};

	inline constexpr std::array<ModeSyncLayout, 20> modeSyncLayouts{
		ModeSyncLayout::Team, ModeSyncLayout::Solo, ModeSyncLayout::Team, ModeSyncLayout::Team, ModeSyncLayout::Team,
		ModeSyncLayout::TeamUntilEmptyEntry, ModeSyncLayout::Team, ModeSyncLayout::TeamUntilEmptyEntry, ModeSyncLayout::Solo, ModeSyncLayout::Scrimmage,
		ModeSyncLayout::TeamUntilEmptyEntry, ModeSyncLayout::Team, ModeSyncLayout::Solo, ModeSyncLayout::Solo, ModeSyncLayout::Team,
		ModeSyncLayout::Unread, ModeSyncLayout::Team, ModeSyncLayout::TeamUntilEmptyEntry, ModeSyncLayout::Team, ModeSyncLayout::TeamUntilEmptyEntry,
	};

	inline constexpr std::uint16_t modeSyncOrder = 304;
	inline constexpr std::uint16_t joinStatsOrder = 306;
	inline constexpr std::uint8_t serverJoinStateMission = 2;
	inline constexpr std::uint8_t joinStateExtra = 37;
	inline constexpr std::uint8_t noModExtra = 2;
	inline constexpr std::uint8_t modeSyncRequestExtra = 28;
	inline constexpr std::size_t maxJoinPlayers = 16;
	inline constexpr std::size_t maxUnitStates = 51;
	inline constexpr std::uint32_t fullHp = 1000000; // sub_AD6A10
}

#endif
