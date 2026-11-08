/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#ifndef CAST_ROOMS_PARTY_LOG_H
#define CAST_ROOMS_PARTY_LOG_H

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include "Detail/LogThrottle.h"
#include "HaveApi/HitTally.h"
#include "HaveApi/PartyMatchInfo.h"

namespace Cast::Domain
{
	// The room's party game and round (from MainServer, M2C_partyMatchInfo) for the mode log, the hits of the round and
	// the throttles of the dropped packets.
	class PartyLog
	{
	public:
		using Clock = Details::LogThrottle::Clock;
		static constexpr std::chrono::seconds dropInterval{ 2 };

		// true: the previous round is over (its hits must be written first)
		bool setMatch(const Common::HaveApi::PartyMatchInfo& info);
		[[nodiscard]] std::int32_t modeId() const noexcept { return m_info.modeId; }
		[[nodiscard]] std::uint32_t round() const noexcept { return m_info.round; }
		[[nodiscard]] bool running() const noexcept { return m_info.running != 0; }

		void write(std::uint32_t room, std::string_view event) const;
		Common::HaveApi::HitTally& hits() noexcept { return m_hits; }
		// false while the same drop was logged less than dropInterval ago; `suppressed`: the drops not logged since
		bool allowDrop(const std::string& key, Clock::time_point now, std::uint32_t& suppressed);

	private:
		Common::HaveApi::PartyMatchInfo m_info{};
		Common::HaveApi::HitTally m_hits;
		std::map<std::string, Details::LogThrottle, std::less<>> m_drops;
	};
}

#endif
