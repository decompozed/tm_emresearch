/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#ifndef MAIN_HAVE_API_MATCH_LOG_H
#define MAIN_HAVE_API_MATCH_LOG_H

#include <chrono>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include "HaveApi/BattleMessages.h"
#include "HaveApi/HitTally.h"
#include "HaveApi/Lava.h"
#include "HaveApi/Messages.h"
#include "HaveApi/Modes/PartyMode.h"

namespace Main::HaveApi
{
	// The MainServer's side of a party match's mode log (HaveApi/ModeLog.h): room, round, and the texts of its events.
	class MatchLog
	{
	public:
		using Clock = std::chrono::steady_clock;

		MatchLog(std::uint16_t room, std::int32_t modeId) noexcept : m_room{ room }, m_modeId{ modeId } {}

		void setRound(std::uint32_t round) noexcept { m_round = round; }
		void write(std::string_view event) const;

		// true when `value` differs from the last one of `key` (repeated states are written once)
		bool changed(const std::string& key, std::string value);
		// changed() and at least `interval` since the last line of `key` (values that move every second)
		bool changedAtMostEvery(const std::string& key, std::string value, Clock::duration interval, Clock::time_point now = Clock::now());

		void hit(const PartyPlayer& attacker, const PartyPlayer& victim, Common::HaveApi::DeathCause cause);
		void writeHits(std::span<const PartyPlayer> players); // the hit summary since the last one (round end)

		[[nodiscard]] static std::string describe(const PartyPlayer& player);
		[[nodiscard]] static std::string describe(std::span<const PartyPlayer> players);
		[[nodiscard]] static std::string describe(const Common::HaveApi::LavaCommand& command);
		[[nodiscard]] static std::string describe(const Common::HaveApi::LavaState& state);
		[[nodiscard]] static std::string describe(const Common::HaveApi::Modifiers& modifiers);
		[[nodiscard]] static std::string_view causeName(Common::HaveApi::DeathCause cause);

	private:
		std::uint16_t m_room;
		std::int32_t m_modeId;
		std::uint32_t m_round = 1;
		Common::HaveApi::HitTally m_hits;
		std::map<std::string, std::string, std::less<>> m_last;
		std::map<std::string, Clock::time_point, std::less<>> m_lastAt;
	};
}

#endif
