/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#include "Rooms/PlayerRoster.h"
#include <algorithm>
#include <Utils/Logger.h>

namespace Cast::Domain
{
	void PlayerRoster::add(const SessionPtr& session)
	{
		if (!session)
		{
			return;
		}
		const auto replaced = std::erase_if(m_players, [&](const std::weak_ptr<Network::Session>& player) {
			const auto existing = player.lock();
			return !existing || existing->getId() == session->getId();
		});
		if (replaced != 0)
		{
			::Utils::Logger::log("Duplicate sessionID found and replaced in room join", ::Utils::LogType::Warning, "Room::addPlayer");
		}
		m_players.push_back(session);
	}

	void PlayerRoster::remove(std::uint64_t sessionId)
	{
		const auto found = std::ranges::find_if(m_players, [sessionId](const std::weak_ptr<Network::Session>& player) {
			const auto session = player.lock();
			return session && session->getId() == sessionId;
		});
		if (found != m_players.end())
		{
			m_players.erase(found);
		}
	}

	PlayerRoster::SessionPtr PlayerRoster::find(std::uint64_t sessionId) const
	{
		for (const auto& player : m_players)
		{
			if (auto session = player.lock(); session && session->getId() == sessionId)
			{
				return session;
			}
		}
		return nullptr;
	}
}
