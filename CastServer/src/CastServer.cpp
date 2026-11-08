/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#include "CastServer.h"
#include <chrono>
#include <Utils/Logger.h>
#include <Utils/SetupParser.h>
#include "Connection/ConnectionRules.h"
#include "Network/IpcTls.h"
#include "Structures/Connection/ConnectionPackets.h"

namespace Cast
{
	namespace
	{
		constexpr std::chrono::milliseconds positionFlushPeriod{ 10 };
		constexpr std::chrono::seconds pendingAttachPeriod{ 1 };

		bool isMainServerAddress(const asio::ip::address& address)
		{
			return address == asio::ip::address::from_string(Common::Utils::SetupParser::getInstance().getSelfMainServerInfo().ip) ||
				address == asio::ip::address::from_string("::1");
		}
	}

	CastServer::CastServer(asio::io_context& io, const std::string& serverIp, std::uint16_t port, std::uint16_t mainPort, std::uint16_t serverId)
		: m_io(io)
		, m_acceptor(io, tcp::endpoint(asio::ip::address::from_string(serverIp), port))
		, m_mainServerAcceptor(io, tcp::endpoint(asio::ip::address::from_string(Common::Utils::SetupParser::getInstance().getSelfCastServerInfo().ip), mainPort))
		, m_serverId(serverId)
		, m_positionTimer(io)
		, m_attachTimer(io)
		, m_sessions(m_rooms)
		, m_ipc(io)
		, m_ctx{ m_rooms, m_sessions, m_ipc, m_serverId }
		, m_ipcHandlers(m_ctx)
		, m_partyHandlers(m_ctx)
		, m_connectionHandlers(m_ctx)
		, m_roomHandlers(m_ctx)
		, m_matchHandlers(m_ctx)
		, m_positionHandlers(m_ctx)
		, m_damageHandlers(m_ctx)
		, m_unitHandlers(m_ctx)
		, m_itemHandlers(m_ctx)
	{
		Common::Network::Session::useSessionIds(Connection::firstLocalSessionId, Connection::localSessionIds);
		m_rooms.setSyncRequester([this](std::uint32_t roomNumber) { m_ipc.sendRoomSyncRequest(roomNumber); });
		m_ipc.setLinkHandlers([this]() { m_rooms.onLinkUp(); }, [this]() { m_rooms.onLinkDown(); });
		tickPendingAttaches();
		m_positionTimer.expires_after(std::chrono::milliseconds(0));
		tickPositionFlush();
		m_ipc.start();

		m_ipcHandlers.registerRoutes(m_ipcRouter);
		m_partyHandlers.registerRoutes(m_ipcRouter);
		m_connectionHandlers.registerRoutes(m_gameRouter);
		m_roomHandlers.registerRoutes(m_gameRouter);
		m_matchHandlers.registerRoutes(m_gameRouter);
		m_positionHandlers.registerRoutes(m_gameRouter);
		m_damageHandlers.registerRoutes(m_gameRouter);
		m_unitHandlers.registerRoutes(m_gameRouter);
		m_itemHandlers.registerRoutes(m_gameRouter);
	}

	void CastServer::tickPendingAttaches()
	{
		m_rooms.expirePendingAttaches(std::chrono::steady_clock::now(), [](Network::Session& session) { session.writeCommand(Structures::castJoinDenied); });
		m_attachTimer.expires_after(pendingAttachPeriod);
		m_attachTimer.async_wait([this](const asio::error_code& error) {
			if (!error)
			{
				tickPendingAttaches();
			}
		});
	}

	void CastServer::tickPositionFlush()
	{
		for (const auto& room : m_rooms.rooms())
		{
			room->flushPendingPositions();
		}
		m_positionTimer.expires_at(m_positionTimer.expiry() + positionFlushPeriod);
		if (m_positionTimer.expiry() < std::chrono::steady_clock::now())
		{
			m_positionTimer.expires_after(positionFlushPeriod);
		}
		m_positionTimer.async_wait([this](const asio::error_code&) { tickPositionFlush(); });
	}

	void CastServer::asyncAccept()
	{
		m_socket.emplace(m_io);
		m_acceptor.async_accept(*m_socket, [this](const asio::error_code&) {
			auto client = std::make_shared<Network::Session>(std::move(*m_socket), [this](std::size_t sessionId) { m_sessions.removeSession(sessionId); });
			client->m_checkValidSession = true;
			client->sendConnectionACK(Common::Enums::CAST_SERVER);
			closeIfNotAuthenticated(client);
			asyncAccept();
		});
	}

	void CastServer::closeIfNotAuthenticated(const std::shared_ptr<Network::Session>& client)
	{
		auto deadline = std::make_shared<asio::steady_timer>(m_io, Connection::authenticationDeadline);
		deadline->async_wait([deadline, weak = std::weak_ptr<Network::Session>(client)](const asio::error_code& error) {
			if (const auto session = weak.lock(); !error && session && !session->isAuthenticated())
			{
				session->closeSocket();
			}
		});
	}

	void CastServer::asyncAcceptMainServer()
	{
		m_mainSocket.emplace(m_io);
		m_mainServerAcceptor.async_accept(*m_mainSocket, [this](const asio::error_code& error) {
			if (!error)
			{
				acceptMainServerLink();
			}
			asyncAcceptMainServer();
		});
	}

	void CastServer::acceptMainServerLink()
	{
		asio::error_code endpointError;
		const tcp::endpoint remote = m_mainSocket->remote_endpoint(endpointError);
		if (endpointError)
		{
			m_mainSocket->close(endpointError);
			return;
		}
		if (!isMainServerAddress(remote.address()))
		{
			::Utils::Logger::log("Unauthorized connection attempt from IP " + remote.address().to_string(), ::Utils::LogType::Warning);
			m_mainSocket->close();
			return;
		}
		auto link = std::make_shared<Common::Network::Session>(std::move(*m_mainSocket), nullptr);
		link->m_checkValidSession = false;
		link->startIpc(Common::Network::IpcTls::Role::Server, Common::Network::IpcTls::castIdentity(m_serverId));
	}
}
