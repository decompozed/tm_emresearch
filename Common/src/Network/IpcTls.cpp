/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#include "../../include/Network/IpcTls.h"
#include <openssl/crypto.h>
#include <openssl/ssl.h>
#include <stdexcept>
#include <utility>

namespace Common::Network::IpcTls
{
	namespace
	{
		constexpr unsigned char aes128GcmSha256[] = { 0x13, 0x01 };

		// asio owns (and deletes) the app data slots of SSL_CTX and SSL
		int contextSlot()
		{
			static const int slot = SSL_CTX_get_ex_new_index(0, nullptr, nullptr, nullptr, nullptr);
			return slot;
		}

		int linkSlot()
		{
			static const int slot = SSL_get_ex_new_index(0, nullptr, nullptr, nullptr, nullptr);
			return slot;
		}

		const Context& contextOf(SSL* ssl)
		{
			return *static_cast<const Context*>(SSL_CTX_get_ex_data(SSL_get_SSL_CTX(ssl), contextSlot()));
		}

		const Link& linkOf(SSL* ssl)
		{
			return *static_cast<const Link*>(SSL_get_ex_data(ssl, linkSlot()));
		}

		SSL_SESSION* pskSession(SSL* ssl)
		{
			const SSL_CIPHER* cipher = SSL_CIPHER_find(ssl, aes128GcmSha256);
			SSL_SESSION* session = SSL_SESSION_new();
			const auto secret = contextOf(ssl).secret();
			if (cipher == nullptr || session == nullptr
				|| SSL_SESSION_set1_master_key(session, secret.data(), secret.size()) != 1
				|| SSL_SESSION_set_cipher(session, cipher) != 1
				|| SSL_SESSION_set_protocol_version(session, TLS1_3_VERSION) != 1)
			{
				SSL_SESSION_free(session);
				return nullptr;
			}
			return session;
		}

		int useSession(SSL* ssl, const EVP_MD* md, const unsigned char** id, std::size_t* idLength, SSL_SESSION** session)
		{
			*session = pskSession(ssl);
			if (*session == nullptr)
			{
				return 0;
			}
			if (md != nullptr && md != SSL_CIPHER_get_handshake_digest(SSL_SESSION_get0_cipher(*session)))
			{
				SSL_SESSION_free(*session);
				*session = nullptr;
				return 0;
			}
			const std::string& identity = linkOf(ssl).identity();
			*id = reinterpret_cast<const unsigned char*>(identity.data());
			*idLength = identity.size();
			return 1;
		}

		int findSession(SSL* ssl, const unsigned char* identity, std::size_t identityLength, SSL_SESSION** session)
		{
			*session = nullptr;
			const std::string& expected = linkOf(ssl).identity();
			if (identityLength != expected.size() || CRYPTO_memcmp(identity, expected.data(), identityLength) != 0)
			{
				return 0;
			}
			*session = pskSession(ssl);
			return *session != nullptr ? 1 : 0;
		}

		void restrictToPsk(SSL_CTX* ctx, Context* owner)
		{
			if (SSL_CTX_set_min_proto_version(ctx, TLS1_3_VERSION) != 1
				|| SSL_CTX_set_max_proto_version(ctx, TLS1_3_VERSION) != 1
				|| SSL_CTX_set_ciphersuites(ctx, "TLS_AES_128_GCM_SHA256") != 1
				|| SSL_CTX_set_num_tickets(ctx, 0) != 1
				|| contextSlot() < 0 || SSL_CTX_set_ex_data(ctx, contextSlot(), owner) != 1)
			{
				throw std::runtime_error("IpcTls: OpenSSL context setup failed");
			}
			SSL_CTX_set_options(ctx, SSL_OP_NO_TICKET);
			SSL_CTX_set_session_cache_mode(ctx, SSL_SESS_CACHE_OFF);
		}
	}

	Context::Context(std::span<const std::uint8_t> secret)
		: m_secret(secret.begin(), secret.end())
		, m_client(asio::ssl::context::tls_client)
		, m_server(asio::ssl::context::tls_server)
	{
		if (m_secret.size() < secretBytes)
		{
			throw std::invalid_argument("IpcTls: the secret is too short");
		}
		restrictToPsk(m_client.native_handle(), this);
		restrictToPsk(m_server.native_handle(), this);
		m_client.set_verify_mode(asio::ssl::verify_peer);
		SSL_CTX_set_psk_use_session_callback(m_client.native_handle(), &useSession);
		SSL_CTX_set_psk_find_session_callback(m_server.native_handle(), &findSession);
	}

	Context::~Context()
	{
		OPENSSL_cleanse(m_secret.data(), m_secret.size());
	}

	Link::Link(asio::ip::tcp::socket& socket, Context& context, Role role, std::string identity)
		: m_stream(socket, context.forRole(role))
		, m_timer(socket.get_executor())
		, m_identity(std::move(identity))
		, m_role(role)
	{
		if (linkSlot() < 0 || SSL_set_ex_data(m_stream.native_handle(), linkSlot(), this) != 1)
		{
			throw std::runtime_error("IpcTls: OpenSSL stream setup failed");
		}
	}

	void Link::handshake(std::chrono::milliseconds timeout, std::function<void(const std::error_code&)> done)
	{
		m_timer.expires_after(timeout);
		m_timer.async_wait([this](const std::error_code& error) {
			if (error || m_finished)
			{
				return;
			}
			m_timedOut = true;
			std::error_code ignored;
			m_stream.next_layer().cancel(ignored);
		});
		const auto type = m_role == Role::Client ? asio::ssl::stream_base::client : asio::ssl::stream_base::server;
		m_stream.async_handshake(type, [this, done = std::move(done)](std::error_code error) {
			m_finished = true;
			m_timer.cancel();
			if (m_timedOut)
			{
				error = asio::error::timed_out;
			}
			else if (!error && SSL_session_reused(m_stream.native_handle()) != 1)
			{
				error = asio::error::access_denied;
			}
			done(error);
		});
	}
}
