/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#ifndef CAST_ROOMS_PARTY_STORM_H
#define CAST_ROOMS_PARTY_STORM_H

#include <array>
#include <chrono>
#include <cstdint>
#include <span>
#include <unordered_set>
#include <vector>
#include "HaveApi/Storm.h"
#include "Rooms/PartyLava.h"

namespace Cast::Domain
{
	class PartyStorm
	{
	public:
		using Clock = PartyLava::Clock;

		static constexpr std::chrono::seconds staleAfter{ 10 };

		void set(const Common::HaveApi::StormCommand& command, Clock::time_point now = Clock::now());
		void clear();
		[[nodiscard]] bool active() const noexcept { return m_active; }
		[[nodiscard]] bool contains(float x, float y, Clock::time_point now = Clock::now()) const;

		Common::HaveApi::StormAnswer evaluate(const PartyLava& positions, Clock::time_point now = Clock::now());
		bool update(std::uint32_t sessionId, float x, float y, Common::HaveApi::StormAnswer& changed, Clock::time_point now = Clock::now());
		[[nodiscard]] Common::HaveApi::StormAnswer outside() const;
		[[nodiscard]] std::vector<std::uint32_t> takeStale(const PartyLava& positions, Clock::time_point now = Clock::now());

	private:
		[[nodiscard]] std::span<const Common::HaveApi::StormBox> boxes() const { return std::span{ m_boxes }.first(m_count); }
		[[nodiscard]] float secondsSince(Clock::time_point now) const;

		std::array<Common::HaveApi::StormBox, Common::HaveApi::maxStormBoxes> m_boxes{};
		std::size_t m_count{};
		Clock::time_point m_since{};
		bool m_active{};
		std::vector<std::uint32_t> m_members;
		std::vector<std::uint32_t> m_outside;
		std::unordered_set<std::uint32_t> m_staleReported;
	};
}

#endif
