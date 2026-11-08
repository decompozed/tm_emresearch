#include <gtest/gtest.h>
#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <system_error>
#include <vector>
#include "Network/IpcTls.h"

namespace IpcTls = Common::Network::IpcTls;
using asio::ip::tcp;

namespace
{
	std::vector<std::uint8_t> secretOf(std::uint8_t seed)
	{
		std::vector<std::uint8_t> secret(IpcTls::secretBytes);
		for (std::size_t i = 0; i < secret.size(); ++i)
		{
			secret[i] = static_cast<std::uint8_t>(seed + i * 7);
		}
		return secret;
	}

	struct SocketPair
	{
		explicit SocketPair(asio::io_context& io)
			: first(io), second(io)
		{
			tcp::acceptor acceptor(io, tcp::endpoint(asio::ip::address_v4::loopback(), 0));
			first.connect(acceptor.local_endpoint());
			acceptor.accept(second);
		}

		tcp::socket first;
		tcp::socket second;
	};

	struct Outcome
	{
		bool done = false;
		std::error_code error;
	};

	void startHandshake(IpcTls::Link& link, Outcome& outcome, std::chrono::milliseconds timeout = IpcTls::handshakeTimeout)
	{
		link.handshake(timeout, [&outcome](const std::error_code& error) {
			outcome.done = true;
			outcome.error = error;
		});
	}

	bool runUntil(asio::io_context& io, const std::function<bool()>& finished)
	{
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
		while (!finished() && std::chrono::steady_clock::now() < deadline)
		{
			io.run_one_for(std::chrono::milliseconds(50));
		}
		return finished();
	}

	class Relay
	{
	public:
		Relay(tcp::socket& clientSide, tcp::socket& serverSide)
			: m_clientSide(clientSide), m_serverSide(serverSide)
		{
			pump(m_clientSide, m_serverSide, m_upBuffer, true);
			pump(m_serverSide, m_clientSide, m_downBuffer, false);
		}

		std::function<void(std::vector<std::uint8_t>&)> towardsServer;

	private:
		void pump(tcp::socket& from, tcp::socket& to, std::array<std::uint8_t, 4096>& buffer, bool up)
		{
			from.async_read_some(asio::buffer(buffer), [this, &from, &to, &buffer, up](const std::error_code& error, std::size_t length) {
				if (error)
				{
					std::error_code ignored;
					to.shutdown(tcp::socket::shutdown_send, ignored);
					return;
				}
				auto chunk = std::make_shared<std::vector<std::uint8_t>>(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(length));
				if (up && towardsServer)
				{
					towardsServer(*chunk);
				}
				asio::async_write(to, asio::buffer(*chunk), [this, &from, &to, &buffer, up, chunk](const std::error_code& writeError, std::size_t) {
					if (!writeError)
					{
						pump(from, to, buffer, up);
					}
				});
			});
		}

		tcp::socket& m_clientSide;
		tcp::socket& m_serverSide;
		std::array<std::uint8_t, 4096> m_upBuffer{};
		std::array<std::uint8_t, 4096> m_downBuffer{};
	};

	struct RelayedLink
	{
		RelayedLink()
			: clientPair(io), serverPair(io), relay(clientPair.second, serverPair.first)
			, server(serverPair.second, serverContext, IpcTls::Role::Server, IpcTls::mainIdentity(1))
			, client(clientPair.first, clientContext, IpcTls::Role::Client, IpcTls::mainIdentity(1))
		{
		}

		bool connect()
		{
			startHandshake(server, serverOutcome);
			startHandshake(client, clientOutcome);
			return runUntil(io, [this] { return serverOutcome.done && clientOutcome.done; }) && !serverOutcome.error && !clientOutcome.error;
		}

		asio::io_context io;
		IpcTls::Context serverContext{ secretOf(1) };
		IpcTls::Context clientContext{ secretOf(1) };
		SocketPair clientPair;
		SocketPair serverPair;
		Relay relay;
		IpcTls::Link server;
		IpcTls::Link client;
		Outcome serverOutcome;
		Outcome clientOutcome;
	};

