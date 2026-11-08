/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */


#include <vector>
#include <iostream>
#include <cstring>
#include <ctime>
#include <optional>

#ifdef _WIN32
#include <corecrt.h>
#endif

#include "../../include/Network/Session.h"
#include "../../include/Network/IpcStats.h"
#include "../../include/Network/IpcSecurity.h"
#include "../../include/Network/IpcTls.h"
#include "../../include/Utils/Parser.h"
#include "../../include/Enums/ExtrasEnums.h"
#include "../../../MainServer/include/Structures/AccountInfo/MainAccountUniqueId.h"
#include <include/Utils/SetupParser.h>
#include "../../include/Utils/SecurityEvent.h"

#include <cryptopp/dh.h>
#include <cryptopp/osrng.h>
#include <cryptopp/secblock.h>
#include <cryptopp/pem.h>
#include <cryptopp/hex.h>
#include <cryptopp/cryptlib.h>
#include <cryptopp/eccrypto.h>
#include <cryptopp/osrng.h>
#include <cryptopp/secblock.h>
#include <cryptopp/oids.h> 
#include <cstring>
#include <iostream>

namespace Common
{
	namespace Network
	{
		void Session::sendMessage(const std::string& message, std::uint32_t extra)
		{
			std::string completeMessage;
			completeMessage.reserve(16 + message.size());
			completeMessage.append(16, '0');
			completeMessage.append(message);

			Common::Network::Packet response;
			response.setTcpHeader(getId(), Common::Enums::NO_ENCRYPTION);
			response.setCommand(316, 0, extra, completeMessage.size());
			response.setData(reinterpret_cast<std::uint8_t*>(completeMessage.data()), completeMessage.size());
			asyncWrite(response);
		}

		bool Session::asyncWrite(const Common::Network::Packet& message)
		{
			return asyncWriteImpl<PacketType::ENCRYPTED>(message);
		}

		bool Session::asyncWrite(const Common::Network::UnecryptedPacket& message)
		{
			return asyncWriteImpl<PacketType::UNECRYPTED>(message);
		}

		void Session::flushWriteQueue()
		{
			if (m_stageBuf.empty() || !m_socket.is_open() || m_tlsHandshaking)
			{
				m_writing = false;
				return;
			}
			m_writing = true;
			m_sendBuf.clear();
			std::swap(m_sendBuf, m_stageBuf);
			auto onWritten = [this, self = this->shared_from_this()](const asio::error_code& errorCode, std::size_t)
				{
					if (errorCode)
					{
						closeSocket();
						return;
					}
					if (!m_stageBuf.empty())
						flushWriteQueue();
					else
						m_writing = false;
				};
			if (m_tls)
				asio::async_write(m_tls->stream(), asio::buffer(m_sendBuf), std::move(onWritten));
			else
				asio::async_write(m_socket, asio::buffer(m_sendBuf), std::move(onWritten));
		}

		void Session::startIpc(IpcTls::Role role, const std::string& identity, std::function<void()> onReady)
		{
			IpcTls::Context* context = IpcSecurity::context();
			if (context == nullptr)
			{
				if (IpcSecurity::required())
				{
					::Utils::Logger::log("[IPC] link with " + m_ip + " refused: TLS is required but not available", ::Utils::LogType::Error);
					closeSocket();
					return;
				}
				sendConnectionACK(Common::Enums::IPC_SERVER);
				if (onReady)
					onReady();
				return;
			}
			try
			{
				m_tls = std::make_shared<IpcTls::Link>(m_socket, *context, role, identity);
			}
			catch (const std::exception& e)
			{
				::Utils::Logger::log("[IPC] link with " + m_ip + " refused: " + e.what(), ::Utils::LogType::Error);
				closeSocket();
				return;
			}
			m_tlsHandshaking = true;
			m_tls->handshake(IpcTls::handshakeTimeout,
				[this, self = this->shared_from_this(), onReady = std::move(onReady)](const std::error_code& error)
				{
					m_tlsHandshaking = false;
					if (error)
					{
						::Utils::Logger::log("[IPC] TLS handshake with " + m_ip + " (" + m_tls->identity() + ") failed: " + error.message()
							+ ". Both servers need [Ipc] SecretEnvironmentName with the same secret.", ::Utils::LogType::Warning);
						closeSocket();
						return;
					}
					sendConnectionACK(Common::Enums::IPC_SERVER);
					if (!m_writing)
						flushWriteQueue();
					if (onReady)
						onReady();
				});
		}
	
