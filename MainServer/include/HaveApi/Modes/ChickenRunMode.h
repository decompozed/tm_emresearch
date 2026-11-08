/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#ifndef MAIN_HAVE_API_MODES_CHICKEN_RUN_MODE_H
#define MAIN_HAVE_API_MODES_CHICKEN_RUN_MODE_H

#include <random>
#include "HaveApi/Modes/PartyMode.h"

namespace Main::HaveApi
{
	// Chicken Run (party game 1). DLL description: "One player is randomly chosen to become the chicken. If the chicken
	// hits another player, this player becomes the new chicken. Whoever is chicken when the round ends, loses."
	// Settings: round time (1..3 minutes), explosives, bonus.
	// Evidence from the DLL (mode 1): roster team 2 = the chicken (chicken icon on its row, the "you are the chicken"
	// control shown to it), team 1 = the others; 0x17 first value = the chicken: only hits between the chicken and
	// the others count as enemy hits (hook "team diff"); 0x17 second value = a player that blinks on the chicken's
	// screen (hook "chicken mark") and has its own icon on the scoreboard.
	// Design decisions (doc/have_api_server_open_questions.md):
	//  - the chicken is drawn at random at every round start ("<name> IS NOW THE CHICKEN!");
	//  - a hit of the chicken on another player passes the role ("<name> IS NOW THE CHICKEN!"); the previous chicken
	//    cannot be tagged back for TagBackSeconds and is the 0x17 second value (blinking) meanwhile;
	//  - explosives On: explosive hits pass the role too; Off: only weapon hits;
	//  - bonus On: passing the role is worth KillPoints to the old chicken; Off: nothing;
	//  - respawn on: deaths do not eliminate anybody;
	//  - the round ends at the time limit only: the chicken loses ("<name> WAS ELIMINATED!"), every other player gets
	//    LastStandingPoints;
	//  - a chicken that leaves the match is replaced by a random player; late joiners are ordinary players;
	//  - the match winner is the player with the most points ("<name> HAS WON", event 9).
	class ChickenRunMode final : public PartyMode
	{
	public:
		static constexpr std::int32_t playerTeam = 1;
		static constexpr std::int32_t chickenTeam = 2;
		static constexpr std::chrono::seconds tagBackTime{ 3 };

		ChickenRunMode();
		explicit ChickenRunMode(std::uint32_t seed); // unit tests

		void onMatchStart(MatchContext& match) override;
		void onRoundStart(MatchContext& match) override;
		void onHit(MatchContext& match, PartyPlayer& victim, PartyPlayer& attacker, Common::HaveApi::DeathCause cause) override;
		void onDeath(MatchContext&, PartyPlayer&, PartyPlayer*) override {}
		void onPlayerLeft(MatchContext& match, PartyPlayer& player) override;
		void onTick(MatchContext& match) override;
		[[nodiscard]] bool isRoundOver(MatchContext&) override { return false; }
		void onRoundEnd(MatchContext& match, RoundEndReason reason) override;
		void onMatchEnd(MatchContext& match) override;
		[[nodiscard]] bool respawnAllowed() const override { return true; }
		[[nodiscard]] std::optional<std::pair<Common::HaveApi::Announcement, std::string>> roundStartAnnouncement(
			const MatchContext& match) const override;
		[[nodiscard]] std::int32_t teamForLateJoiner(MatchContext&) override { return playerTeam; }

		[[nodiscard]] std::int32_t chicken() const { return m_chicken; }

	private:
		void makeChicken(MatchContext& match, PartyPlayer& player, std::int32_t previous);
		void drawChicken(MatchContext& match, std::int32_t excludedPlayerId);
		[[nodiscard]] PartyPlayer* find(MatchContext& match, std::int32_t playerId) const;

		std::mt19937 m_random;
		std::int32_t m_chicken = 0;                 // player id
		std::int32_t m_tagBackBlocked = 0;          // player id of the previous chicken while it cannot be tagged back
		std::chrono::seconds m_tagBackUntil{ 0 };   // round time
	};
}

#endif