	constexpr std::array<std::uint8_t, 12> message{ 0x00, 0x00, 0x30, 0x00, 0x04, 0x01, 0x00, 0x00, 0x2A, 0x00, 0x00, 0x00 };
}

TEST(IpcTls, HandshakeWithTheSameSecretCarriesData)
{
	asio::io_context io;
	SocketPair sockets(io);
	IpcTls::Context serverContext{ secretOf(1) };
	IpcTls::Context clientContext{ secretOf(1) };
	IpcTls::Link server(sockets.second, serverContext, IpcTls::Role::Server, IpcTls::mainIdentity(1));
	IpcTls::Link client(sockets.first, clientContext, IpcTls::Role::Client, IpcTls::mainIdentity(1));
	Outcome serverOutcome, clientOutcome;
	startHandshake(server, serverOutcome);
	startHandshake(client, clientOutcome);
	ASSERT_TRUE(runUntil(io, [&] { return serverOutcome.done && clientOutcome.done; }));
	ASSERT_FALSE(serverOutcome.error) << serverOutcome.error.message();
	ASSERT_FALSE(clientOutcome.error) << clientOutcome.error.message();

	std::array<std::uint8_t, message.size()> received{};
	bool delivered = false;
	asio::async_write(client.stream(), asio::buffer(message), [](const std::error_code&, std::size_t) {});
	asio::async_read(server.stream(), asio::buffer(received), [&](const std::error_code& error, std::size_t) { delivered = !error; });
	ASSERT_TRUE(runUntil(io, [&] { return delivered; }));
	EXPECT_EQ(received, message);
}

TEST(IpcTls, WrongSecretIsRefusedOnBothSides)
{
	asio::io_context io;
	SocketPair sockets(io);
	IpcTls::Context serverContext{ secretOf(1) };
	IpcTls::Context clientContext{ secretOf(2) };
	IpcTls::Link server(sockets.second, serverContext, IpcTls::Role::Server, IpcTls::mainIdentity(1));
	IpcTls::Link client(sockets.first, clientContext, IpcTls::Role::Client, IpcTls::mainIdentity(1));
	Outcome serverOutcome, clientOutcome;
	startHandshake(server, serverOutcome);
	startHandshake(client, clientOutcome);
	ASSERT_TRUE(runUntil(io, [&] { return serverOutcome.done && clientOutcome.done; }));
	EXPECT_TRUE(serverOutcome.error);
	EXPECT_TRUE(clientOutcome.error);
}

TEST(IpcTls, HandshakeMeantForAnotherServerIsRefused)
{
	asio::io_context io;
	SocketPair sockets(io);
	IpcTls::Context serverContext{ secretOf(1) };
	IpcTls::Context clientContext{ secretOf(1) };
	IpcTls::Link server(sockets.second, serverContext, IpcTls::Role::Server, IpcTls::castIdentity(1));
	IpcTls::Link client(sockets.first, clientContext, IpcTls::Role::Client, IpcTls::mainIdentity(1));
	Outcome serverOutcome, clientOutcome;
	startHandshake(server, serverOutcome);
	startHandshake(client, clientOutcome);
	ASSERT_TRUE(runUntil(io, [&] { return serverOutcome.done && clientOutcome.done; }));
	EXPECT_TRUE(serverOutcome.error);
	EXPECT_TRUE(clientOutcome.error);
}

TEST(IpcTls, PlaintextClientIsRefusedByTlsServer)
{
	asio::io_context io;
	SocketPair sockets(io);
	IpcTls::Context serverContext{ secretOf(1) };
	IpcTls::Link server(sockets.second, serverContext, IpcTls::Role::Server, IpcTls::mainIdentity(1));
	Outcome serverOutcome;
	startHandshake(server, serverOutcome);
	asio::write(sockets.first, asio::buffer(message));
	ASSERT_TRUE(runUntil(io, [&] { return serverOutcome.done; }));
	EXPECT_TRUE(serverOutcome.error);
	EXPECT_NE(serverOutcome.error, std::error_code(asio::error::timed_out));
}

