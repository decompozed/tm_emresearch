/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#include "../include/MainIpcExchange.h"
#include <Network/IpcSecurity.h>
#include <Network/IpcTls.h>
#include <Utils/Constants.h>
#include <Utils/Logger.h>
#include <asio.hpp>
#include <string>

namespace Auth::Utils
{
	std::optional<std::vector<std::uint8_t>> exchangeWithMain(const Common::Utils::MainSetup& server, const std::vector<std::uint8_t>& request,
		std::chrono::milliseconds timeout)
	{
		namespace IpcTls = Common::Network::IpcTls;

		IpcTls::Context* tls = Common::Network::IpcSecurity::context();
		if (tls == nullptr && Common::Network::IpcSecurity::required())
		{
			return std::nullopt;
		}

		asio::io_context io;
		asio::ip::tcp::resolver resolver(io);
		asio::ip::tcp::socket socket(io);
		asio::error_code error;
		const auto endpoints = resolver.resolve(server.ip, std::to_string(server.ipcPort), error);
		if (error)
		{
			return std::nullopt;
		}
		asio::connect(socket, endpoints, error);
		if (error)
		{
			return std::nullopt;
		}

		std::optional<IpcTls::Link> link;
		if (tls != nullptr)
		{
			link.emplace(socket, *tls, IpcTls::Role::Client, IpcTls::mainIdentity(server.serverNumber));
		}

		std::vector<std::uint8_t> response(Common::Constants::maxPacketBytes);
		std::size_t received = 0;
		asio::steady_timer deadline(io, timeout);
		deadline.async_wait([&socket](const asio::error_code& expired) {
			if (!expired)
			{
				asio::error_code ignored;
				socket.cancel(ignored);
			}
		});

		const auto exchange = [&](auto& stream) {
			asio::async_write(stream, asio::buffer(request), [&, target = &stream](const asio::error_code& writeError, std::size_t) {
				if (writeError)
				{
					deadline.cancel();
					return;
				}
				target->async_read_some(asio::buffer(response), [&](const asio::error_code& readError, std::size_t length) {
					received = readError ? 0 : length;
					deadline.cancel();
				});
			});
		};

		if (link)
		{
			link->handshake(timeout, [&](const std::error_code& handshakeError) {
				if (handshakeError)
				{
					::Utils::Logger::log("[IPC] TLS handshake with MainServer " + std::to_string(server.serverNumber) + " (" + server.ip + ") failed: "
						+ handshakeError.message() + ". Both servers need [Ipc] SecretEnvironmentName with the same secret.",
						::Utils::LogType::Warning, "Auth::exchangeWithMain");
					deadline.cancel();
					return;
				}
				exchange(link->stream());
			});
		}
		else
		{
			exchange(socket);
		}
		io.run();

		if (received == 0)
		{
			return std::nullopt;
		}
		response.resize(received);
		return response;
	}
}
