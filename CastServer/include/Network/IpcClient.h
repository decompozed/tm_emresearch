#ifndef CAST_NETWORK_IPC_CLIENT_H
#define CAST_NETWORK_IPC_CLIENT_H

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <asio.hpp>
#include "HaveApi/CastConnect.h"
#include "HaveApi/CombatEvent.h"
#include "HaveApi/Storm.h"
#include "HaveApi/Zone.h"
#include "Network/IpcRequests.h"
#include "Network/Session.h"

namespace Cast::Network
{
	class IpcClient
	{
	public:
		using ConnectDone = std::function<void(std::optional<Common::HaveApi::CastConnectAnswer>)>;

		explicit IpcClient(asio::io_context& io);

		void start();
		[[nodiscard]] std::shared_ptr<Common::Network::Session> session() const { return m_session; }
		void setLinkHandlers(std::function<void()> up, std::function<void()> down);

		void sendPlayerStateUpdate(std::uint32_t accountId, bool isDead);
		void sendPartyCombatEvent(const Common::HaveApi::CombatEvent& event);
		void sendPartyZoneOccupants(std::uint32_t roomSessionId, const Common::HaveApi::ZoneAnswer& occupants);
		void sendPartyStormOutside(std::uint32_t roomSessionId, const Common::HaveApi::StormAnswer& outside);
		void sendRoomSyncRequest(std::uint32_t roomNumber);
		void verifyConnect(const Common::HaveApi::CastConnectCheck& check, ConnectDone done);
		void onAnswer(const Common::Network::UnecryptedPacket& packet) { m_requests.resolve(packet); }

	private:
		void connect();
		void onConnected(const std::shared_ptr<Common::Network::Session>& session);
		void scheduleReconnect();
		void failLater(ConnectDone done);
		[[nodiscard]] bool linkOpen() const { return m_session && m_session->is_open(); }
		void send(std::uint16_t order, std::uint8_t option, std::uint32_t tcpHeader, std::span<const std::uint8_t> payload = {});

		asio::io_context& m_io;
		std::shared_ptr<Common::Network::Session> m_session;
		Common::Network::IpcRequests m_requests;
		std::function<void()> m_onLinkUp;
		std::function<void()> m_onLinkDown;
	};
}

#endif
