/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#ifndef MAIN_HAVE_API_MODES_ELIMINATION_SHUFFLE_MODE_H
#define MAIN_HAVE_API_MODES_ELIMINATION_SHUFFLE_MODE_H

#include <array>
#include <random>
#include "HaveApi/Modes/PartyMode.h"

namespace Main::HaveApi
{
	// Elimination Shuffle (party game 9, not in the original DLL): the game's Elimination (red against blue, no respawn
	// during a round, a team wins the round when the other one is wiped out), but every round is played on another map (no
	// repeats; PartyMatch::reloadMap between the rounds). Settings: round time (the official Elimination's 2 / 4 minutes,
	// roomoptionInfo.cdb 206001 / 206002) and rounds (3, 5, 7, 11: 406001..406004).
	// Design decisions (doc/have_api_server_open_questions.md):
	//  - teams drawn at random and balanced at the match start, kept for the whole match (as the official mode);
	//  - time up: a draw, nobody wins the round;
	//  - the radar (every living player's marker, 0x1B) for both teams in the round's last minute;
	//  - the rounds setting is the rounds a team must win: the match goes on until one does (draws do not count);
	//  - a kill of an enemy is worth KillPoints, every player of the round's winning team gets LastStandingPoints;
	//  - a late joiner goes to the smaller team and waits for the next round.
	class EliminationShuffleMode final : public PartyMode
	{
	public:
		EliminationShuffleMode();
		explicit EliminationShuffleMode(std::uint32_t seed); // unit tests

		void onMatchStart(MatchContext& match) override;
		void onRoundStart(MatchContext& match) override;
		void onDeath(MatchContext& match, PartyPlayer& victim, PartyPlayer* killer) override;
		void onTick(MatchContext& match) override;
		[[nodiscard]] bool isRoundOver(MatchContext& match) override;
		void onRoundEnd(MatchContext& match, RoundEndReason reason) override;
		void onMatchEnd(MatchContext& match) override;
		[[nodiscard]] bool respawnAllowed() const override { return false; }
		[[nodiscard]] std::int32_t teamForLateJoiner(MatchContext& match) override;
		[[nodiscard]] std::uint32_t roundsPerMatch(std::uint32_t configured) const override;
		[[nodiscard]] bool changesMapEveryRound() const override { return true; }
		[[nodiscard]] bool isMatchDecided(const MatchContext& match) const override;
		// no "Round N" banner: the official ROUND panel shows the round
		[[nodiscard]] bool showsRounds() const override { return false; }

		[[nodiscard]] std::int32_t roundsWon(std::int32_t team) const { return m_wins[static_cast<std::size_t>(team)]; }

		static constexpr std::int32_t red = 1;
		static constexpr std::int32_t blue = 2;
		static constexpr std::uint32_t radarSeconds = 60;
		static constexpr std::uint32_t maxRounds = 255; // a safety stop (draws only)

	private:
		void sendRounds(MatchContext& match) const;

		std::mt19937 m_random;
		std::array<std::int32_t, 3> m_wins{}; // [red], [blue]
		std::uint32_t m_rounds = 5;
		bool m_radar = false;
	};
}

#endif
