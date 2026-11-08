/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#include "Network/SessionsManager.h"
#include "Managers/RoomsManager.h"

namespace Cast::Network
{
	void SessionsManager::addSession(const std::shared_ptr<Session>& session, std::size_t sessionId)
	{
		if (m_sessions.try_emplace(sessionId, session).second)
		{
			session->setSessionId(sessionId);
		}
	}

	void SessionsManager::removeSession(std::size_t sessionId)
	{
		const auto found = m_sessions.find(sessionId);
		if (found == m_sessions.end())
		{
			return;
		}
		found->second->setInMatch(false);
		m_sessions.erase(found);
		m_rooms.removePlayer(sessionId);
	}

	std::shared_ptr<Session> SessionsManager::getSession(std::size_t sessionId) const
	{
		const auto found = m_sessions.find(sessionId);
		return found != m_sessions.end() ? found->second : nullptr;
	}
}
