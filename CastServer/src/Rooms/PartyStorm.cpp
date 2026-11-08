/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#include "Rooms/PartyStorm.h"
#include <algorithm>

namespace Cast::Domain
{
	void PartyStorm::set(const Common::HaveApi::StormCommand& command, Clock::time_point now)
	{
		m_count = (std::min)(static_cast<std::size_t>(command.boxCount), m_boxes.size());
		std::copy_n(command.boxes.begin(), m_count, m_boxes.begin());
		m_since = now;
		m_active = true;
		const std::size_t members = (std::min)(static_cast<std::size_t>(command.memberCount), command.members.size());
		m_members.assign(command.members.begin(), command.members.begin() + static_cast<std::ptrdiff_t>(members));
		std::erase_if(m_outside, [&](std::uint32_t session) { return std::ranges::find(m_members, session) == m_members.end(); });
	}

	void PartyStorm::clear()
	{
		m_active = false;
		m_count = 0;
		m_members.clear();
		m_outside.clear();
		m_staleReported.clear();
	}

	float PartyStorm::secondsSince(Clock::time_point now) const
	{
		return std::chrono::duration<float>(now - m_since).count();
	}

	bool PartyStorm::contains(float x, float y, Clock::time_point now) const
	{
		return Common::HaveApi::stormContains(boxes(), secondsSince(now), x, y);
	}

	Common::HaveApi::StormAnswer PartyStorm::outside() const
	{
		Common::HaveApi::StormAnswer answer{};
		answer.done = 1;
		for (const std::uint32_t session : m_outside)
		{
			if (answer.count < answer.sessions.size())
			{
				answer.sessions[answer.count++] = session;
			}
		}
		return answer;
	}

	Common::HaveApi::StormAnswer PartyStorm::evaluate(const PartyLava& positions, Clock::time_point now)
	{
		if (!m_active)
		{
			return outside();
		}
		m_outside.clear();
		for (const std::uint32_t session : m_members)
		{
			const auto p = positions.position(session);
			if (p && !contains(p->x, p->y, now))
			{
				m_outside.push_back(session);
			}
		}
		return outside();
	}

	bool PartyStorm::update(std::uint32_t sessionId, float x, float y, Common::HaveApi::StormAnswer& changed, Clock::time_point now)
	{
		if (!m_active || std::ranges::find(m_members, sessionId) == m_members.end())
		{
			return false;
		}
		const bool out = !contains(x, y, now);
		const auto found = std::ranges::find(m_outside, sessionId);
		if (out == (found != m_outside.end()))
		{
			return false;
		}
		if (out)
		{
			m_outside.push_back(sessionId);
		}
		else
		{
			m_outside.erase(found);
		}
		changed = outside();
		return true;
	}

	std::vector<std::uint32_t> PartyStorm::takeStale(const PartyLava& positions, Clock::time_point now)
	{
		std::vector<std::uint32_t> stale;
		if (!m_active)
		{
			return stale;
		}
		for (const std::uint32_t session : m_members)
		{
			const auto p = positions.position(session);
			if ((!p || now - p->at > staleAfter) && m_staleReported.insert(session).second)
			{
				stale.push_back(session);
			}
		}
		return stale;
	}
}
