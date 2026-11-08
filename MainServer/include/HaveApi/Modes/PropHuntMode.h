/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#ifndef MAIN_HAVE_API_MODES_PROP_HUNT_MODE_H
#define MAIN_HAVE_API_MODES_PROP_HUNT_MODE_H

#include <random>
#include <unordered_map>
#include "HaveApi/Modes/PartyMode.h"

namespace Main::HaveApi
{
	// Prop Hunt (party game 8, not in the original DLL):
	//  - every round about one player in three (at least one, never everybody) is a HUNTER (team 1), the others are
	//    PROPS (team 2); the roles rotate: the players who hunted least hunt next (random among equals);
	//  - a prop is one of the objects of the map (mapNNN/prop_NN.nif, Tools/nif/make_prop_models.py; drawn by the DLL
	//    in place of the character); props cannot damage anybody (0x14 canDamageOthers 0);
	//  - hide time (setting): the hunters cannot move or shoot (0x14 movement scale 0, canDamageOthers 0) and the DLL
	//    blacks out their screen; then the hunters are released;
	//  - no radar for the hunters;
	//  - props see their team mates' markers (the DLL); a prop that is hit is told "you've been found, run!" (at most
	//    every foundRepeatSeconds); a prop's flash tells it whether a hunter was within the radius;
	//  - the hunters' penalty: every hit on a decoy costs the hunter decoyPenalty hit points (its DLL); a hunter at 0
	//    dies for everybody (0x2D PenaltyDeath); its DLL shows "that's a decoy!" at the first hit;
	//  - abilities of a prop (DLL keys F4, F6..F9, message 0x2D): change object (setting: 2..30 per round; an object not
	//    used yet in the round while there is one), lock rotation (any time), decoy: a still copy of its object where
	//    it stands (setting: 2..10 per round), flash: hunters within flashRadius are blinded for a few seconds (1 per
	//    round, once the hunters are released), wall (F4): the object hangs on the wall next to the prop like a picture
	//    until the prop moves away (the DLL finds the wall and checks the distance);
	//  - props have propHealth hit points (0x14 max health override);
	//  - decoys can be shot: a hunter's DLL counts its hits on a decoy and reports it broken (0x2D DecoyHit), the
	//    server removes it for everybody (0x2B RemoveDecoy);
	//  - no respawn in a round; a found prop is out; the hunters win when every prop is out, the props win when the time
	//    is up with one alive (or every hunter is out); winners get LastStandingPoints each, a prop kill KillPoints;
	//  - rounds: setting (3 / 5), round time: setting (minutes, the hide time included);
	//  - not yet (needs reverse engineering of a missed shot): the hunters' penalty for shooting objects.
	class PropHuntMode final : public PartyMode
	{
	public:
		static constexpr std::int32_t hunterTeam = 1;
		static constexpr std::int32_t propTeam = 2;
		static constexpr std::int32_t flashesPerRound = 1;  // no 7th settings slot (message 0x0A has 6 values)
		static constexpr float flashRadius = 1000.0f; // a bit farther than the first 700
		static constexpr std::int32_t propHealth = 5000; // props have 5000 hit points (players 2000)
		static constexpr std::int32_t fallbackModelCount = 10;

		enum class Phase { Off, Hiding, Hunting };

		PropHuntMode();
		explicit PropHuntMode(std::uint32_t seed); // unit tests

		void onMatchStart(MatchContext& match) override;
		void onRoundStart(MatchContext& match) override;
		void onDeath(MatchContext& match, PartyPlayer& victim, PartyPlayer* killer) override;
		void onHit(MatchContext& match, PartyPlayer& victim, PartyPlayer& attacker, Common::HaveApi::DeathCause cause) override;
		void onTick(MatchContext& match) override;
		[[nodiscard]] bool isRoundOver(MatchContext& match) override;
		void onRoundEnd(MatchContext& match, RoundEndReason reason) override;
		void onMatchEnd(MatchContext&) override {}
		void onPropAction(MatchContext& match, PartyPlayer& player, const Common::HaveApi::PropActionPayload& action) override;
		[[nodiscard]] bool respawnAllowed() const override { return false; }
		[[nodiscard]] std::int32_t teamForLateJoiner(MatchContext&) override { return hunterTeam; }
		[[nodiscard]] std::uint32_t roundsPerMatch(std::uint32_t) const override { return m_rounds; }

		[[nodiscard]] Phase phase() const { return m_phase; }
		[[nodiscard]] std::int32_t modelOf(std::int32_t playerId) const;
		[[nodiscard]] static std::int32_t hunterCount(std::size_t players);
		[[nodiscard]] static std::chrono::seconds hideTime(const MatchContext& match);
		static constexpr std::int32_t foundRepeatSeconds = 8;

	private:
		struct PropUses
		{
			std::int32_t model = 0;
			bool locked = false;
			std::int32_t changes = 0;
			std::int32_t decoys = 0;
			std::int32_t flashes = 0;
			std::vector<std::int32_t> used; // objects of this round: a change picks one not used yet while there is one
			Common::HaveApi::PropWall wall{};
		};

		void sendPhase(MatchContext& match, const PartyPlayer& player);
		void sendPhases(MatchContext& match);
		[[nodiscard]] std::int32_t modelCount(const MatchContext& match) const;
		[[nodiscard]] std::int32_t nextModel(PropUses& uses, std::int32_t models);

		std::mt19937 m_random;
		std::uint32_t m_rounds = 3;
		Phase m_phase = Phase::Off;
		std::chrono::seconds m_huntSince{ 0 };
		std::unordered_map<std::int32_t, std::chrono::seconds> m_lastFound; // prop -> when it was last told "found"
		std::unordered_map<std::int32_t, std::int32_t> m_timesHunted; // player id -> rounds as hunter
		std::unordered_map<std::int32_t, PropUses> m_props;           // player id -> object and abilities
	};
}

#endif
