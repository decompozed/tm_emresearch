/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#include "Network/IpcClient.h"
#include <chrono>
#include <string>
#include <Utils/Constants.h>
#include <Utils/Logger.h>
#include <Utils/SetupParser.h>
#include "Connection/ConnectionRules.h"
#include "Network/IpcTls.h"

namespace Cast::Network
{
	namespace
	{
		constexpr std::chrono::seconds reconnectDelay{ 5 };

		template<class T>
		std::span<const std::uint8_t> bytesOf(const T& value) noexcept
		{
			return { reinterpret_cast<const std::uint8_t*>(&value), sizeof(value) };
		}
	}

	IpcClient::IpcClient(asio::io_context& io)
		: m_io(io), m_requests(io)
	{
	}

	void IpcClient::start()
	{
		connect();
	}

	void IpcClient::setLinkHandlers(std::function<void()> up, std::function<void()> down)
	{
		m_onLinkUp = std::move(up);
		m_onLinkDown = std::move(down);
	}

	void IpcClient::connect()
	{
		auto socket = std::make_shared<asio::ip::tcp::socket>(m_io);
		auto resolver = std::make_shared<asio::ip::tcp::resolver>(m_io);
		const auto& main = Common::Utils::SetupParser::getInstance().getSelfMainServerInfo();

		resolver->async_resolve(main.ip, std::to_string(main.ipcPort),
			[this, socket, resolver, identity = Common::Network::IpcTls::mainIdentity(main.serverNumber)](const asio::error_code& error,
				asio::ip::tcp::resolver::results_type endpoints) {
				if (error)
				{
					scheduleReconnect();
					return;
				}
				asio::async_connect(*socket, endpoints, [this, socket, identity](const asio::error_code& error, const asio::ip::tcp::endpoint&) {
					if (error)
					{
						scheduleReconnect();
						return;
					}
					auto session = std::make_shared<Common::Network::Session>(std::move(*socket), [this](std::size_t) { scheduleReconnect(); });
					session->m_checkValidSession = false;
					session->startIpc(Common::Network::IpcTls::Role::Client, identity, [this, session]() { onConnected(session); });
				});
			});
	}

	void IpcClient::onConnected(const std::shared_ptr<Common::Network::Session>& session)
	{
		m_session = session;
		if (m_onLinkUp)
		{
			m_onLinkUp();
		}
		send(Common::Constants::C2M_linkHello, 0, 0);
		::Utils::Logger::log("[IpcClient] MainServer link up", ::Utils::LogType::Info);
	}

	void IpcClient::scheduleReconnect()
	{
		if (m_session)
		{
			::Utils::Logger::log("[IpcClient] MainServer link down, reconnecting in 5 s", ::Utils::LogType::Warning);
		}
		m_session.reset();
		m_requests.failAll();
		if (m_onLinkDown)
		{
			m_onLinkDown();
		}
		auto timer = std::make_shared<asio::steady_timer>(m_io);
		timer->expires_after(reconnectDelay);
		timer->async_wait([this, timer](auto) { connect(); });
	}

	void IpcClient::send(std::uint16_t order, std::uint8_t option, std::uint32_t tcpHeader, std::span<const std::uint8_t> payload)
	{
		Common::Network::UnecryptedPacket packet;
		packet.setTcpHeader(tcpHeader);
		packet.setCommand(order, 0, 0, option);
		if (!payload.empty())
		{
			packet.setData(payload.data(), static_cast<std::uint16_t>(payload.size()));
		}
		m_session->asyncWrite(packet);
	}

	void IpcClient::sendPlayerStateUpdate(std::uint32_t accountId, bool isDead)
	{
		if (!Common::Utils::SetupParser::getInstance().getSelfCastServerInfo().IPC_enableDeadBroadcast || !m_session)
		{
			return;
		}
		send(Common::Constants::C2M_updatePlayerState, isDead ? Common::Enums::STATE_DYING : Common::Enums::STATE_NORMAL, 0, bytesOf(accountId));
	}

	void IpcClient::sendPartyCombatEvent(const Common::HaveApi::CombatEvent& event)
	{
		if (linkOpen())
		{
			send(Common::Constants::C2M_partyCombatEvent, 0, 0, bytesOf(event));
		}
	}

	void IpcClient::sendPartyZoneOccupants(std::uint32_t roomSessionId, const Common::HaveApi::ZoneAnswer& occupants)
	{
		if (linkOpen())
		{
			send(Common::Constants::C2M_partyZoneOccupants, 0, roomSessionId, bytesOf(occupants));
		}
	}

	void IpcClient::sendPartyStormOutside(std::uint32_t roomSessionId, const Common::HaveApi::StormAnswer& outside)
	{
		if (linkOpen())
		{
			send(Common::Constants::C2M_partyStormOutside, 0, roomSessionId, bytesOf(outside));
		}
	}

	void IpcClient::sendRoomSyncRequest(std::uint32_t roomNumber)
	{
		if (linkOpen())
		{
			send(Common::Constants::C2M_roomSyncRequest, 0, 0, bytesOf(roomNumber));
		}
	}

	void IpcClient::verifyConnect(const Common::HaveApi::CastConnectCheck& check, ConnectDone done)
	{
		if (!linkOpen())
		{
			::Utils::Logger::log("No MainServer link", ::Utils::LogType::Warning, "Cast::verifyConnect");
			failLater(std::move(done));
			return;
		}
		const std::uint8_t id = m_requests.add(static_cast<std::uint16_t>(Common::Constants::C2M_castConnect),
			[done](const Common::Network::UnecryptedPacket* answer) {
				done(answer ? Common::HaveApi::decodeExact<Common::HaveApi::CastConnectAnswer>(answer->getData(), answer->getDataSize()) : std::nullopt);
			},
			Connection::verifyTimeout);
		if (id == 0)
		{
			failLater(std::move(done));
			return;
		}
		send(Common::Constants::C2M_castConnect, id, 0, bytesOf(check));
	}

	void IpcClient::failLater(ConnectDone done)
	{
		asio::post(m_io, [done = std::move(done)]() { done(std::nullopt); });
	}
}
