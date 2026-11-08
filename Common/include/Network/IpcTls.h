#ifndef COMMON_NETWORK_IPC_TLS_H
#define COMMON_NETWORK_IPC_TLS_H

#include <chrono>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <system_error>
#include <vector>
#include <asio.hpp>
#include <asio/ssl.hpp>

namespace Common::Network::IpcTls
{
	enum class Role : std::uint8_t { Client, Server };

	using Stream = asio::ssl::stream<asio::ip::tcp::socket&>;

	inline constexpr std::chrono::milliseconds handshakeTimeout{ 5000 };
	inline constexpr std::size_t secretBytes = 32;

	inline std::string mainIdentity(std::uint32_t serverNumber) { return "main-" + std::to_string(serverNumber); }
	inline std::string castIdentity(std::uint32_t serverNumber) { return "cast-" + std::to_string(serverNumber); }

	class Context
	{
	public:
		explicit Context(std::span<const std::uint8_t> secret);
		~Context();
		Context(const Context&) = delete;
		Context& operator=(const Context&) = delete;

		asio::ssl::context& forRole(Role role) { return role == Role::Client ? m_client : m_server; }
		std::span<const std::uint8_t> secret() const { return m_secret; }

	private:
		std::vector<std::uint8_t> m_secret;
		asio::ssl::context m_client;
		asio::ssl::context m_server;
	};

	class Link
	{
	public:
		Link(asio::ip::tcp::socket& socket, Context& context, Role role, std::string identity);
		Link(const Link&) = delete;
		Link& operator=(const Link&) = delete;

		Stream& stream() { return m_stream; }
		const std::string& identity() const { return m_identity; }

		void handshake(std::chrono::milliseconds timeout, std::function<void(const std::error_code&)> done);

	private:
		Stream m_stream;
		asio::steady_timer m_timer;
		std::string m_identity;
		Role m_role;
		bool m_timedOut{};
		bool m_finished{};
	};
}

#endif
