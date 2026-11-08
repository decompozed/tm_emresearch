/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#include "Rooms/PartyLava.h"
#include <algorithm>

namespace Cast::Domain
{
	void PartyLava::setPosition(std::uint32_t sessionId, float x, float y, float z, Clock::time_point now)
	{
		m_positions[sessionId] = { x, y, z, now };
	}

	std::optional<PartyLava::Position> PartyLava::position(std::uint32_t sessionId) const
	{
		const auto found = m_positions.find(sessionId);
		return found != m_positions.end() ? std::optional{ found->second } : std::nullopt;
	}

	void PartyLava::setDisarmed(std::uint32_t sessionId, bool on)
	{
		if (on)
		{
			m_disarmed.insert(sessionId);
		}
		else
		{
			m_disarmed.erase(sessionId);
		}
	}

	void PartyLava::Box::add(float x, float y, float z)
	{
		if (!any)
		{
			*this = { x, y, z, x, y, true };
			return;
		}
		minX = (std::min)(minX, x);
		maxX = (std::max)(maxX, x);
		minY = (std::min)(minY, y);
		maxY = (std::max)(maxY, y);
		minZ = (std::min)(minZ, z);
	}

	Common::HaveApi::LavaState PartyLava::start(const Common::HaveApi::LavaCommand& command, std::uint32_t map, Clock::time_point now)
	{
		m_killed.clear();
		m_base = {};
		m_base.wallsOn = command.wallsOn;
		m_baseFloorSpeed = command.floorSpeed;
		m_lastStart = {};

		Box fresh;
		Box known;
		for (const auto& [sessionId, p] : m_positions)
		{
			const bool isFresh = now - p.at <= positionMaxAge;
			if (isFresh)
			{
				fresh.add(p.x, p.y, p.z);
				++m_lastStart.freshPositions;
			}
			if (isFresh || p.at >= m_stoppedAt)
			{
				known.add(p.x, p.y, p.z);
				++m_lastStart.knownPositions;
			}
		}
		Box spawns;
		for (const Common::HaveApi::PartySpawn& spawn : Common::HaveApi::findPartySpawns(map))
		{
			spawns.add(spawn.x, spawn.y, spawn.z);
		}
		const auto bounds = Common::HaveApi::findMapBounds(map);
		if (fresh.any) m_lastStart.lowestFreshZ = fresh.minZ;
		if (known.any) m_lastStart.lowestKnownZ = known.minZ;
		if (spawns.any) m_lastStart.lowestSpawnZ = spawns.minZ;
		if (bounds) m_lastStart.mapMinZ = bounds->minZ;

		float floorZ = 0.0f;
		if (fresh.any)
		{
			floorZ = fresh.minZ - startMargin;
		}
		else if (spawns.any || bounds)
		{
			m_base.fromMap = 1;
			float ground = spawns.any ? spawns.minZ : bounds->minZ;
			if (spawns.any && bounds)
			{
				ground = (std::min)(ground, (std::max)(bounds->minZ, spawns.minZ - maxDepthBelowSpawns));
			}
			floorZ = ground - startMargin;
		}
		else
		{
			m_active = false;
			m_base = {};
			return m_base;
		}
		if (known.any)
		{
			floorZ = (std::min)(floorZ, known.minZ - startMargin);
		}
		if (spawns.any)
		{
			floorZ = (std::min)(floorZ, spawns.minZ - startMargin);
		}

		if (bounds)
		{
			place({ bounds->minX, bounds->minY, bounds->minZ, bounds->maxX, bounds->maxY, true }, floorZ, mapWallMargin);
		}
		else
		{
			place(fresh.any ? fresh : known.any ? known : spawns, floorZ, wallMargin);
		}
		if (known.any)
		{
			keepOutside(known, mapWallMargin);
		}
		beginMoving(command, now);
		return m_base;
	}

	void PartyLava::place(const Box& box, float floorZ, float margin)
	{
		m_base.active = 1;
		m_base.floorZ = floorZ;
		m_base.wallX1 = box.minX - margin;
		m_base.wallX2 = box.maxX + margin;
		m_base.wallY1 = box.minY - margin;
		m_base.wallY2 = box.maxY + margin;
	}

	void PartyLava::keepOutside(const Box& box, float margin)
	{
		m_base.wallX1 = (std::min)(m_base.wallX1, box.minX - margin);
		m_base.wallX2 = (std::max)(m_base.wallX2, box.maxX + margin);
		m_base.wallY1 = (std::min)(m_base.wallY1, box.minY - margin);
		m_base.wallY2 = (std::max)(m_base.wallY2, box.maxY + margin);
	}

	void PartyLava::beginMoving(const Common::HaveApi::LavaCommand& command, Clock::time_point now)
	{
		const float seconds = (std::max)(command.wallCloseSeconds, 1.0f);
		const float centreX = (m_base.wallX1 + m_base.wallX2) / 2.0f;
		const float centreY = (m_base.wallY1 + m_base.wallY2) / 2.0f;
		m_baseWallSpeeds = { (centreX - m_base.wallX1) / seconds, (centreX - m_base.wallX2) / seconds,
			(centreY - m_base.wallY1) / seconds, (centreY - m_base.wallY2) / seconds };
		applySpeeds(command.speedFactor);
		m_since = now;
		m_active = true;
	}

	void PartyLava::applySpeeds(float factor)
	{
		m_base.floorSpeed = m_baseFloorSpeed * factor;
		m_base.wallX1Speed = m_baseWallSpeeds[0] * factor;
		m_base.wallX2Speed = m_baseWallSpeeds[1] * factor;
		m_base.wallY1Speed = m_baseWallSpeeds[2] * factor;
		m_base.wallY2Speed = m_baseWallSpeeds[3] * factor;
	}

	Common::HaveApi::LavaState PartyLava::state(Clock::time_point now) const
	{
		if (!m_active)
		{
			return m_base;
		}
		const float t = std::chrono::duration<float>(now - m_since).count();
		Common::HaveApi::LavaState s = m_base;
		s.floorZ += s.floorSpeed * t;
		s.wallX1 += s.wallX1Speed * t;
		s.wallX2 += s.wallX2Speed * t;
		s.wallY1 += s.wallY1Speed * t;
		s.wallY2 += s.wallY2Speed * t;
		return s;
	}

	Common::HaveApi::LavaState PartyLava::setSpeed(const Common::HaveApi::LavaCommand& command, Clock::time_point now)
	{
		if (!m_active)
		{
			return m_base;
		}
		m_base = state(now);
		m_since = now;
		applySpeeds(command.speedFactor);
		return m_base;
	}

	Common::HaveApi::LavaState PartyLava::stop(Clock::time_point now)
	{
		m_stoppedAt = now;
		m_active = false;
		m_killed.clear();
		m_base.active = 0;
		return m_base;
	}

	bool PartyLava::reaches(std::uint32_t sessionId, float x, float y, float z, const char*& reason, Clock::time_point now)
	{
		if (!m_active || m_killed.contains(sessionId))
		{
			return false;
		}
		const Common::HaveApi::LavaState s = state(now);
		reason = nullptr;
		if (z < s.floorZ)
			reason = "floor";
		else if (s.wallsOn && x < s.wallX1)
			reason = "wall x1";
		else if (s.wallsOn && x > s.wallX2)
			reason = "wall x2";
		else if (s.wallsOn && y < s.wallY1)
			reason = "wall y1";
		else if (s.wallsOn && y > s.wallY2)
			reason = "wall y2";
		if (reason == nullptr)
		{
			return false;
		}
		m_killed.insert(sessionId);
		return true;
	}
}
