/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#include "Network/Sessions/CastSession.h"
#include <format>
#include <Utils/Logger.h>

namespace Cast::Network
{
	Session::Session(asio::ip::tcp::socket&& socket, std::function<void(std::size_t)> onClose)
		: Common::Network::Session{ std::move(socket), onClose }
	{
		m_socket.set_option(asio::ip::tcp::no_delay(true));
	}

	void Session::onPacket(std::vector<std::uint8_t>& data)
	{
		Common::Network::UnecryptedPacket packet;
		if (!packet.processIncomingPacket(data.data(), static_cast<std::uint16_t>(data.size())))
		{
			return;
		}

		const std::uint16_t order = packet.getOrder();
		if (!Connection::accepts(m_stage, order))
		{
			logDropped(packet, m_stage == Connection::Stage::AwaitingConnect ? "before the 252" : "252 not accepted or repeated");
			return;
		}

		auto& handlers = Common::Network::Session::callbacks<Common::Network::PacketType::UNECRYPTED, Session>;
		const auto handler = handlers.find(order);
		if (handler == handlers.end())
		{
			::Utils::Logger::log("[SEID: " + std::to_string(m_id) + "] No callback for order: " + std::to_string(order),
				::Utils::LogType::Error, "CastSession::onPacket");
			m_reader.resize(0);
			return;
		}
		handler->second(packet, std::static_pointer_cast<Session>(shared_from_this()));
	}

	void Session::logDropped(const Common::Network::UnecryptedPacket& packet, std::string_view reason)
	{
		if (!m_droppedLog.allow(Details::LogThrottle::Clock::now()))
		{
			return;
		}
		const auto suppressed = m_droppedLog.takeSuppressed();
		::Utils::Logger::log(std::format("[AntiCheat] order {} (mission {}, extra {}, option {}, {} bytes) from session {} dropped: {}{}",
			packet.getOrder(), packet.getMission(), packet.getExtra(), packet.getOption(), packet.getDataSize(), m_id, reason,
			suppressed ? std::format(" ({} similar lines suppressed)", suppressed) : std::string{}), ::Utils::LogType::Warning, "Cast::Session");
	}

	void Session::writeCommand(const Structures::CommandFields& command)
	{
		Common::Network::UnecryptedPacket packet;
		packet.setTcpHeader(static_cast<std::uint32_t>(getId()));
		packet.setCommand(command.order, command.mission, command.extra, command.option);
		asyncWrite(packet);
	}
}
