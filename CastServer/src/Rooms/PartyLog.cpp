/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#include "Rooms/PartyLog.h"
#include "HaveApi/ModeLog.h"

namespace Cast::Domain
{
	namespace
	{
		constexpr std::size_t maxDropKeys = 256;
	}

	bool PartyLog::setMatch(const Common::HaveApi::PartyMatchInfo& info)
	{
		const bool roundOver = m_info.running != 0 && (info.running == 0 || info.round != m_info.round || info.modeId != m_info.modeId);
		m_info = info;
		if (roundOver)
		{
			m_drops.clear();
		}
		return roundOver;
	}

	void PartyLog::write(std::uint32_t room, std::string_view event) const
	{
		Common::HaveApi::writeModeLog(m_info.modeId, room, m_info.round, event);
	}

	bool PartyLog::allowDrop(const std::string& key, Clock::time_point now, std::uint32_t& suppressed)
	{
		auto found = m_drops.find(key);
		if (found == m_drops.end())
		{
			if (m_drops.size() >= maxDropKeys)
			{
				m_drops.clear();
			}
			found = m_drops.emplace(key, Details::LogThrottle{ dropInterval }).first;
		}
		if (!found->second.allow(now))
		{
			return false;
		}
		suppressed = found->second.takeSuppressed();
		return true;
	}
}