TEST(IpcTls, TlsClientIsRefusedByPlaintextServerThatCloses)
{
	asio::io_context io;
	SocketPair sockets(io);
	IpcTls::Context clientContext{ secretOf(1) };
	IpcTls::Link client(sockets.first, clientContext, IpcTls::Role::Client, IpcTls::mainIdentity(1));
	Outcome clientOutcome;
	startHandshake(client, clientOutcome);
	std::array<std::uint8_t, 8> header{};
	sockets.second.async_read_some(asio::buffer(header), [&](const std::error_code&, std::size_t) {
		std::error_code ignored;
		sockets.second.close(ignored);
	});
	ASSERT_TRUE(runUntil(io, [&] { return clientOutcome.done; }));
	EXPECT_TRUE(clientOutcome.error);
}

TEST(IpcTls, TlsClientTimesOutOnSilentPlaintextServer)
{
	asio::io_context io;
	SocketPair sockets(io);
	IpcTls::Context clientContext{ secretOf(1) };
	IpcTls::Link client(sockets.first, clientContext, IpcTls::Role::Client, IpcTls::mainIdentity(1));
	Outcome clientOutcome;
	startHandshake(client, clientOutcome, std::chrono::milliseconds(200));
	std::array<std::uint8_t, 512> clientHello{};
	sockets.second.async_read_some(asio::buffer(clientHello), [](const std::error_code&, std::size_t) {});
	ASSERT_TRUE(runUntil(io, [&] { return clientOutcome.done; }));
	EXPECT_EQ(clientOutcome.error, std::error_code(asio::error::timed_out));
}

TEST(IpcTls, AlteredRecordIsRefused)
{
	RelayedLink link;
	ASSERT_TRUE(link.connect());
	link.relay.towardsServer = [](std::vector<std::uint8_t>& chunk) { chunk.back() ^= 0x01; };

	std::array<std::uint8_t, message.size()> received{};
	std::error_code readError;
	bool finished = false;
	asio::async_write(link.client.stream(), asio::buffer(message), [](const std::error_code&, std::size_t) {});
	asio::async_read(link.server.stream(), asio::buffer(received), [&](const std::error_code& error, std::size_t) {
		readError = error;
		finished = true;
	});
	ASSERT_TRUE(runUntil(link.io, [&] { return finished; }));
	EXPECT_EQ(readError.category(), asio::error::get_ssl_category()) << readError.message();
	EXPECT_NE(received, message);
}

TEST(IpcTls, ReplayedRecordIsRefused)
{
	RelayedLink link;
	ASSERT_TRUE(link.connect());
	link.relay.towardsServer = [](std::vector<std::uint8_t>& chunk) {
		const std::vector<std::uint8_t> copy = chunk;
		chunk.insert(chunk.end(), copy.begin(), copy.end());
	};

	std::array<std::uint8_t, message.size()> first{};
	std::array<std::uint8_t, message.size()> second{};
	std::error_code firstError, secondError;
	bool finished = false;
	asio::async_write(link.client.stream(), asio::buffer(message), [](const std::error_code&, std::size_t) {});
	asio::async_read(link.server.stream(), asio::buffer(first), [&](const std::error_code& error, std::size_t) {
		firstError = error;
		asio::async_read(link.server.stream(), asio::buffer(second), [&](const std::error_code& replayError, std::size_t) {
			secondError = replayError;
			finished = true;
		});
	});
	ASSERT_TRUE(runUntil(link.io, [&] { return finished; }));
	EXPECT_FALSE(firstError);
	EXPECT_EQ(first, message);
	EXPECT_EQ(secondError.category(), asio::error::get_ssl_category()) << secondError.message();
}
