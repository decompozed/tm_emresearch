#ifndef CAST_ROOMS_PLAYER_ROSTER_H
#define CAST_ROOMS_PLAYER_ROSTER_H

#include <cstdint>
#include <memory>
#include <vector>
#include "Network/Sessions/CastSession.h"

namespace Cast::Domain
{
	class PlayerRoster
	{
	public:
		using SessionPtr = std::shared_ptr<Network::Session>;

		void add(const SessionPtr& session);
		void remove(std::uint64_t sessionId);
		[[nodiscard]] SessionPtr find(std::uint64_t sessionId) const;

		template<class Fn>
		void forEachLive(Fn&& fn) const
		{
			for (const auto& player : m_players)
			{
				if (const auto session = player.lock())
				{
					fn(session);
				}
			}
		}

		template<class Pred, class Fn>
		void removeIf(Pred&& pred, Fn&& onRemoved)
		{
			std::erase_if(m_players, [&](const std::weak_ptr<Network::Session>& player) {
				const auto session = player.lock();
				if (!session)
				{
					return true;
				}
				if (!pred(session))
				{
					return false;
				}
				onRemoved(session);
				return true;
			});
		}

	private:
		std::vector<std::weak_ptr<Network::Session>> m_players;
	};
}

#endif