		void Session::asyncRead()
		{
			if (m_checkValidSession && m_isFirstRead)
			{
				m_isFirstRead = false;
				if (s_loggedIps.contains(m_ip))
				{
					m_isValidSession = true;
					s_loggedIps.erase(m_ip);
				}
			}
			if (m_checkValidSession && !m_isValidSession)
			{
				//::Utils::Logger::log("Invalid session with IP: " + m_ip, ::Utils::LogType::Warning, "Session::onRead");
				Common::Utils::SecurityEvent::emit("authz_denied", "MEDIUM", 0, m_ip, "denied", "connection without a login from this IP", corrId());
				closeSocket();
				return;
			}
			if (!m_socket.is_open())
			{
				return;
			}
			auto onReceived = [this, self = this->shared_from_this()](const asio::error_code& error, std::size_t bytes_transferred)
				{
					this->onRead(error, bytes_transferred);
					asyncRead();
				};
			if (m_tls)
				m_tls->stream().async_read_some(asio::buffer(m_buffer.data(), m_buffer.size()), std::move(onReceived));
			else
				m_socket.async_read_some(asio::buffer(m_buffer.data(), m_buffer.size()), std::move(onReceived));
		}

		void Session::onRead(asio::error_code error, std::size_t bytes_transferred)
		{
			if (!error)
			{
				if (m_reader.size() + bytes_transferred > 65536)
				{
					std::printf("Session::onRead() - Buffer overflow detected, closing connection\n");
					Common::Utils::SecurityEvent::emit("packet_invalid", "MEDIUM", 0, m_ip, "dropped", "read_buffer_overflow", corrId());
					closeSocket();
					return;
				}

				const constexpr int headerSize = sizeof(Common::Protocol::TcpHeader);
				m_reader.insert(m_reader.end(), m_buffer.begin(), m_buffer.begin() + bytes_transferred);

				//if (!m_crypt.isUsed) Common::Parser::parse_cast(m_reader.data(), m_reader.size(), 13000, "client", "server");
				//if (m_crypt.isUsed) Common::Parser::parse(m_reader.data(), m_reader.size(), 13000, "client", "server", m_crypt.UserKey);

				Common::Protocol::TcpHeader header;
				Common::Cryptography::Crypt cryptography;
				cryptography.KeySetup(0);

				while (m_reader.size() >= headerSize)
				{
					if (m_crypt.isUsed) 
					{
						cryptography.RC5Decrypt32(reinterpret_cast<int32_t*>(m_reader.data()), &header, headerSize);
						if (!header.isValidMain())
						{
							Common::Utils::SecurityEvent::emit("packet_invalid", "MEDIUM", 0, m_ip, "dropped", "bad_tcp_header size " + std::to_string(header.getSize()), corrId());
							closeSocket();
							return;
						}
					}
					else
					{
						std::memcpy(&header, m_reader.data(), headerSize);
						if (!header.isValidCast())
						{
							Common::Utils::SecurityEvent::emit("packet_invalid", "MEDIUM", 0, m_ip, "dropped", "bad_tcp_header size " + std::to_string(header.getSize()), corrId());
							closeSocket();
							return;
						}
					}

					if (m_reader.size() >= static_cast<std::size_t>(header.getSize()))
					{
						std::vector<std::uint8_t> data(m_reader.begin(), m_reader.begin() + header.getSize());
						onPacket(data);
						if (m_reader.empty()) break;

						const auto newSize = m_reader.size() - header.getSize();
						std::memmove(m_reader.data(), m_reader.data() + header.getSize(), newSize);
						m_reader.resize(newSize);
					}
					else
					{
						// avoid infinite loop
						break;
					}
				}
			}
			else
			{
				if (m_tls && error.category() == asio::error::get_ssl_category())
				{
					::Utils::Logger::log("[IPC] TLS link with " + m_ip + " closed: " + error.message(), ::Utils::LogType::Warning);
				}
				closeSocket();
			}
		}

