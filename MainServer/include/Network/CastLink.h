/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#ifndef MAIN_NETWORK_CAST_LINK_H
#define MAIN_NETWORK_CAST_LINK_H

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <asio.hpp>
#include "Network/IpcRequests.h"
#include "Network/Packet.h"
#include "Network/Session.h"
#include "Utils/Logger.h"

namespace Main::Ipc
{
	// The MainServer -> CastServer commands. They travel on the one connection the CastServer keeps open to the
	// MainServer (Cast IpcClient, reconnected every 5 s; it announces itself with C2M_linkHello): the Cast routes a
	// command by its order whatever connection brings it, and answers on the same connection. A command is written without
	// waiting (the session's write queue, TCP_NODELAY); an answer arrives as a packet event and calls the request's
	// callback (IpcRequests: the command's option byte = the number).
	class CastLink
	{
	public:
		static CastLink& instance()
		{
			static CastLink link;
			return link;
		}

		void initialize(asio::io_context& io)
		{
			m_io = &io;
			m_requests = std::make_unique<Common::Network::IpcRequests>(io);
		}

		void setOnAttach(std::function<void()> onAttach) { m_onAttach = std::move(onAttach); }

		// C2M_linkHello: this session is the CastServer's link (a new one replaces the previous)
		void attach(const std::shared_ptr<Common::Network::Session>& session)
		{
			{
				std::lock_guard lock{ m_mutex };
				m_session = session;
			}
			m_warned = false;
			::Utils::Logger::log("[CastLink] CastServer link up", ::Utils::LogType::Info);
			if (m_onAttach)
			{
				m_onAttach();
			}
		}

		// The CastServer's link: messages meant for the AuthServer's connections (A2M_*) are not taken from it
		[[nodiscard]] bool isLink(const std::shared_ptr<Common::Network::Session>& session) const
		{
			std::lock_guard lock{ m_mutex };
			return session != nullptr && m_session.lock() == session;
		}

		[[nodiscard]] bool connected() const
		{
			std::lock_guard lock{ m_mutex };
			const auto session = m_session.lock();
			return session && session->is_open();
		}

		// Fire and forget: false when the link is down (the command is dropped, as the Cast is not there to run it)
		bool send(const Common::Network::UnecryptedPacket& packet)
		{
			std::shared_ptr<Common::Network::Session> session;
			{
				std::lock_guard lock{ m_mutex };
				session = m_session.lock();
			}
			if (!session || !session->is_open() || m_io == nullptr)
			{
				if (!m_warned.exchange(true))
				{
					::Utils::Logger::log("[CastLink] no CastServer link: command " + std::to_string(packet.getOrder()) + " dropped",
						::Utils::LogType::Warning);
				}
				return false;
			}
			asio::post(*m_io, [session, packet]() { session->asyncWrite(packet); }); // the session's own thread
			return true;
		}

		// A command with an answer: `answer` gets the Cast's packet, or nullptr (no link, timeout). Never waits.
		void request(Common::Network::UnecryptedPacket packet, Common::Network::IpcRequests::Answer answer,
			std::chrono::milliseconds timeout = std::chrono::milliseconds{ 3000 })
		{
			if (!m_requests || !connected())
			{
				send(packet); // logs once
				if (answer && m_io != nullptr)
				{
					asio::post(*m_io, [answer]() { answer(nullptr); });
				}
				return;
			}
			const std::uint8_t id = m_requests->add(static_cast<std::uint16_t>(packet.getOrder()), std::move(answer), timeout);
			if (id == 0)
			{
				return;
			}
			packet.setOption(id);
			if (!send(packet))
			{
				m_requests->failAll(); // the link just went down: no answer will come
			}
		}

		// An answer of the Cast (the orders of the commands that have one)
		void onAnswer(const Common::Network::UnecryptedPacket& packet)
		{
			if (m_requests)
			{
				m_requests->resolve(packet);
			}
		}

	private:
		CastLink() = default;

		asio::io_context* m_io = nullptr;
		std::unique_ptr<Common::Network::IpcRequests> m_requests;
		mutable std::mutex m_mutex;
		std::weak_ptr<Common::Network::Session> m_session;
		std::atomic<bool> m_warned{ false };
		std::function<void()> m_onAttach;
	};
}

#endif
