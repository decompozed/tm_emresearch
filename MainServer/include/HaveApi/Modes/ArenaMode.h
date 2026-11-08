/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#ifndef MAIN_HAVE_API_MODES_ARENA_MODE_H
#define MAIN_HAVE_API_MODES_ARENA_MODE_H

#include "HaveApi/Modes/PartyMode.h"

namespace Main::HaveApi
{
	// Arena (party game 3). DLL description: "Everyone versus everyone. The last figure standing gets extra points.
	// Dead players must wait until a new round begins." Settings: round time, heal winner, radar phase.
	// Design decisions (doc/have_api_server_open_questions.md):
	//  - a kill is worth [HaveApi] KillPoints, the last one standing gets LastStandingPoints;
	//  - heal winner (as the original server): On = the round winner starts the next round with full health; Off = it
	//    keeps the health it had left (the eliminated players respawn with full health anyway);
	//  - radar phase: from the second half of the round every living player's overhead marker is shown (0x1B);
	//  - a round ends when at most one player is alive (with two or more players) or at the time limit;
	//  - the match winner is the player with the most points.
	class ArenaMode final : public PartyMode
	{
	public:
		void onMatchStart(MatchContext& match) override;
		void onRoundStart(MatchContext& match) override;
		void onDeath(MatchContext& match, PartyPlayer& victim, PartyPlayer* killer) override;
		void onTick(MatchContext& match) override;
		[[nodiscard]] bool isRoundOver(MatchContext& match) override;
		void onRoundEnd(MatchContext& match, RoundEndReason reason) override;
		void onMatchEnd(MatchContext& match) override;
		[[nodiscard]] bool respawnAllowed() const override { return false; }
		[[nodiscard]] bool healSurvivorsAtRoundStart(const MatchContext& match) const override;

	private:
		RadarPhase m_radar;
	};
}

#endif
