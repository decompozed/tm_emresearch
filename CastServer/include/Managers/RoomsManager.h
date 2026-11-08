#ifndef CAST_MANAGERS_ROOMS_MANAGER_H
#define CAST_MANAGERS_ROOMS_MANAGER_H

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>
#include <Utils/Constants.h>
#include "HaveApi/RoomSync.h"
#include "Rooms/Room.h"

namespace Cast::Domain
{
	class RoomsManager
	{
	public:
		using SessionPtr = std::shared_ptr<Network::Session>;

		void setSyncRequester(std::function<void(std::uint32_t)> requester) { m_syncRequester = std::move(requester); }
		[[nodiscard]] const std::vector<std::shared_ptr<Room>>& rooms() const noexcept { return m_rooms; }

		void applySync(const Common::HaveApi::RoomSyncMessage& message);
		void onLinkUp();
		void onLinkDown();

		void attach(const SessionPtr& session, std::uint16_t order, std::uint32_t headerSession);
		void expirePendingAttaches(std::chrono::steady_clock::time_point now, const std::function<void(Network::Session&)>& deny);
		[[nodiscard]] std::shared_ptr<Room> getRoom(std::uint64_t sessionId);
		void removePlayer(std::uint64_t sessionId);

	private:
		struct PendingAttach
		{
			std::weak_ptr<Network::Session> session;
			std::uint16_t order{};
			std::uint32_t headerSession{};
			std::chrono::steady_clock::time_point at{};
			bool notMember{};
		};

		void closeRoomsOfOlderLinks();
		void declareRoom(const Common::HaveApi::RoomSyncMessage& message);
		void retryPendingAttaches(const Room& room);
		void tryAttach(const SessionPtr& session, std::uint16_t order, std::uint32_t headerSession, bool retry);
		void deferAttach(const SessionPtr& session, std::uint16_t order, std::uint32_t headerSession, AttachVerdict verdict, const Room* room, bool retry);
		[[nodiscard]] std::shared_ptr<Room> findRoom(std::uint32_t roomNumber) const;
		[[nodiscard]] std::shared_ptr<Room> findRoomOfMember(std::uint32_t sessionId) const;
		void requestSync(Room& room);
		void requestSyncIfHostUnknown(Room& room);
		void closeRoom(const std::shared_ptr<Room>& room);

		std::vector<std::shared_ptr<Room>> m_rooms;
		std::array<std::shared_ptr<Room>, Common::Constants::maxSessionsPerServer + 1> m_roomBySession{};
		std::array<PendingAttach, Common::Constants::maxSessionsPerServer + 1> m_pendingAttach{};
		std::function<void(std::uint32_t)> m_syncRequester;
		std::uint32_t m_linkEpoch{};
		bool m_linkUp{};
	};
}

#endif
