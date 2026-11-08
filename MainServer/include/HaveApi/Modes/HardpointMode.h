/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#ifndef MAIN_HAVE_API_MODES_HARDPOINT_MODE_H
#define MAIN_HAVE_API_MODES_HARDPOINT_MODE_H

#include <random>
#include "HaveApi/HardpointZones.h"
#include "HaveApi/Modes/PartyMode.h"
#include "HaveApi/Modes/TeamBaseSpawns.h"

namespace Main::HaveApi
{
	// Shared with Headquarters.
	// Two balanced random teams: the players in random order take red, blue, red, blue... (fallback team = team).
	void assignBalancedTeams(MatchContext& match, std::mt19937& random);
	// The first zone: the spot farthest from both teams' bases (the map's TDM spawn nodes on floor); `fallback` on maps
	// without bases. Only for the first zone: nobody has spawned yet.
	[[nodiscard]] std::size_t spotFarthestFromBases(std::span<const Common::HaveApi::ZoneSpot> spots, std::uint32_t mapId,
		std::size_t fallback);
	// The next zone (any spot but `current` when there are several): a random one farther than `keepOut` from every
	// spawn point the clients used (MatchContext::spawnPoints); none such: the one farthest from the nearest spawn;
	// no spawn known: a random one.
	[[nodiscard]] std::size_t spotAwayFromSpawns(std::span<const Common::HaveApi::ZoneSpot> spots, std::span<const SpawnPoint> spawns,
		float keepOut, std::size_t current, std::mt19937& random);
	// The smaller of the red and blue teams (red when equal).
	[[nodiscard]] std::int32_t smallerOfTwoTeams(std::span<const PartyPlayer> players);

	// Hardpoint (party game 6, not in the original DLL):
	//  - two teams drawn at random and balanced (red = team 1, blue = team 2); respawn on;
	//  - a zone (vertical ring, only x / y count) at one of the map's spots (Common/include/HaveApi/HardpointZones.h),
	//    moving every "zone time" seconds to another random spot; "ZONE MOVES IN 10 SECONDS" before it moves;
	//  - every second a team alone in the zone scores PointsPerSecond (one player is enough); both teams in the zone:
	//    nobody scores ("TEAMS ARE FIGHTING IN THE ZONE"); the holder is announced when it changes;
	//  - a player in the zone while its team scores gets the point on its own scoreboard row too; an enemy kill is worth
	//    KillPoints (players) - it does not change the team score;
	//  - one round: it ends when a team reaches the points limit, or at the time limit (setting, Off by default); the
	//    team with the most points wins ("TEAM ... HAS WON", Team Clash's events 14 / 15);
	//  - respawns: each team at its official base (TeamBaseSpawns: the map's TDM / SAB team spawns, 0x38); the DLL
	//    respawns at one of them. The zone still moves to a spot farther than respawnKeepOut from every spawn point the
	//    clients used (spotAwayFromSpawns), and a player who respawned within respawnKeepOut of the zone is not an occupant
	//    for PartyZone::spawnGrace (CastServer);
	//  - the first zone is the spot farthest from both bases (map spawn nodes, nobody has spawned yet); the zone reaches
	//    CastServer at the round start; CastServer reports the zone's occupants when they change (no polling);
	//  - no round banner (one round; the DLL shows the score box instead);
	//  - late joiners go to the smaller team.
	class HardpointMode final : public PartyMode
	{
	public:
		static constexpr std::int32_t redTeam = 1;
		static constexpr std::int32_t blueTeam = 2;
		static constexpr float zoneRadius = 1100.0f; // the ring model's radius (make_hardpoint_ring.py)
		static constexpr float respawnKeepOut = 3000.0f;      // zone spots keep this far from the clients' spawns
		static constexpr std::int32_t pointsPerSecond = 1;
		static constexpr std::chrono::seconds moveWarning{ 10 };

		enum class ZoneState : std::int32_t { Free = 0, Red = 1, Blue = 2, Contested = 3 };

		HardpointMode();
		explicit HardpointMode(std::uint32_t seed); // unit tests

		void onMatchStart(MatchContext& match) override;
		void onRoundStart(MatchContext& match) override;
		void onDeath(MatchContext& match, PartyPlayer& victim, PartyPlayer* killer) override;
		void onTick(MatchContext& match) override;
		[[nodiscard]] bool isRoundOver(MatchContext& match) override;
		void onRoundEnd(MatchContext& match, RoundEndReason reason) override;
		void onMatchEnd(MatchContext& match) override;
		[[nodiscard]] bool respawnAllowed() const override { return true; }
		[[nodiscard]] std::optional<std::pair<Common::HaveApi::Announcement, std::string>> roundStartAnnouncement(
			const MatchContext&) const override;
		[[nodiscard]] std::int32_t teamForLateJoiner(MatchContext& match) override;
		[[nodiscard]] std::uint32_t roundsPerMatch(std::uint32_t) const override { return 1; }
		[[nodiscard]] bool showsRounds() const override { return false; }

		[[nodiscard]] std::int32_t redPoints() const { return m_points[0]; }
		[[nodiscard]] std::int32_t bluePoints() const { return m_points[1]; }
		[[nodiscard]] ZoneState zoneState() const { return m_state; }
		[[nodiscard]] std::size_t currentSpot() const { return m_spot; }

		[[nodiscard]] static std::chrono::seconds zoneTime(const MatchContext& match);
		[[nodiscard]] static std::int32_t pointsLimit(const MatchContext& match);

	private:
		void tickZone(MatchContext& match);
		void updateSpawns(MatchContext& match);
		void moveZone(MatchContext& match, bool announceMove);
		void sendZone(MatchContext& match);
		[[nodiscard]] std::int32_t winner() const;

		std::mt19937 m_random;
		TeamBaseSpawns m_teamSpawns;
		std::vector<Common::HaveApi::ZoneSpot> m_spots;
		std::size_t m_spot = 0;
		std::chrono::seconds m_zoneSince{ 0 };
		bool m_warned = false;
		ZoneState m_state = ZoneState::Free;
		std::array<std::int32_t, 2> m_points{};
	};
}

#endif
