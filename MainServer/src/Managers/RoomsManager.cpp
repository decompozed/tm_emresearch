/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */


#include <functional>
#include <limits>
#include "Rooms/Room.h"
#include "Managers/RoomsManager.h"
#include "Structures/Room/RoomsList.h"
#include "Rooms/RoomNumberManager.h"
#include "Utils/Logger.h"
#include "Detail/IpcUtils.h"

namespace Main
{
	namespace Classes
	{
		void RoomsManager::addRoom(Main::Classes::Room&& room)
		{
			std::uint16_t roomNum = room.getRoomNumber();
			bool isClanRoom = room.isClanRoom();
			if (m_roomByNumber.contains(roomNum))
			{
				bool suspicious = false;
				::Utils::Logger::log("[RoomsManager] room " + std::to_string(roomNum) + " created over an existing room: " +
					m_roomByNumber[roomNum].diagnosticsLine(suspicious), ::Utils::LogType::Warning);
			}
			m_roomByNumber[roomNum] = std::move(room);
			m_roomByNumber[roomNum].activateCastSync();
			::Utils::Logger::log("[RoomsManager] room " + std::to_string(roomNum) + (isClanRoom ? " (clan)" : "") + " created, " +
				std::to_string(m_roomByNumber.size()) + " rooms", ::Utils::LogType::Info);
		}

		void RoomsManager::removeRoom(std::uint16_t roomNum, std::uint32_t extra)
		{
			auto it = m_roomByNumber.find(roomNum);
			if (it == m_roomByNumber.end())
			{
				return;
			}

			bool isClanRoom = it->second.isClanRoom();

			if (isClanRoom)
			{
				Main::Classes::RoomNumberGenerator<Main::Enums::RoomType::Clan>::getInstance().release(roomNum);
			}
			else
			{
				Main::Classes::RoomNumberGenerator<Main::Enums::RoomType::Room>::getInstance().release(roomNum);
			}

			bool suspicious = false;
			::Utils::Logger::log("[RoomsManager] removing " + it->second.diagnosticsLine(suspicious) + " (extra " + std::to_string(extra) + ")",
				::Utils::LogType::Info);
			it->second.removeAllPlayers(extra);
			m_roomByNumber.erase(it);
		}

		void RoomsManager::removeAbandonedRooms()
		{
			std::vector<std::uint16_t> abandoned;
			for (const auto& [number, room] : m_roomByNumber)
			{
				if (!room.hasLivePlayer())
				{
					abandoned.push_back(number);
				}
			}
			for (const auto number : abandoned)
			{
				removeRoom(number);
			}
		}

		void RoomsManager::logDiagnostics(bool everyRoom)
		{
			std::size_t ghosts = 0;
			for (const auto& [number, room] : m_roomByNumber)
			{
				bool suspicious = false;
				const std::string line = room.diagnosticsLine(suspicious);
				if (suspicious)
				{
					++ghosts;
					::Utils::Logger::log("[RoomDiagnostics] SUSPICIOUS " + line, ::Utils::LogType::Warning);
				}
				else if (everyRoom)
				{
					::Utils::Logger::log("[RoomDiagnostics] " + line, ::Utils::LogType::Info);
				}
			}
			if (everyRoom || ghosts > 0)
			{
				::Utils::Logger::log("[RoomDiagnostics] " + std::to_string(m_roomByNumber.size()) + " rooms, " + std::to_string(ghosts) +
					" suspicious", ghosts > 0 ? ::Utils::LogType::Warning : ::Utils::LogType::Info);
			}
		}

		void RoomsManager::checkCastSync()
		{
			for (auto& [number, room] : m_roomByNumber)
			{
				room.flushCastSync();
			}
		}

		void RoomsManager::resyncCast()
		{
			for (auto& [number, room] : m_roomByNumber)
			{
				room.flushCastSync(true);
			}
			Main::Ipc::M2C_sendRoomSync(RoomCastSync::makeMessage(0, RoomCastSnapshot{}, Common::HaveApi::RoomSyncFlags::FullSyncEnd));
		}

		void RoomsManager::onCastSyncRequest(std::uint32_t roomNumber)
		{
			if (roomNumber == 0)
			{
				return;
			}
			if (auto* room = roomNumber <= std::numeric_limits<std::uint16_t>::max() ? getRoomByNumber(static_cast<std::uint16_t>(roomNumber)) : nullptr)
			{
				room->flushCastSync(true);
				return;
			}
			Main::Ipc::M2C_sendRoomSync(RoomCastSync::makeMessage(roomNumber, RoomCastSnapshot{}));
		}

		std::size_t RoomsManager::getTotalRooms() const
		{
			return m_roomByNumber.size();
		}

		std::vector<Main::Structures::SingleRoom> RoomsManager::getRoomsList()
		{
			std::vector<Main::Structures::SingleRoom> roomsList;
			roomsList.reserve(m_roomByNumber.size());

			for (auto it = m_roomByNumber.begin(); it != m_roomByNumber.end(); )
			{
				auto& room = it->second;

				if (room.getPlayersSize() == 0)
				{
					room.removeAllPlayers();
					it = m_roomByNumber.erase(it); 
					continue;
				}

				if (room.getRoomNumber() >= Common::Constants::clanRoomNumberStart)
				{
					++it;
					continue;
				}

				roomsList.emplace_back(room.getRoomInfo());
				++it;
			}

			return roomsList;
		}

		std::vector<Main::Structures::SingleRoom> RoomsManager::getClanRoomsList()
		{
			std::vector<Main::Structures::SingleRoom> clanRoomsList;
			clanRoomsList.reserve(m_roomByNumber.size());

			for (auto it = m_roomByNumber.begin(); it != m_roomByNumber.end(); )
			{
				auto& room = it->second;

				if (room.getPlayersSize() == 0)
				{
					room.removeAllPlayers();
					it = m_roomByNumber.erase(it);
					continue;
				}

				if (room.getRoomNumber() < Common::Constants::clanRoomNumberStart)
				{
					++it;
					continue;
				}

				clanRoomsList.emplace_back(room.getRoomInfo());
				++it;
			}

			return clanRoomsList;
		}

		Main::Classes::Room* RoomsManager::getRoomByNumber(std::uint16_t roomNumber)
		{
			auto it = m_roomByNumber.find(roomNumber);
			if (it != m_roomByNumber.end())
			{
				return &it->second; 
			}
			return nullptr;
		}
	};
}
