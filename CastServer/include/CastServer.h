#ifndef CAST_SERVER_H
#define CAST_SERVER_H

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <asio.hpp>
#include "App/ServerContext.h"
#include "Handlers/ConnectionHandlers.h"
#include "Handlers/DamageHandlers.h"
#include "Handlers/IpcHandlers.h"
#include "Handlers/ItemHandlers.h"
#include "Handlers/MatchHandlers.h"
#include "Handlers/PartyHandlers.h"
#include "Handlers/PositionHandlers.h"
#include "Handlers/RoomHandlers.h"
#include "Handlers/UnitHandlers.h"
#include "Managers/RoomsManager.h"
#include "Network/IpcClient.h"
#include "Network/PacketRouter.h"
#include "Network/SessionsManager.h"

namespace Cast
{
	class CastServer
	{
	public:
		CastServer(asio::io_context& io, const std::string& serverIp, std::uint16_t port, std::uint16_t mainPort, std::uint16_t serverId);

		void asyncAccept();
		void asyncAcceptMainServer();

	private:
		using tcp = asio::ip::tcp;

		void tickPositionFlush();
		void tickPendingAttaches();
		void closeIfNotAuthenticated(const std::shared_ptr<Network::Session>& client);
		void acceptMainServerLink();

		asio::io_context& m_io;
		tcp::acceptor m_acceptor;
		tcp::acceptor m_mainServerAcceptor;
		std::optional<tcp::socket> m_socket;
		std::optional<tcp::socket> m_mainSocket;
		std::uint16_t m_serverId;
		asio::steady_timer m_positionTimer;
		asio::steady_timer m_attachTimer;

		Domain::RoomsManager m_rooms;
		Network::SessionsManager m_sessions;
		Network::IpcClient m_ipc;
		ServerContext m_ctx;

		Handlers::IpcHandlers m_ipcHandlers;
		Handlers::PartyHandlers m_partyHandlers;
		Handlers::ConnectionHandlers m_connectionHandlers;
		Handlers::RoomHandlers m_roomHandlers;
		Handlers::MatchHandlers m_matchHandlers;
		Handlers::PositionHandlers m_positionHandlers;
		Handlers::DamageHandlers m_damageHandlers;
		Handlers::UnitHandlers m_unitHandlers;
		Handlers::ItemHandlers m_itemHandlers;

		Network::PacketRouter<Network::Session> m_gameRouter;
		Network::PacketRouter<Common::Network::Session> m_ipcRouter;
	};
}

#endif
