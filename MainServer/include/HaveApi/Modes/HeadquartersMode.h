/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#ifndef MAIN_HAVE_API_MODES_HEADQUARTERS_MODE_H
#define MAIN_HAVE_API_MODES_HEADQUARTERS_MODE_H

#include <random>
#include "HaveApi/HardpointZones.h"
#include "HaveApi/Modes/HardpointMode.h"
#include "HaveApi/Modes/PartyMode.h"

namespace Main::HaveApi
{
	// Headquarters (party game 7, after Call of Duty's mode; not in the original DLL).
	// Built on Hardpoint: same teams, zone spots, ring, score box, respawns (each team at its official base, TeamBaseSpawns).
	//  - one HQ (the zone) at a time, at one of the map's spots; the first one is the spot farthest from both bases, the
	//    next ones are away from the spawn points the clients used (spotAwayFromSpawns); a player who respawned near the
	//    HQ does not count inside it for PartyZone::spawnGrace (no capture / destroy work by spawning in it);
	//  - capture: a team alone in the neutral HQ for "capture time" seconds captures it; the work goes faster with more
	//    players inside (1 player x1, 2 players x1.5, 3+ players x2); both teams inside: paused (contested); nobody of the
	//    capturing team inside: the progress is lost; the progress bar is shown to everybody (0x29, like Bomb Battle);
	//  - captured: the owners score 1 point a second, + captureBonus to the team and + LastStandingPoints to each player
	//    who captured; the owners CANNOT RESPAWN while they hold it (their dead watch); the attackers respawn normally;
	//  - destroy: the attackers alone in the HQ for "capture time" seconds (same speed-up) destroy it: + captureBonus to
	//    their team, + LastStandingPoints to each destroyer; defenders and attackers inside: paused;
	//  - the HQ expires "HQ lifetime" seconds after its capture; destroyed or expired: the owners' dead respawn at once,
	//    the next HQ comes online elsewhere nextHqDelay seconds later; a neutral HQ waits until somebody captures it;
	//  - enemy kills: KillPoints (players); one round; points limit or time limit (most points wins), as Hardpoint.
	class HeadquartersMode final : public PartyMode
	{
	public:
		static constexpr std::int32_t redTeam = HardpointMode::redTeam;
		static constexpr std::int32_t blueTeam = HardpointMode::blueTeam;
		static constexpr float zoneRadius = HardpointMode::zoneRadius;
		static constexpr float respawnKeepOut = HardpointMode::respawnKeepOut; // HQ spots keep this far from the spawns
		static constexpr std::int32_t pointsPerSecond = 1;
		static constexpr std::int32_t captureBonus = 10;
		static constexpr std::chrono::seconds nextHqDelay{ 5 };

		enum class Phase { Waiting, Neutral, Held };

		HeadquartersMode();
		explicit HeadquartersMode(std::uint32_t seed); // unit tests

		void onMatchStart(MatchContext& match) override;
		void onRoundStart(MatchContext& match) override;
		void onDeath(MatchContext& match, PartyPlayer& victim, PartyPlayer* killer) override;
		void onRespawn(MatchContext& match, PartyPlayer& player) override;
		[[nodiscard]] bool keepsLateJoinerDead(const PartyPlayer& player) const override;
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
		[[nodiscard]] Phase phase() const { return m_phase; }
		[[nodiscard]] std::int32_t owner() const { return m_owner; }
		[[nodiscard]] std::size_t currentSpot() const { return m_spot; }

		[[nodiscard]] static double captureSeconds(const MatchContext& match);
		[[nodiscard]] static std::chrono::seconds lifetime(const MatchContext& match);
		[[nodiscard]] static std::int32_t pointsLimit(const MatchContext& match);
		[[nodiscard]] static double speed(std::int32_t playersInside); // capture / destroy speed-up

	private:
		void tickZone(MatchContext& match);
		void updateSpawns(MatchContext& match);
		void bringOnline(MatchContext& match, bool announceIt);
		void capture(MatchContext& match, std::int32_t team, std::span<PartyPlayer* const> capturers);
		void endHq(MatchContext& match, Common::HaveApi::Announcement event);
		void addPoints(MatchContext& match, std::int32_t team, std::int32_t points);
		void sendZone(MatchContext& match, std::int32_t state);
		void sendProgress(MatchContext& match, Common::HaveApi::HqProgressKind kind, std::int32_t team, double progress,
			double ratePerSecond, std::int32_t secondsLeft);
		[[nodiscard]] std::int32_t winner() const;

		std::mt19937 m_random;
		TeamBaseSpawns m_teamSpawns;
		std::vector<Common::HaveApi::ZoneSpot> m_spots;
		std::size_t m_spot = 0;
		Phase m_phase = Phase::Waiting;
		std::chrono::seconds m_phaseSince{ 0 };
		std::int32_t m_owner = 0;            // team holding the HQ (Held)
		std::int32_t m_workTeam = 0;          // team capturing / destroying
		double m_work = 0.0;                  // seconds of capture / destroy work done
		std::int32_t m_zoneState = -1;        // last 0x27 state sent
		std::array<std::int32_t, 2> m_points{};
	};
}

#endif
