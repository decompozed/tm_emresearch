#ifndef CAST_NETWORK_SESSIONS_CAST_SESSION_H
#define CAST_NETWORK_SESSIONS_CAST_SESSION_H

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>
#include <asio.hpp>
#include "Connection/ConnectionRules.h"
#include "Detail/LogThrottle.h"
#include "Enums/GameEnums.h"
#include "Network/Session.h"
#include "Structures/Combat/CombatPackets.h"
#include "Structures/Connection/ConnectionPackets.h"

namespace Cast::Network
{
	class Session final : public Common::Network::Session
	{
	public:
		Session(asio::ip::tcp::socket&& socket, std::function<void(std::size_t)> onClose);

		void onPacket(std::vector<std::uint8_t>& data) override;
		void logDropped(const Common::Network::UnecryptedPacket& packet, std::string_view reason);
		void writeCommand(const Structures::CommandFields& command);

		[[nodiscard]] Connection::Stage stage() const noexcept { return m_stage; }
		void setStage(Connection::Stage stage) noexcept { m_stage = stage; }
		[[nodiscard]] bool isAuthenticated() const noexcept { return m_stage == Connection::Stage::Authenticated; }

		[[nodiscard]] bool isInMatch() const noexcept { return m_inMatch; }
		void setInMatch(bool inMatch) noexcept { m_inMatch = inMatch; }
		[[nodiscard]] bool isDead() const noexcept { return m_dead; }
		[[nodiscard]] std::chrono::steady_clock::time_point deadSince() const noexcept { return m_deadSince; }
		void setDead(bool dead) noexcept
		{
			if (dead && !m_dead)
			{
				m_deadSince = std::chrono::steady_clock::now();
			}
			m_dead = dead;
		}
		[[nodiscard]] bool isInvisible() const noexcept { return m_invisible; }
		void setInvisible(bool invisible) noexcept { m_invisible = invisible; }
		[[nodiscard]] Common::Enums::Team getTeam() const noexcept { return m_team; }
		void setTeam(Common::Enums::Team team) noexcept { m_team = team; }
		[[nodiscard]] const std::string& nickname() const noexcept { return m_nickname; }
		void setNickname(std::string nickname) { m_nickname = std::move(nickname); }
		[[nodiscard]] const Structures::Half3& lastPosition() const noexcept { return m_lastPosition; }
		void setLastPosition(const Structures::Half3& position) noexcept { m_lastPosition = position; }

	private:
		Connection::Stage m_stage{ Connection::Stage::AwaitingConnect };
		Details::LogThrottle m_droppedLog{ std::chrono::seconds{ 5 } };
		Common::Enums::Team m_team{};
		bool m_inMatch{};
		bool m_dead{};
		std::chrono::steady_clock::time_point m_deadSince{};
		bool m_invisible{};
		std::string m_nickname;
		Structures::Half3 m_lastPosition{};
	};
}

#endif
