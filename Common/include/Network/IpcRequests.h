/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#ifndef COMMON_NETWORK_IPC_REQUESTS_H
#define COMMON_NETWORK_IPC_REQUESTS_H

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <utility>
#include <asio.hpp>
#include "Network/Packet.h"

namespace Common::Network
{
	// The requests of the MainServer <-> CastServer link waiting for their answer: nothing waits on a socket. A request
	// gets a number (the command's option byte, 1..255; the other side echoes it in its answer) and a timer; the answer, or
	// the timeout (nullptr), calls its callback once, on the io_context.
	class IpcRequests
	{
	public:
		using Answer = std::function<void(const UnecryptedPacket*)>;

		explicit IpcRequests(asio::io_context& io) : m_io{ io } {}

		// The request's number for its command (0: too many requests of that command in flight, none sent)
		std::uint8_t add(std::uint16_t order, Answer answer, std::chrono::milliseconds timeout)
		{
			std::lock_guard lock{ m_mutex };
			std::uint8_t id = 0;
			for (int tries = 0; tries < 255 && id == 0; ++tries)
			{
				m_next = static_cast<std::uint8_t>(m_next == 255 ? 1 : m_next + 1);
				if (!m_pending.contains({ order, m_next }))
				{
					id = m_next;
				}
			}
			if (id == 0)
			{
				return 0;
			}
			auto timer = std::make_shared<asio::steady_timer>(m_io, timeout);
			m_pending[{ order, id }] = { std::move(answer), timer };
			timer->async_wait([this, order, id](const asio::error_code& error) {
				if (!error)
				{
					finish(order, id, nullptr);
				}
			});
			return id;
		}

		// An answer (command = the request's, option = its number); unknown or late answers are ignored
		void resolve(const UnecryptedPacket& answer)
		{
			finish(static_cast<std::uint16_t>(answer.getOrder()), answer.getOption(), &answer);
		}

		// The link is gone: every request ends without an answer
		void failAll()
		{
			std::map<std::pair<std::uint16_t, std::uint8_t>, Pending> pending;
			{
				std::lock_guard lock{ m_mutex };
				pending.swap(m_pending);
			}
			for (auto& [key, request] : pending)
			{
				request.timer->cancel();
				if (request.answer)
				{
					request.answer(nullptr);
				}
			}
		}

	private:
		struct Pending
		{
			Answer answer;
			std::shared_ptr<asio::steady_timer> timer;
		};

		void finish(std::uint16_t order, std::uint8_t id, const UnecryptedPacket* answer)
		{
			Pending request;
			{
				std::lock_guard lock{ m_mutex };
				const auto found = m_pending.find({ order, id });
				if (found == m_pending.end())
				{
					return;
				}
				request = std::move(found->second);
				m_pending.erase(found);
			}
			request.timer->cancel();
			if (request.answer)
			{
				request.answer(answer);
			}
		}

		asio::io_context& m_io;
		std::mutex m_mutex;
		std::map<std::pair<std::uint16_t, std::uint8_t>, Pending> m_pending;
		std::uint8_t m_next = 0;
	};
}

#endif
