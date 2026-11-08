/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#ifndef MAIN_HAVE_API_MODES_DOMINATION_MODE_H
#define MAIN_HAVE_API_MODES_DOMINATION_MODE_H

#include <array>
#include <map>
#include <random>
#include <vector>
#include "HaveApi/DominationZones.h"
#include "HaveApi/Modes/PartyMode.h"
#include "HaveApi/Modes/TeamBaseSpawns.h"

namespace Main::HaveApi
{
	// Domination (party game 10, after Call of Duty's mode; not in the original DLL). Two teams (red / blue, balanced),
	// three fixed zones A / B / C per map (Common/include/HaveApi/DominationZones.h: A on the red base's side, B on the
	// blue base's, C between them), squares of zoneHalfSize on the floor (a player counts between zoneBelow under and
	// zoneAbove over the floor, CastServer PartyZone).
	//  - capture: a team alone in a zone it does not hold captures it in captureSeconds (2 players x1.5, 3+ x2, as
	//    Headquarters); an enemy zone passes to it directly (no neutral step); both teams inside: paused (contested);
	//    the capturers left (or died): the progress is lost; capturers get LastStandingPoints each;
	//  - points: 1 a second for every zone a team holds; the points limit (setting, 100..350) wins, or at the time limit
	//    (setting, Off / minutes) the most points; equal points = draw;
	//  - respawns: at the team's official base (TeamBaseSpawns; A lies on the red base's side, B on the blue one's, C in
	//    the middle, DominationZones.h); the teams swap bases (spawnBaseOf, never one base for both) when a team holds the
	//    other's home zone and has not lost its own to it;
	//  - announcements: start, "we captured X", "enemy captured X" (the others), "X under attack" (owners, once per attack).
	class DominationMode final : public PartyMode
	{
	public:
		static constexpr std::int32_t redTeam = 1;
		static constexpr std::int32_t blueTeam = 2;
		static constexpr std::size_t zoneCount = 3;
		static constexpr float zoneHalfSize = 500.0f;
		static constexpr float zoneBelow = 200.0f;      // the band a player counts in, around the floor height
		static constexpr float zoneAbove = 500.0f;
		static constexpr float respawnKeepOut = 1000.0f; // a respawn this close to a zone: not an occupant for the spawn grace
		static constexpr double captureSeconds = 10.0;
		static constexpr std::int32_t pointsPerZone = 1;

		struct Zone
		{
			std::int32_t owner = 0;
			std::int32_t workTeam = 0; // the team capturing it
			double work = 0.0;         // seconds of capture work done
			double speed = 0.0;        // work per second now (0: paused / nobody capturing)
			bool contested = false;
			bool attackAnnounced = false;
		};

		DominationMode();
		explicit DominationMode(std::uint32_t seed); // unit tests

		void onMatchStart(MatchContext& match) override;
		void onRoundStart(MatchContext& match) override;
		void onDeath(MatchContext& match, PartyPlayer& victim, PartyPlayer* killer) override;
		void onRespawn(MatchContext& match, PartyPlayer& player) override;
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
		[[nodiscard]] const Zone& zone(std::size_t index) const { return m_zones[index]; }
		[[nodiscard]] const Common::HaveApi::DominationMap* map() const { return m_map; }
		[[nodiscard]] std::int32_t spawnBaseOf(std::int32_t team) const; // the team whose base `team` respawns at

		[[nodiscard]] static std::int32_t pointsLimit(const MatchContext& match);
		[[nodiscard]] static double speed(std::int32_t playersInside);
		// the map's Domination data, or the room's map is none of them: a random Domination map
		[[nodiscard]] static bool supportsMap(std::uint32_t mapId);

	private:
		void placeZones(MatchContext& match, bool active);
		void capture(MatchContext& match, std::size_t index, std::int32_t team, std::span<PartyPlayer* const> capturers);
		void addPoints(MatchContext& match, std::int32_t team, std::int32_t points);
		void sendZones(MatchContext& match);
		[[nodiscard]] std::int32_t winner() const;

		std::mt19937 m_random;
		const Common::HaveApi::DominationMap* m_map = nullptr;
		std::array<Zone, zoneCount> m_zones{};
		std::array<std::int32_t, 2> m_points{};
		bool m_running = false;
		std::vector<std::uint8_t> m_lastZones;                           // last 0x37 payload sent
		TeamBaseSpawns m_teamSpawns;
	};
}

#endif
