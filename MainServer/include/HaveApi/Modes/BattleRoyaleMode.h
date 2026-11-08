/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#ifndef MAIN_HAVE_API_MODES_BATTLE_ROYALE_MODE_H
#define MAIN_HAVE_API_MODES_BATTLE_ROYALE_MODE_H

#include <array>
#include <chrono>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <random>
#include <set>
#include <span>
#include <string_view>
#include "HaveApi/Modes/BattleRoyaleStorm.h"
#include "HaveApi/Modes/PartyMode.h"

namespace Main::HaveApi
{
	class BattleRoyaleMode final : public PartyMode
	{
	public:
		static constexpr auto dropTimeout = std::chrono::seconds{ 40 };
		static constexpr std::int32_t dropCountdownSeconds = 5;
		static constexpr std::array<std::int32_t, 5> placementPoints = { 10, 6, 4, 2, 2 };
		static constexpr double fallbackDamageFactor = 1.6;
		static constexpr auto lethalWindow = std::chrono::seconds{ 2 };
		static constexpr double radarPulseSeconds = 5.0;
		static constexpr double closingWarningSeconds = 30.0;
		static constexpr double inStormRepeatSeconds = 10.0;
		static constexpr std::size_t finalArenaSetting = 1;
		static constexpr std::size_t stormDamageSetting = 2;
		enum class StormDamage : std::int32_t { Timer = 1, Gradual = 2 };

		BattleRoyaleMode() : BattleRoyaleMode(std::random_device{}()) {}
		explicit BattleRoyaleMode(std::uint32_t seed) : m_random{ seed }, m_spawnRandom{ seed + 1 } {}

		void onMatchStart(MatchContext& match) override;
		void onRoundStart(MatchContext& match) override;
		void onDeath(MatchContext& match, PartyPlayer& victim, PartyPlayer* killer) override;
		void onPlayerLeft(MatchContext& match, PartyPlayer& player) override;
		void onTick(MatchContext& match) override;
		[[nodiscard]] bool isRoundOver(MatchContext& match) override;
		void onRoundEnd(MatchContext& match, RoundEndReason reason) override;
		void onMatchEnd(MatchContext& match) override;
		[[nodiscard]] bool respawnAllowed() const override { return false; }
		[[nodiscard]] std::uint32_t roundsPerMatch(std::uint32_t) const override { return 1; }
		[[nodiscard]] bool showsRounds() const override { return false; }
		[[nodiscard]] bool acceptsLateJoiners() const override { return false; }
		void onStormLethal(MatchContext& match, PartyPlayer& victim) override;

		[[nodiscard]] bool armed() const { return m_drop == Drop::Over; }
		[[nodiscard]] const StormPlan* storm() const { return m_storm.get(); }
		[[nodiscard]] std::int32_t stormDamageOf(std::int32_t playerId) const;
		static constexpr std::array<BR::Arena, BR::arenaCount> spawnOrder = { BR::Arena::Paper, BR::Arena::Castle, BR::Arena::Pvc, BR::Arena::Studio };
		[[nodiscard]] static Common::HaveApi::BattleSpawnsPayload spreadOverArenas(std::span<const PartyPlayer> players, std::mt19937& random);

	private:
		enum class Drop { Loading, Countdown, Over };

		void arm(MatchContext& match, bool armed);
		void placeSurvivors(MatchContext& match);
		void announceWinner(MatchContext& match);
		void startStorm(MatchContext& match);
		void warnClosings(MatchContext& match, double t);
		void announcePhase(MatchContext& match, const StormPlan::Phase& phase);
		void updateStorm(MatchContext& match);
		void stopStorm(MatchContext& match);
		void hurt(MatchContext& match, const StormPlan::Phase& phase, double t);
		void stormKill(MatchContext& match, const PartyPlayer& player);
		[[nodiscard]] bool gradual(const MatchContext& match) const;

		Drop m_drop = Drop::Loading;
		std::chrono::seconds m_countdownFrom{};
		bool m_placed = false;
		bool m_announced = false;
		std::mt19937 m_random;
		std::mt19937 m_spawnRandom;
		std::unique_ptr<StormPlan> m_storm;
		std::size_t m_sentSegment = std::numeric_limits<std::size_t>::max();
		std::size_t m_announcedPhase = 0;
		std::size_t m_warnedClosings = 0;
		std::map<std::int32_t, double> m_inStormAnnounced;
		bool m_radarPulsed = false;
		bool m_radarOn = false;
		std::map<std::int32_t, double> m_outsideSince;
		std::map<std::int32_t, std::int32_t> m_stormDamage;
		std::set<std::int32_t> m_stormKills;
	};
}

#endif