		void Session::closeSocket()
		{
			if (!m_socket.is_open())
			{
				releaseOwnId();
				return;
			}

			if ((!m_checkValidSession && m_onCloseSocketCallback) || (m_isValidSession && m_onCloseSocketCallback))
			{
				m_onCloseSocketCallback(m_id);
			}

			releaseOwnId();

			asio::error_code errorCode;
			auto endPoint = m_socket.remote_endpoint(errorCode);
			
			m_socket.shutdown(tcp::socket::shutdown_both, errorCode);
			m_socket.close(errorCode);
		}


		// Default implementation is used for IPC
		void Session::onPacket(std::vector<std::uint8_t>& data)
		{
			Common::Network::UnecryptedPacket incomingPacket;
			if (!incomingPacket.processIncomingPacket(data.data(), static_cast<std::uint16_t>(data.size())))
			{
				return;
			}

			const std::uint16_t callbackNum = incomingPacket.getOrder();
			IpcStats::count(callbackNum);
			if (!Common::Network::Session::callbacks<Common::Network::PacketType::UNECRYPTED, Session>.contains(callbackNum))
			{
				::Utils::Logger::log("[default IPC] No callback for order: " + std::to_string(callbackNum), ::Utils::LogType::Warning);
				return;
			}
			Common::Network::Session::callbacks<Common::Network::PacketType::UNECRYPTED, Session>[callbackNum](incomingPacket, shared_from_this());
		}


		std::uint8_t* Session::getBufferData()
		{
			return m_buffer.data();
		}

		std::size_t Session::getBufferSize() const
		{
			return m_buffer.size();
		}
		
		Common::Cryptography::Crypt Session::getUserCrypt() const
		{
			return m_crypt;
		}
		
		Common::Cryptography::Crypt Session::getDefaultCrypt() const
		{
			return m_defaultCrypt;
		}
		
		void Session::sendConnectionACK(Common::Enums::ServerType serverType)
		{
			Packet connectionAck;
			connectionAck.setTcpHeader(m_id, Common::Enums::EncryptionType::NO_ENCRYPTION);

			switch (serverType)
			{
				case Enums::AUTH_SERVER:
				{
					struct AuthAck
					{
						std::int32_t key{ static_cast<std::int32_t>(rand() + 1) };
						std::uint32_t timestamp32 = static_cast<std::uint32_t>(std::time(nullptr));
					} authAck;

					m_crypt.KeySetup(authAck.key);
					connectionAck.setData(reinterpret_cast<std::uint8_t*>(&authAck), sizeof(AuthAck));
					connectionAck.setCommand(401, 0, static_cast<int>(Common::Enums::AUTH_SUCCESS), 0);
					asyncWrite(connectionAck);
					break;
				}

				case Enums::MAIN_SERVER:
				{
					struct UniqueId
					{
						std::uint32_t session : 16 = 0;
						std::uint32_t server : 15 = 4;
						std::uint32_t unknown : 1 = 0;
					};

					struct MainAck
					{
						std::int32_t key;
						UniqueId uniqueId{};
						
						MainAck(const Common::Cryptography::Crypt& crypt)
							: key(static_cast<std::int32_t>(rand() + 1))
						{
						}
					};
					MainAck mainAck{ m_crypt };
					mainAck.uniqueId.session = m_id; 
					mainAck.uniqueId.server = 1;    
					m_crypt.KeySetup(mainAck.key);
					connectionAck.setData(reinterpret_cast<std::uint8_t*>(&mainAck), sizeof(MainAck));
					connectionAck.setCommand(401, 0, static_cast<int>(Common::Enums::MAIN_SUCCESS), 1);
					// nb. option = channel selected by user
					// nb. success = MAIN_SUCCESS
					asyncWrite(connectionAck);

					break;
				}

				case Enums::CAST_SERVER:
				{
					m_crypt.isUsed = false;
					struct CastAck
					{
						std::int32_t key{ static_cast<std::int32_t>(rand() + 1) };
					} castAck;
					connectionAck.setTcpHeader(0, Common::Enums::EncryptionType::NO_ENCRYPTION); // not read by the client (0xAD43E0, 0xABC6F0)
					connectionAck.setData(reinterpret_cast<std::uint8_t*>(&castAck), sizeof(castAck));
					connectionAck.setCommand(401, 0, Common::Enums::CAST_SUCCESS, 0);
					asyncWrite(connectionAck);
					break;
				}

				case Enums::IPC_SERVER:
				{
					m_crypt.isUsed = false;
				}
			}

			asyncRead();
		}

		std::size_t Session::getId() const
		{
			return m_id;
		}
	}
}
