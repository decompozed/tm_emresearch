/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#include "Rooms/PartyZone.h"
#include <algorithm>
#include <cmath>

namespace Cast::Domain
{
	void PartyZone::set(const Common::HaveApi::ZoneCommand& command)
	{
		m_active = true;
		m_shape = command.shape;
		m_x = command.x;
		m_y = command.y;
		m_radius = command.radius;
		m_keepOut = command.keepOut;
		m_zMin = command.zMin;
		m_zMax = command.zMax;
		m_teams.clear();
		addTeams(command);
	}

	void PartyZone::addMembers(const Common::HaveApi::ZoneCommand& command)
	{
		if (m_active)
		{
			addTeams(command);
		}
	}

	void PartyZone::addTeams(const Common::HaveApi::ZoneCommand& command)
	{
		const std::size_t count = (std::min)(static_cast<std::size_t>(command.count), command.members.size());
		for (const auto& member : std::span{ command.members }.first(count))
		{
			m_teams[member.sessionId] = member.team;
		}
	}

	void PartyZone::clear()
	{
		m_active = false;
		m_teams.clear();
		m_inside.clear();
	}

	float PartyZone::distanceToZone(float x, float y) const
	{
		return std::hypot(x - m_x, y - m_y);
	}

	bool PartyZone::contains(float x, float y, float z) const
	{
		const bool inPlane = m_shape == Common::HaveApi::ZoneShape::Square
			? std::abs(x - m_x) <= m_radius && std::abs(y - m_y) <= m_radius
			: distanceToZone(x, y) <= m_radius;
		return inPlane && (m_zMax <= m_zMin || (z >= m_zMin && z <= m_zMax));
	}

	Common::HaveApi::ZoneAnswer PartyZone::answer(std::span<const std::uint32_t> sessions) const
	{
		Common::HaveApi::ZoneAnswer answer{};
		answer.done = 1;
		answer.index = m_index;
		for (const std::uint32_t session : sessions.first((std::min)(sessions.size(), answer.sessions.size())))
		{
			answer.sessions[answer.count++] = session;
		}
		return answer;
	}

	Common::HaveApi::ZoneAnswer PartyZone::occupants(const PartyLava& positions, Clock::time_point now) const
	{
		std::vector<std::uint32_t> inside;
		if (m_active)
		{
			for (const auto& [sessionId, team] : m_teams)
			{
				const auto p = positions.position(sessionId);
				if (p && now - p->at <= positionMaxAge && contains(p->x, p->y, p->z) && !inSpawnGrace(sessionId, now))
				{
					inside.push_back(sessionId);
				}
			}
		}
		return answer(inside);
	}

	Common::HaveApi::ZoneAnswer PartyZone::resetOccupants(const PartyLava& positions, Clock::time_point now)
	{
		const Common::HaveApi::ZoneAnswer current = occupants(positions, now);
		m_inside.assign(current.sessions.begin(), current.sessions.begin() + current.count);
		return current;
	}

	bool PartyZone::updateOccupant(std::uint32_t sessionId, float x, float y, float z, Common::HaveApi::ZoneAnswer& changed, Clock::time_point now)
	{
		if (!m_active || !m_teams.contains(sessionId))
		{
			return false;
		}
		const bool inside = contains(x, y, z) && !inSpawnGrace(sessionId, now);
		const auto found = std::ranges::find(m_inside, sessionId);
		if (inside == (found != m_inside.end()))
		{
			return false;
		}
		if (inside)
		{
			m_inside.push_back(sessionId);
		}
		else
		{
			m_inside.erase(found);
		}
		changed = answer(m_inside);
		return true;
	}

	void PartyZone::onRespawn(std::uint32_t sessionId, float x, float y, Clock::time_point now)
	{
		std::erase_if(m_respawns, [&](const auto& respawn) { return now - respawn.second.at >= spawnGrace; });
		m_respawns[sessionId] = { x, y, now };
	}

	bool PartyZone::inSpawnGrace(std::uint32_t sessionId, Clock::time_point now) const
	{
		const auto found = m_respawns.find(sessionId);
		return m_active && found != m_respawns.end() && now - found->second.at < spawnGrace &&
			distanceToZone(found->second.x, found->second.y) < m_keepOut;
	}
}
