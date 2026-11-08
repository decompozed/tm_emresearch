/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#ifndef MAIN_HAVE_API_MODES_CHASER_MODE_H
#define MAIN_HAVE_API_MODES_CHASER_MODE_H

#include <map>
#include <random>
#include <vector>
#include "HaveApi/Modes/PartyMode.h"

namespace Main::HaveApi
{
	// Chaser (party game 2). DLL description: "Each round, one player becomes the chaser. The chaser must eliminate
	// all players and receives bonus points for killing the marked target players. Players can fight back to
	// survive." Setting: round time (1.5..3 minutes).
	// Evidence from the DLL (mode 2) and an in-game test:
	//  - roster team 2 = the chaser: scoreboard icon 516, the chaser panel (control 137024) on its screen, and the
	//    relation hook makes a team-2 player an enemy of everybody; team 1 players are allies of each other;
	//  - 0x1A = the marked target: scoreboard icon 517 and an overhead marker seen by everybody while it is alive
	//    (with 0x1A = chaser, everybody saw the chaser's position and the icons were swapped);
	//  - 0x19 = up to 15 target markers, state 1 / 2 each (two icons);
	//  - 0x17 first value = the chaser, as in Chicken Run (hook "team diff"); announcements 4..7.
	// Design decisions (doc/have_api_server_open_questions.md):
	//  - every round a new chaser, among the players who were the chaser the fewest times ("<name> IS THE CHASER");
	//  - one runner in three (at least one) is a marked target; 0x19 state 1 = target alive, 2 = target eliminated;
	//    0x1A shows one of them at a time: the first living target (the next one when it is eliminated);
	//  - nobody respawns during the round; the chaser's kill is worth KillPoints, + MarkedTargetPoints for a target;
	//    a runner killing the chaser gets KillPoints;
	//  - the round ends: every runner eliminated ("ALL PLAYERS ELIMINATED", the chaser gets LastStandingPoints),
	//    the chaser eliminated or gone ("THE CHASER WAS ELIMINATED") or the time limit ("ROUND TIME UP!"): then the
	//    surviving runners get LastStandingPoints;
	//  - late joiners are runners, eliminated until the next round; the match winner has the most points.
	class ChaserMode final : public PartyMode
	{
	public:
		static constexpr std::int32_t runnerTeam = 1;   // targets too
		static constexpr std::int32_t chaserTeam = 2;
		static constexpr std::int8_t targetAlive = 1;
		static constexpr std::int8_t targetEliminated = 2;

		ChaserMode();
		explicit ChaserMode(std::uint32_t seed); // unit tests

		void onMatchStart(MatchContext& match) override;
		void onRoundStart(MatchContext& match) override;
		void onDeath(MatchContext& match, PartyPlayer& victim, PartyPlayer* killer) override;
		void onPlayerLeft(MatchContext& match, PartyPlayer& player) override;
		[[nodiscard]] bool isRoundOver(MatchContext& match) override;
		void onRoundEnd(MatchContext& match, RoundEndReason reason) override;
		void onMatchEnd(MatchContext& match) override;
		[[nodiscard]] bool respawnAllowed() const override { return false; }
		[[nodiscard]] std::optional<std::pair<Common::HaveApi::Announcement, std::string>> roundStartAnnouncement(
			const MatchContext& match) const override;
		[[nodiscard]] std::int32_t teamForLateJoiner(MatchContext&) override { return runnerTeam; }

		[[nodiscard]] std::int32_t chaser() const { return m_chaser; }
		[[nodiscard]] const std::vector<std::int32_t>& targets() const { return m_targets; }
		[[nodiscard]] std::int32_t markedTarget(MatchContext& match) const; // shown by 0x1A, 0 = none

	private:
		[[nodiscard]] PartyPlayer* find(MatchContext& match, std::int32_t playerId) const;
		[[nodiscard]] bool isTarget(std::int32_t playerId) const;
		void sendChaserState(MatchContext& match);

		std::mt19937 m_random;
		std::map<std::int32_t, int> m_timesChaser; // player id -> rounds as the chaser
		std::int32_t m_chaser = 0;
		std::vector<std::int32_t> m_targets;       // player ids, in marker order
	};
}

#endif
