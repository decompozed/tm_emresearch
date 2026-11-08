/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#ifndef MAIN_HAVE_API_MODES_TEAM_CLASH_MODE_H
#define MAIN_HAVE_API_MODES_TEAM_CLASH_MODE_H

#include <random>
#include "HaveApi/Modes/PartyMode.h"

namespace Main::HaveApi
{
	// Team Clash (party game 4). DLL description: "Round based game mode where up to 5 teams fight against each other.
	// Players of the same team spawn together. Teams can be configured to be kept or randomly chosen each round."
	// Settings: round time, keep teams, radar phase, number of teams (3..5).
	// Evidence from the DLL: team 1..5 = red, blue, orange, green, purple; the team panel counts the players whose team
	// (roster "state") is 1..teams, so eliminated players are team 0 and keep their team as fallback; the DLL itself
	// tells team mates from enemies by team, falling back to the fallback team (hook "team diff").
	// Design decisions (doc/have_api_server_open_questions.md):
	//  - teams are filled at random and balanced (sizes differ by at most one); with keep teams Off they are drawn
	//    again every round, with On they stay for the whole match (unless the players who left leave a single team);
	//  - a kill of an enemy is worth KillPoints; a kill of a team mate gives nothing;
	//  - a round ends when at most one team has living players (with players in two teams or more) or at the time
	//    limit; every player of the winning team gets LastStandingPoints;
	//  - a team whose last living player dies is announced as eliminated;
	//  - radar phase: as Arena, from the second half of the round;
	//  - a player joining a running match goes to the smallest team and waits for the next round;
	//  - the match winner: keep teams On = the team with the most points; Off = the player with the most points.
	class TeamClashMode final : public PartyMode
	{
	public:
		TeamClashMode();
		explicit TeamClashMode(std::uint32_t seed); // unit tests

		void onMatchStart(MatchContext& match) override;
		void onRoundStart(MatchContext& match) override;
		void onDeath(MatchContext& match, PartyPlayer& victim, PartyPlayer* killer) override;
		void onTick(MatchContext& match) override;
		[[nodiscard]] bool isRoundOver(MatchContext& match) override;
		void onRoundEnd(MatchContext& match, RoundEndReason reason) override;
		void onMatchEnd(MatchContext& match) override;
		[[nodiscard]] bool respawnAllowed() const override { return false; }
		[[nodiscard]] std::optional<std::pair<Common::HaveApi::Announcement, std::string>> roundStartAnnouncement(
			const MatchContext& match) const override;
		[[nodiscard]] std::int32_t teamForLateJoiner(MatchContext& match) override;

		[[nodiscard]] static std::int32_t teamCount(const MatchContext& match);

	private:
		void drawTeams(MatchContext& match);

		std::mt19937 m_random;
		RadarPhase m_radar;
		bool m_teamsDrawn = false;
	};
}

#endif
