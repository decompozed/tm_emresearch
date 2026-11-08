#ifndef CAST_NETWORK_SESSIONS_MANAGER_H
#define CAST_NETWORK_SESSIONS_MANAGER_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include "Network/Sessions/CastSession.h"

namespace Cast::Domain { class RoomsManager; }

namespace Cast::Network
{
	class SessionsManager
	{
	public:
		explicit SessionsManager(Domain::RoomsManager& rooms) noexcept : m_rooms(rooms) {}

		void addSession(const std::shared_ptr<Session>& session, std::size_t sessionId);
		void removeSession(std::size_t sessionId);
		[[nodiscard]] std::shared_ptr<Session> getSession(std::size_t sessionId) const;

	private:
		Domain::RoomsManager& m_rooms;
		std::unordered_map<std::uint64_t, std::shared_ptr<Session>> m_sessions;
	};
}

#endif
