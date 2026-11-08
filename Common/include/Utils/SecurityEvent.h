/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#ifndef COMMON_UTILS_SECURITY_EVENT_H
#define COMMON_UTILS_SECURITY_EVENT_H

#include <chrono>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>

#include <cryptopp/hex.h>
#include <cryptopp/sha.h>

// Security events as JSON lines (one object per line), in <TB_LOG_DIR>/security/<server>-YYYY-MM-DD.log.
// Fields: ts (UTC), event, severity, server, account_id, src, result, reason, corr_id.
// Rules: text that came from a player is escaped, so a quote or a newline cannot add a field or a line; the IP is
// never written, only a short salted hash of it; passwords, codes, tokens and keys are never passed in. Nothing is
// written without TB_LOG_DIR, and a failure to write is swallowed: logging must not take a server down.
namespace Common
{
	namespace Utils
	{
		namespace SecurityEvent
		{
			inline constexpr std::size_t maxFieldBytes = 200;

			// The text as the inside of a JSON string: " \ and control characters escaped, bytes that are not valid UTF-8
			// replaced by U+FFFD, cut to maxFieldBytes of input
			inline std::string escapeJson(std::string_view text)
			{
				std::string out;
				out.reserve(text.size() + 8);
				if (text.size() > maxFieldBytes)
				{
					text = text.substr(0, maxFieldBytes);
				}
				for (std::size_t i = 0; i < text.size();)
				{
					const unsigned char c = static_cast<unsigned char>(text[i]);
					if (c < 0x80)
					{
						switch (c)
						{
						case '"': out += "\\\""; break;
						case '\\': out += "\\\\"; break;
						case '\n': out += "\\n"; break;
						case '\r': out += "\\r"; break;
						case '\t': out += "\\t"; break;
						default:
							if (c < 0x20 || c == 0x7f)
							{
								out += std::format("\\u{:04x}", static_cast<unsigned>(c));
							}
							else
							{
								out += static_cast<char>(c);
							}
						}
						++i;
						continue;
					}

					const std::size_t length = (c >= 0xC2 && c <= 0xDF) ? 2 : (c >= 0xE0 && c <= 0xEF) ? 3 : (c >= 0xF0 && c <= 0xF4) ? 4 : 0;
					bool valid = length != 0 && i + length <= text.size();
					for (std::size_t k = 1; valid && k < length; ++k)
					{
						valid = (static_cast<unsigned char>(text[i + k]) & 0xC0) == 0x80;
					}
					if (valid)
					{
						out.append(text.substr(i, length));
						i += length;
					}
					else
					{
						out += "\\ufffd";
						++i;
					}
				}
				return out;
			}

			// 12 hex characters of SHA-256(salt + ip): enough to tell sources apart, not enough to read the address back
			// without the salt (TB_LOG_SALT in the environment; without it the hash can be guessed for the few IPv4 addresses)
			inline std::string hashSource(std::string_view ip, std::string_view salt = {})
			{
				if (ip.empty())
				{
					return "";
				}
				const std::string input = std::string{ salt } + std::string{ ip };
				std::string digest;
				CryptoPP::SHA256 hash;
				CryptoPP::StringSource source(input, true, new CryptoPP::HashFilter(hash, new CryptoPP::HexEncoder(new CryptoPP::StringSink(digest), false)));
				return digest.substr(0, 12);
			}

			struct Event
			{
				std::string_view event;      // authn_fail, authn_lockout, packet_invalid, admin_command, authz_denied
				std::string_view severity;   // LOW, MEDIUM, HIGH, CRITICAL
				std::uint32_t accountId{ 0 };
				std::string_view src;        // already hashed, see hashSource
				std::string_view result;     // fail, denied, ok, locked
				std::string_view reason;
				std::string_view corrId;
			};

			inline std::string formatTimestamp(std::chrono::system_clock::time_point now)
			{
				return std::format("{:%Y-%m-%dT%H:%M:%S}Z", std::chrono::floor<std::chrono::milliseconds>(now));
			}

			// One line without the newline. Every text field goes through escapeJson, the server name too.
			inline std::string formatLine(const Event& e, std::string_view server, std::chrono::system_clock::time_point now)
			{
				return std::format(R"({{"ts":"{}","event":"{}","severity":"{}","server":"{}","account_id":{},"src":"{}","result":"{}","reason":"{}","corr_id":"{}"}})",
					formatTimestamp(now), escapeJson(e.event), escapeJson(e.severity), escapeJson(server), e.accountId,
					escapeJson(e.src), escapeJson(e.result), escapeJson(e.reason), escapeJson(e.corrId));
			}

			// Writes the event to the process's security log; takes the plain IP and hashes it. Thread-safe, never throws.
			void emit(std::string_view event, std::string_view severity, std::uint32_t accountId, std::string_view ip,
				std::string_view result, std::string_view reason, std::string_view corrId = {}) noexcept;
		}
	}
}

#endif
