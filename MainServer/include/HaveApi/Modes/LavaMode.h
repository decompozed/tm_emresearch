/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#ifndef MAIN_HAVE_API_MODES_LAVA_MODE_H
#define MAIN_HAVE_API_MODES_LAVA_MODE_H

#include "HaveApi/Modes/PartyMode.h"

namespace Main::HaveApi
{
	// Lava Escape (party game 5). DLL description: "Deadly lava will slowly rise until one player remains. Rockets and
	// grenades can be used to push other players!" Settings: speed (5 / 8 / 10 / 15: Slow, Normal, Fast, Extreme),
	// grace period (Off / Normal / Long), walls, hot phase, sleep phase. Announcements 24 (<name> HAS WON), 25 (LAVA IS
	// COMING!), 26 (HOT PHASE HAS STARTED x2), 27 (THE LAVA HAS STOPPED); 0x25 shows the hot / sleep icons.
	// The DLL only draws the lava; CastServer places it and kills (Common/include/HaveApi/Lava.h).
	// Design decisions (doc/have_api_server_open_questions.md):
	//  - everyone for themselves (own teams: explosives push the others), but players cannot kill each other:
	//    CastServer turns a hit that would take the last hit point into 1; nobody respawns during the round; the last one
	//    alive gets LastStandingPoints and "<name> HAS WON";
	//  - grace period Off / Normal / Long = the lava starts 3 (every player loaded and moving) / 5 / 10 s after the round
	//    start ("LAVA IS COMING!");
	//  - floor speed = setting x LavaSpeedScale world units/s (default 3: Normal = 24/s); the walls reach the map
	//    centre in LavaWallCloseSeconds x 8 / setting (default 120 s at Normal), whatever the map size;
	//  - hot phase: 60 s after the lava started its speed doubles; sleep phase: after every 45 s of moving it stops
	//    for 5 s;
	//  - the round ends when at most one player is alive or at LavaRoundSeconds; the match winner has the most points.
	class LavaMode final : public PartyMode
	{
	public:
		static constexpr std::chrono::seconds hotPhaseAfter{ 60 };
		static constexpr std::chrono::seconds sleepEvery{ 45 };
		static constexpr std::chrono::seconds sleepFor{ 5 };
		static constexpr std::chrono::seconds minimumStart{ 3 }; // positions of every player reach CastServer first

		void onMatchStart(MatchContext& match) override;
		void onRoundStart(MatchContext& match) override;
		void onDeath(MatchContext& match, PartyPlayer& victim, PartyPlayer* killer) override;
		void onTick(MatchContext& match) override;
		[[nodiscard]] bool isRoundOver(MatchContext& match) override;
		void onRoundEnd(MatchContext& match, RoundEndReason reason) override;
		void onMatchEnd(MatchContext& match) override;
		[[nodiscard]] bool respawnAllowed() const override { return false; }

		[[nodiscard]] static std::chrono::seconds gracePeriod(const MatchContext& match);

	private:
		[[nodiscard]] Common::HaveApi::LavaCommand command(const MatchContext& match, Common::HaveApi::LavaAction action, float factor) const;

		bool m_started = false;
		bool m_starting = false; // a start request waits for the CastServer's answer
		std::uint32_t m_round = 0; // stale answers (of an ended round) are ignored
		bool m_hot = false;
		bool m_sleeping = false;
		std::chrono::seconds m_startedAt{ 0 };
		std::chrono::seconds m_movingSince{ 0 };
		std::chrono::seconds m_sleepUntil{ 0 };
	};
}

#endif
