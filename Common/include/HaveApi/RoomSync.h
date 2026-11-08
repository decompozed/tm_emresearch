/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#ifndef COMMON_HAVE_API_ROOM_SYNC_H
#define COMMON_HAVE_API_ROOM_SYNC_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include "Utils/Constants.h"
#include "Utils/Macros.h"

namespace Common::HaveApi
{
	inline constexpr std::size_t maxRoomSyncEntries = Common::Constants::maxRoomPlayersNoObs + Common::Constants::maxObserverPlayers;
	inline constexpr std::uint16_t maxRoomSyncMode = 0x1F;

	namespace RoomSyncFlags
	{
		inline constexpr std::uint8_t FullSyncEnd = 1;
	}

PACK_PUSH(1)
	struct RoomSyncEntry
	{
		std::uint16_t session{};
		std::uint8_t team{};
		std::uint8_t isObserver{};

		bool operator==(const RoomSyncEntry&) const = default;
	};

	struct RoomSyncHeader
	{
		std::uint32_t roomNumber{};
		std::uint32_t sequence{};
		std::uint16_t hostSession{};
		std::uint8_t count{};
		std::uint8_t flags{};
		std::uint16_t mode{};
		std::uint16_t map{};
	};

	struct RoomSyncMessage
	{
		RoomSyncHeader header{};
		std::array<RoomSyncEntry, maxRoomSyncEntries> entries{};

		[[nodiscard]] std::size_t byteSize() const noexcept { return sizeof(RoomSyncHeader) + header.count * sizeof(RoomSyncEntry); }
	};
PACK_POP()

	static_assert(sizeof(RoomSyncEntry) == 4);
	static_assert(sizeof(RoomSyncHeader) == 16);
	static_assert(sizeof(RoomSyncMessage) == 16 + 4 * maxRoomSyncEntries);

	[[nodiscard]] inline std::optional<RoomSyncMessage> decodeRoomSync(const std::uint8_t* data, std::size_t size)
	{
		if (data == nullptr || size < sizeof(RoomSyncHeader))
		{
			return std::nullopt;
		}
		RoomSyncMessage message{};
		std::memcpy(&message.header, data, sizeof(RoomSyncHeader));
		const auto& header = message.header;
		if (header.count > maxRoomSyncEntries || size != message.byteSize() || (header.flags & ~RoomSyncFlags::FullSyncEnd) != 0)
		{
			return std::nullopt;
		}
		if (header.flags & RoomSyncFlags::FullSyncEnd)
		{
			const bool empty = header.roomNumber == 0 && header.count == 0 && header.hostSession == 0 && header.mode == 0 && header.map == 0;
			return empty ? std::optional{ message } : std::nullopt;
		}
		if (header.roomNumber == 0 || header.mode > maxRoomSyncMode)
		{
			return std::nullopt;
		}
		std::memcpy(message.entries.data(), data + sizeof(RoomSyncHeader), header.count * sizeof(RoomSyncEntry));

		bool hostListed = header.hostSession == 0;
		for (std::size_t i = 0; i < header.count; ++i)
		{
			const auto& entry = message.entries[i];
			if (entry.session == 0 || entry.session > Common::Constants::maxSessionsPerServer || entry.isObserver > 1)
			{
				return std::nullopt;
			}
			for (std::size_t j = 0; j < i; ++j)
			{
				if (message.entries[j].session == entry.session)
				{
					return std::nullopt;
				}
			}
			hostListed = hostListed || (entry.session == header.hostSession && entry.isObserver == 0);
		}
		return hostListed ? std::optional{ message } : std::nullopt;
	}
}

#endif
