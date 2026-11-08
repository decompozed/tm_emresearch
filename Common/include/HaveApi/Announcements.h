/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#ifndef COMMON_HAVE_API_ANNOUNCEMENTS_H
#define COMMON_HAVE_API_ANNOUNCEMENTS_H

#include <cstdint>
#include <string_view>
#include <vector>
#include "HaveApi/BattleMessages.h"

// The announcements of message 0x16 (the original DLL's 27 and those of the party games added since). Names from what
// each code shows in game; the codes marked "text" show the message text (a player name) in front of the fixed words.
namespace Common::HaveApi
{
	enum class Announcement : std::int32_t
	{
		ChickenChosen = 1,          // text: "<name> IS NOW THE CHICKEN!"
		ChickenPlayerEliminated = 2,// text: "<name> WAS ELIMINATED!" (chicken icon)
		ChickenEliminated = 3,      // "THE CHICKEN WAS ELIMINATED"
		ChaserChosen = 4,           // text: "<name> IS THE CHASER"
		ChaserAllEliminated = 5,    // "ALL PLAYERS ELIMINATED"
		ChaserEliminated = 6,       // "THE CHASER WAS ELIMINATED"
		ChaserTimeUp = 7,           // "ROUND TIME UP!" (chaser icon)
		ArenaTimeUp = 8,            // "ROUND TIME UP!" (VS icon)
		ArenaWinner = 9,            // text: "<name> HAS WON"
		TeamClashTimeUp = 10,       // "ROUND TIME UP!" (crossed swords)
		TeamClashStart3 = 11,       // "BATTLE START! X vs Y vs Z"
		TeamClashStart4 = 12,
		TeamClashStart5 = 13,
		TeamWonFirst = 14,          // 14..18: team 1..5 (red, blue, orange, green, purple) WON
		TeamEliminatedFirst = 19,   // 19..23: team 1..5 was ELIMINATED
		LavaWinner = 24,            // text: "<name> HAS WON"
		LavaComing = 25,            // "LAVA IS COMING!"
		LavaHotPhase = 26,          // "HOT PHASE HAS STARTED x2"
		LavaStopped = 27,           // "THE LAVA HAS STOPPED zzz"
		// Hardpoint (party game 6, images added to the client: room_partygames_3.dds)
		HardpointStart = 28,        // "CAPTURE THE ZONE!"
		HardpointZoneMoved = 29,    // "ZONE MOVED!"
		HardpointZoneMoving = 30,   // "ZONE MOVES IN 10 SECONDS"
		HardpointRedHolding = 31,   // "RED TEAM IS HOLDING"
		HardpointBlueHolding = 32,  // "BLUE TEAM IS HOLDING"
		HardpointContested = 33,    // "TEAMS ARE FIGHTING IN THE ZONE"
		// Headquarters (party game 7, images added to the client: room_partygames_3.dds)
		HqOnline = 34,              // "NEW HQ ONLINE! CAPTURE IT!"
		HqCaptured = 35,            // to the owners: "HQ CAPTURED! NO RESPAWNS!"
		HqEnemyCaptured = 36,       // to the attackers: "ENEMY HQ! DESTROY IT"
		HqDestroyed = 37,           // "HQ DESTROYED! NEW HQ IN 5 SECONDS"
		HqExpired = 38,             // "HQ EXPIRED NEW HQ IN 5 SECONDS"
		HqContested = 39,           // "TEAMS ARE FIGHTING FOR THE HQ"
		// Prop Hunt (party game 8, images added to the client: room_partygames_3.dds)
		PropHide = 40,              // to the props: "HIDE! BECOME AN OBJECT"
		PropWait = 41,              // to the hunters: "THE PROPS ARE HIDING... WAIT"
		PropHuntersReleased = 42,   // "THE HUNTERS ARE COMING!"
		PropHuntersWin = 43,        // "HUNTERS WIN! ALL PROPS FOUND"
		PropPropsWin = 44,          // "PROPS WIN! TIME IS UP"
		// (room_partygames_4.dds)
		PropDecoy = 45,             // to the hunter who broke a decoy: "THAT'S A DECOY!"
		PropFlashHit = 46,          // to the prop: "FLASH HIT! A HUNTER IS BLINDED"
		PropFlashMissed = 47,       // to the prop: "FLASH MISSED"
		PropFound = 48,             // to the prop that was hit: "YOU'VE BEEN FOUND! RUN!"
		// 49, 50: the Prop Hunt radar, removed
		// Domination (party game 10)
		DominationStart = 51,           // "CAPTURE THE ZONES!"
		DominationCapturedA = 52,       // 52..54 to the capturers: "ZONE A / B / C CAPTURED"
		DominationEnemyCapturedA = 55,  // 55..57 to the others: "THE ENEMY CAPTURED A / B / C"
		DominationUnderAttackA = 58,    // 58..60 to the owners: "ZONE A / B / C UNDER ATTACK"
		// Battle Royale (party game 11, room_partygames_6.dds); the message text is shown instead when the image is missing
		BrZoneClosing = 61,             // "THE ZONE IS CLOSING!"
		BrCastleClosesSoon = 62,        // "<arena> CLOSES IN 30 SECONDS", 62..65 in the order of BattleRoyaleMap::Arena
		BrPaperClosesSoon = 63,
		BrPvcClosesSoon = 64,
		BrStudioClosesSoon = 65,
		BrPaperPvcCloseSoon = 66,       // "PAPER LAND + PVC FACTORY CLOSE IN 30 SECONDS"
		BrCastleStudioCloseSoon = 67,   // "CASTLE + THE STUDIO CLOSE IN 30 SECONDS"
		BrInStorm = 68,                 // to one player: "YOU ARE IN THE STORM!"
		BrLastToyStanding = 69,         // text: "<name>" over "LAST TOY STANDING!"
	};

	inline constexpr std::int32_t teamClashMaxTeams = 5;

	[[nodiscard]] constexpr Announcement teamWon(std::int32_t team)
	{
		return static_cast<Announcement>(static_cast<std::int32_t>(Announcement::TeamWonFirst) + team - 1);
	}

	[[nodiscard]] constexpr Announcement teamEliminated(std::int32_t team)
	{
		return static_cast<Announcement>(static_cast<std::int32_t>(Announcement::TeamEliminatedFirst) + team - 1);
	}

	[[nodiscard]] constexpr Announcement teamClashStart(std::int32_t teams)
	{
		return static_cast<Announcement>(static_cast<std::int32_t>(Announcement::TeamClashStart3) + teams - 3);
	}

	[[nodiscard]] inline std::vector<std::uint8_t> encodeAnnouncement(Announcement event, std::int32_t durationSeconds, std::string_view text = {})
	{
		return encodeAnnouncement(static_cast<std::int32_t>(event), durationSeconds, text);
	}
}

#endif
