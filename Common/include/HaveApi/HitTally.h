/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#ifndef COMMON_HAVE_API_HIT_TALLY_H
#define COMMON_HAVE_API_HIT_TALLY_H

#include <cstdint>
#include <map>
#include <utility>
#include <vector>
#include "HaveApi/CombatEvent.h"

namespace Common::HaveApi
{
	// Hits per attacker -> victim (session ids) since the last take(): the mode logs write one summary line per pair
	// and round instead of one line per hit.
	class HitTally
	{
	public:
		struct Entry
		{
			std::uint32_t attacker{};
			std::uint32_t victim{};
			std::uint32_t weapon{};
			std::uint32_t explosive{};
			std::uint32_t other{};
			std::uint32_t spared{};

			[[nodiscard]] std::uint32_t total() const noexcept { return weapon + explosive + other; }
		};

		void add(std::uint32_t attacker, std::uint32_t victim, DeathCause cause, bool spared = false)
		{
			Entry& entry = m_entries[{ attacker, victim }];
			entry.attacker = attacker;
			entry.victim = victim;
			switch (cause)
			{
			case DeathCause::Weapon: ++entry.weapon; break;
			case DeathCause::Explosive: ++entry.explosive; break;
			default: ++entry.other; break;
			}
			entry.spared += spared ? 1 : 0;
		}

		[[nodiscard]] bool empty() const noexcept { return m_entries.empty(); }

		[[nodiscard]] std::vector<Entry> take()
		{
			std::vector<Entry> entries;
			entries.reserve(m_entries.size());
			for (const auto& [key, entry] : m_entries)
			{
				entries.push_back(entry);
			}
			m_entries.clear();
			return entries;
		}

	private:
		std::map<std::pair<std::uint32_t, std::uint32_t>, Entry> m_entries;
	};
}

#endif
