/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#include "Managers/RoomsManager.h"
#include <algorithm>
#include <iterator>
#include <string>
#include <Utils/Logger.h>

namespace Cast::Domain
{
	namespace
	{
		constexpr std::chrono::seconds pendingAttachLifetime{ 10 };
	}

	void RoomsManager::applySync(const Common::HaveApi::RoomSyncMessage& message)
	{
		const auto& header = message.header;
		if (header.flags & Common::HaveApi::RoomSyncFlags::FullSyncEnd)
		{
			closeRoomsOfOlderLinks();
			return;
		}
		if (header.count == 0)
		{
			if (const auto room = findRoom(header.roomNumber); room && room->membership().isNewer(header.sequence))
			{
				closeRoom(room);
			}
			return;
		}
		declareRoom(message);
	}

	void RoomsManager::closeRoomsOfOlderLinks()
	{
		std::vector<std::shared_ptr<Room>> stale;
		std::ranges::copy_if(m_rooms, std::back_inserter(stale), [this](const std::shared_ptr<Room>& room) { return room->syncEpoch() != m_linkEpoch; });
		for (const auto& room : stale)
		{
			closeRoom(room);
		}
	}

	void RoomsManager::declareRoom(const Common::HaveApi::RoomSyncMessage& message)
	{
		const auto& header = message.header;
		auto room = findRoom(header.roomNumber);
		if (!room)
		{
			room = std::make_shared<Room>(header.roomNumber);
			m_rooms.push_back(room);
			::Utils::Logger::log("[RoomsManager] room " + std::to_string(header.roomNumber) + " declared by the MainServer, " +
				std::to_string(m_rooms.size()) + " rooms", ::Utils::LogType::Info);
		}
		if (!room->membership().apply(message))
		{
			::Utils::Logger::log("[RoomsManager] room " + std::to_string(header.roomNumber) + ": sync " + std::to_string(header.sequence) +
				" older than " + std::to_string(room->membership().lastSequence()) + ", ignored", ::Utils::LogType::Warning);
			return;
		}
		room->setSyncEpoch(m_linkEpoch);
		room->setMode(header.mode);
		room->setMap(header.map);
		room->detachUndeclared([this](std::uint64_t sessionId) {
			if (sessionId < m_roomBySession.size())
			{
				m_roomBySession[sessionId] = nullptr;
			}
		});
		retryPendingAttaches(*room);
	}

	void RoomsManager::retryPendingAttaches(const Room& room)
	{
		const auto now = std::chrono::steady_clock::now();
		for (const auto& member : room.membership().members())
		{
			auto& pending = m_pendingAttach[member.session];
			const auto session = pending.session.lock();
			if (!session)
			{
				continue;
			}
			if (now - pending.at > pendingAttachLifetime || session->getId() != member.session)
			{
				pending = {};
				continue;
			}
			tryAttach(session, pending.order, pending.headerSession, true);
		}
	}

	void RoomsManager::onLinkUp()
	{
		++m_linkEpoch;
		m_linkUp = true;
		for (const auto& room : m_rooms)
		{
			room->membership().forgetSequence();
			room->membership().invalidateHost();
		}
	}

	void RoomsManager::onLinkDown()
	{
		m_linkUp = false;
		for (const auto& room : m_rooms)
		{
			room->membership().invalidateHost();
		}
	}

	void RoomsManager::attach(const SessionPtr& session, std::uint16_t order, std::uint32_t headerSession)
	{
		tryAttach(session, order, headerSession, false);
	}

	void RoomsManager::expirePendingAttaches(std::chrono::steady_clock::time_point now, const std::function<void(Network::Session&)>& deny)
	{
		for (std::size_t sessionId = 0; sessionId < m_pendingAttach.size(); ++sessionId)
		{
			auto& pending = m_pendingAttach[sessionId];
			const auto session = pending.session.lock();
			if (!session)
			{
				pending = {};
				continue;
			}
			if (now - pending.at <= pendingAttachLifetime)
			{
				continue;
			}
			const bool deniable = pending.notMember && m_linkUp && !findRoomOfMember(static_cast<std::uint32_t>(sessionId));
			pending = {};
			if (deniable)
			{
				::Utils::Logger::log("[RoomsManager] session " + std::to_string(sessionId) + " is in no room of the MainServer: 278 deny",
					::Utils::LogType::Warning, "Cast::RoomsManager");
				deny(*session);
			}
		}
	}

