/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#ifndef MAIN_ROOMS_ROOM_CAST_SYNC_H
#define MAIN_ROOMS_ROOM_CAST_SYNC_H

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include "HaveApi/RoomSync.h"
#include "Utils/Constants.h"

namespace Main::Classes
{
	struct RoomCastSnapshot
	{
		std::uint16_t host{};
		std::uint8_t count{};
		std::uint16_t mode{};
		std::uint16_t map{};
		std::array<Common::HaveApi::RoomSyncEntry, Common::HaveApi::maxRoomSyncEntries> entries{};

		bool operator==(const RoomCastSnapshot& other) const noexcept
		{
			return host == other.host && count == other.count && mode == other.mode && map == other.map &&
				std::equal(entries.begin(), entries.begin() + count, other.entries.begin());
		}
	};

	template<class Players>
	[[nodiscard]] RoomCastSnapshot makeRoomCastSnapshot(const Players& players, const Players& observers, std::uint32_t mode, std::uint32_t map)
	{
		RoomCastSnapshot snapshot{};
		snapshot.mode = static_cast<std::uint16_t>(mode);
		snapshot.map = static_cast<std::uint16_t>(map);
		const auto validSession = [](std::uint32_t session) { return session != 0 && session <= Common::Constants::maxSessionsPerServer; };
		const auto add = [&](const Players& list, bool observer) {
			for (const auto& [info, weakSession] : list)
			{
				const auto session = weakSession.lock();
				if (snapshot.count == snapshot.entries.size() || !session || !session->is_open() || !validSession(info.uniqueId.session))
				{
					continue;
				}
				snapshot.entries[snapshot.count++] = Common::HaveApi::RoomSyncEntry{ static_cast<std::uint16_t>(info.uniqueId.session),
					static_cast<std::uint8_t>(info.team), static_cast<std::uint8_t>(observer) };
			}
		};
		add(players, false);
		add(observers, true);

		const auto begin = snapshot.entries.begin();
		std::sort(begin, begin + snapshot.count, [](const auto& a, const auto& b) { return a.session < b.session; });
		const auto end = std::unique(begin, begin + snapshot.count, [](const auto& a, const auto& b) { return a.session == b.session; });
		snapshot.count = static_cast<std::uint8_t>(end - begin);

		if (!players.empty())
		{
			const auto& [hostInfo, weakHost] = players.front();
			const auto hostSession = weakHost.lock();
			const bool listed = std::any_of(begin, end, [&](const auto& entry) { return entry.session == hostInfo.uniqueId.session && entry.isObserver == 0; });
			if (hostSession && hostSession->is_open() && listed)
			{
				snapshot.host = static_cast<std::uint16_t>(hostInfo.uniqueId.session);
			}
		}
		return snapshot;
	}

	class RoomCastSync
	{
	public:
		[[nodiscard]] static std::uint32_t nextSequence() noexcept { return s_sequence.fetch_add(1, std::memory_order_relaxed) + 1; }

		[[nodiscard]] static Common::HaveApi::RoomSyncMessage makeMessage(std::uint32_t roomNumber, const RoomCastSnapshot& snapshot, std::uint8_t flags = 0)
		{
			Common::HaveApi::RoomSyncMessage message{};
			message.header.roomNumber = roomNumber;
			message.header.sequence = nextSequence();
			message.header.hostSession = snapshot.host;
			message.header.count = snapshot.count;
			message.header.flags = flags;
			message.header.mode = snapshot.mode;
			message.header.map = snapshot.map;
			std::copy_n(snapshot.entries.begin(), snapshot.count, message.entries.begin());
			return message;
		}

		void activate() noexcept { m_active = true; }
		[[nodiscard]] bool active() const noexcept { return m_active; }

		void enter() noexcept { ++m_depth; }
		[[nodiscard]] bool leave() noexcept { return --m_depth == 0; }

		template<class Send>
		bool flush(std::uint32_t roomNumber, const RoomCastSnapshot& now, bool force, Send&& send)
		{
			if (!m_active || (!force && m_sent && now == m_last))
			{
				return false;
			}
			if (!send(makeMessage(roomNumber, now)))
			{
				return false;
			}
			m_last = now;
			m_sent = true;
			return true;
		}

	private:
		inline static std::atomic<std::uint32_t> s_sequence{};

		RoomCastSnapshot m_last{};
		bool m_active{};
		bool m_sent{};
		std::uint8_t m_depth{};
	};

	template<class Owner>
	class RoomCastSyncGuard
	{
	public:
		explicit RoomCastSyncGuard(Owner& owner) noexcept : m_owner{ owner } { m_owner.castSync().enter(); }

		~RoomCastSyncGuard()
		{
			if (m_owner.castSync().leave())
			{
				try
				{
					m_owner.flushCastSync();
				}
				catch (...)
				{
				}
			}
		}

		RoomCastSyncGuard(const RoomCastSyncGuard&) = delete;
		RoomCastSyncGuard& operator=(const RoomCastSyncGuard&) = delete;

	private:
		Owner& m_owner;
	};
}

#endif