	void RoomsManager::tryAttach(const SessionPtr& session, std::uint16_t order, std::uint32_t headerSession, bool retry)
	{
		const auto sessionId = static_cast<std::uint32_t>(session->getId());
		if (sessionId >= m_roomBySession.size())
		{
			return;
		}
		const auto current = m_roomBySession[sessionId];
		const auto room = current && current->isAuthorizedMember(sessionId) ? current : findRoomOfMember(sessionId);
		const AttachVerdict verdict = room ? room->membership().attachVerdict(sessionId, order, headerSession, current == room) : AttachVerdict::NotMember;

		switch (verdict)
		{
		case AttachVerdict::Attach:
			if (current && current != room)
			{
				removePlayer(sessionId);
			}
			room->addPlayer(session);
			m_roomBySession[sessionId] = room;
			m_pendingAttach[sessionId] = {};
			return;
		case AttachVerdict::AlreadyAttached:
			m_pendingAttach[sessionId] = {};
			return;
		case AttachVerdict::NotMember:
		case AttachVerdict::HostUnknown:
			deferAttach(session, order, headerSession, verdict, room.get(), retry);
			if (room)
			{
				requestSync(*room);
			}
			return;
		case AttachVerdict::NotHost:
		case AttachVerdict::WrongHost:
			m_pendingAttach[sessionId] = {};
			::Utils::Logger::log("[AntiCheat] order " + std::to_string(order) + " from session " + std::to_string(sessionId) + " (header " +
				std::to_string(headerSession) + ") refused in room " + std::to_string(room->getRoomNumber()) + ": " + toString(verdict),
				::Utils::LogType::Warning, "Cast::RoomsManager");
			return;
		}
	}

	void RoomsManager::deferAttach(const SessionPtr& session, std::uint16_t order, std::uint32_t headerSession, AttachVerdict verdict, const Room* room,
		bool retry)
	{
		if (retry)
		{
			return;
		}
		const auto sessionId = static_cast<std::uint32_t>(session->getId());
		m_pendingAttach[sessionId] = PendingAttach{ session, order, headerSession, std::chrono::steady_clock::now(), verdict == AttachVerdict::NotMember && !room };
		::Utils::Logger::log("[AntiCheat] order " + std::to_string(order) + " from session " + std::to_string(sessionId) + " (header " +
			std::to_string(headerSession) + ") not authorized yet: " + toString(verdict) + "; retried for " +
			std::to_string(pendingAttachLifetime.count()) + " s if the MainServer declares it", ::Utils::LogType::Warning, "Cast::RoomsManager");
	}

	std::shared_ptr<Room> RoomsManager::findRoom(std::uint32_t roomNumber) const
	{
		const auto found = std::ranges::find_if(m_rooms, [roomNumber](const std::shared_ptr<Room>& room) { return room->getRoomNumber() == roomNumber; });
		return found != m_rooms.end() ? *found : nullptr;
	}

	std::shared_ptr<Room> RoomsManager::findRoomOfMember(std::uint32_t sessionId) const
	{
		const auto found = std::ranges::find_if(m_rooms, [sessionId](const std::shared_ptr<Room>& room) { return room->isAuthorizedMember(sessionId); });
		return found != m_rooms.end() ? *found : nullptr;
	}

	void RoomsManager::requestSync(Room& room)
	{
		if (m_syncRequester && room.takeSyncRequestSlot(std::chrono::steady_clock::now()))
		{
			m_syncRequester(room.getRoomNumber());
		}
	}

	void RoomsManager::requestSyncIfHostUnknown(Room& room)
	{
		if (!room.host())
		{
			requestSync(room);
		}
	}

	void RoomsManager::closeRoom(const std::shared_ptr<Room>& room)
	{
		room->detachAll();
		std::ranges::replace(m_roomBySession, room, std::shared_ptr<Room>{});
		std::erase(m_rooms, room);
		::Utils::Logger::log("[RoomsManager] room " + std::to_string(room->getRoomNumber()) + " closed, " + std::to_string(m_rooms.size()) + " rooms",
			::Utils::LogType::Info);
	}

	std::shared_ptr<Room> RoomsManager::getRoom(std::uint64_t sessionId)
	{
		if (sessionId >= m_roomBySession.size())
		{
			return nullptr;
		}
		const auto& room = m_roomBySession[sessionId];
		if (room)
		{
			requestSyncIfHostUnknown(*room);
		}
		return room;
	}

	void RoomsManager::removePlayer(std::uint64_t sessionId)
	{
		if (sessionId >= m_roomBySession.size())
		{
			return;
		}
		m_pendingAttach[sessionId] = {};
		const auto room = m_roomBySession[sessionId];
		if (!room)
		{
			return;
		}
		room->removePlayer(sessionId);
		m_roomBySession[sessionId] = nullptr;
		if (room->isHost(static_cast<std::uint32_t>(sessionId)))
		{
			room->membership().invalidateHost();
		}
	}
}
