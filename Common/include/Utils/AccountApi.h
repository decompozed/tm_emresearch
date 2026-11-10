#ifndef COMMON_UTILS_ACCOUNT_API_H
#define COMMON_UTILS_ACCOUNT_API_H

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <iterator>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// The pure rules of the player account API (the website's register, login and password endpoints): input validation,
// the rate limiter, CORS origin matching and which address counts as the client. No sockets, no database, so all of it
// can be tested without a server. See the API contract for the endpoints that use it.
namespace Common
{
	namespace Utils
	{
		namespace AccountApi
		{
			inline constexpr std::size_t maxBodyBytes = 4096;
			inline constexpr std::size_t maxEmailLength = 254;
			inline constexpr std::size_t minPasswordLength = 10;
			inline constexpr std::size_t maxPasswordLength = 128;
			inline constexpr std::size_t maxTurnstileTokenLength = 2048;

			inline bool isAsciiWordChar(char c)
			{
				return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
			}

			// ^[A-Za-z0-9_]{3,16}$ (also the rule for the nickname, the name other players see)
			inline bool validName(std::string_view name)
			{
				return name.size() >= 3 && name.size() <= 16 && std::all_of(name.begin(), name.end(), isAsciiWordChar);
			}

			inline bool validUsername(std::string_view username) { return validName(username); }
			inline bool validNickname(std::string_view nickname) { return validName(nickname); }

			// Characters, not bytes: every byte that is not a UTF-8 continuation byte starts one. The JSON parser has already
			// refused invalid UTF-8, but this stays correct on its own for a lone continuation byte (counted as nothing).
			inline std::size_t utf8Length(std::string_view text)
			{
				return static_cast<std::size_t>(std::count_if(text.begin(), text.end(),
					[](char c) { return (static_cast<unsigned char>(c) & 0xC0) != 0x80; }));
			}

			inline std::string asciiLower(std::string_view text)
			{
				std::string out(text);
				for (char& c : out)
				{
					if (c >= 'A' && c <= 'Z')
					{
						c = static_cast<char>(c - 'A' + 'a');
					}
				}
				return out;
			}

			// 10 to 128 characters, not equal to the username (case-insensitive), no NUL (it would end a C string early)
			inline bool validPassword(std::string_view password, std::string_view username)
			{
				const std::size_t length = utf8Length(password);
				if (length < minPasswordLength || length > maxPasswordLength)
				{
					return false;
				}
				if (password.find('\0') != std::string_view::npos)
				{
					return false;
				}
				return asciiLower(password) != asciiLower(username);
			}

			// ^[^@\s]+@[^@\s]+\.[^@\s]+$ and at most 254 characters. \s is any ASCII space or control character here.
			inline bool validEmail(std::string_view email)
			{
				if (email.empty() || email.size() > maxEmailLength)
				{
					return false;
				}
				std::size_t at = std::string_view::npos;
				for (std::size_t i = 0; i < email.size(); ++i)
				{
					const unsigned char c = static_cast<unsigned char>(email[i]);
					if (c <= 0x20 || c == 0x7f)
					{
						return false;
					}
					if (c == '@')
					{
						if (at != std::string_view::npos)
						{
							return false;
						}
						at = i;
					}
				}
				if (at == std::string_view::npos || at == 0)
				{
					return false;
				}
				const std::string_view domain = email.substr(at + 1);
				// a dot with something before it and something after it
				for (std::size_t i = 1; i + 1 < domain.size(); ++i)
				{
					if (domain[i] == '.')
					{
						return true;
					}
				}
				return false;
			}

			// ---- CORS ----

			// Exact match against [Website] AllowedOrigins: no wildcard, no prefix or suffix match, no case folding, no trailing
			// slash tolerance (a browser never sends one). "null" (sandboxed pages, local files) and the empty string never match,
			// even if someone lists them.
			inline bool originAllowed(std::string_view origin, const std::vector<std::string>& allowed)
			{
				if (origin.empty() || origin == "null")
				{
					return false;
				}
				return std::find(allowed.begin(), allowed.end(), origin) != allowed.end();
			}

			// ---- client address ----

			inline bool isIpv4Text(std::string_view text)
			{
				int parts = 0;
				std::size_t pos = 0;
				while (pos <= text.size())
				{
					const std::size_t end = text.find('.', pos);
					const std::string_view part = text.substr(pos, end == std::string_view::npos ? std::string_view::npos : end - pos);
					unsigned value = 0;
					if (part.empty() || part.size() > 3)
					{
						return false;
					}
					const auto [ptr, ec] = std::from_chars(part.data(), part.data() + part.size(), value);
					if (ec != std::errc() || ptr != part.data() + part.size() || value > 255)
					{
						return false;
					}
					++parts;
					if (end == std::string_view::npos)
					{
						break;
					}
					pos = end + 1;
				}
				return parts == 4;
			}

			// Loose IPv6 check: hex digits and colons (and a dotted tail), 2 to 7 colons, at most one "::", groups of at most 4 digits
			inline bool isIpv6Text(std::string_view text)
			{
				if (text.size() < 2 || text.size() > 45)
				{
					return false;
				}
				int colons = 0;
				std::size_t groupLength = 0;
				bool sawDouble = false;
				for (std::size_t i = 0; i < text.size(); ++i)
				{
					const char c = text[i];
					if (c == ':')
					{
						++colons;
						if (i > 0 && text[i - 1] == ':')
						{
							if (sawDouble)
							{
								return false;
							}
							sawDouble = true;
						}
						groupLength = 0;
					}
					else if (c == '.')
					{
						const std::size_t lastColon = text.rfind(':');
						return lastColon != std::string_view::npos && i > lastColon && isIpv4Text(text.substr(lastColon + 1)) && colons >= 2;
					}
					else if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))
					{
						if (++groupLength > 4)
						{
							return false;
						}
					}
					else
					{
						return false;
					}
				}
				return colons >= 2 && colons <= 7;
			}

			inline bool isIpText(std::string_view text) { return isIpv4Text(text) || isIpv6Text(text); }

			// The address used for rate limiting and logs. CF-Connecting-IP is believed only when the TCP peer is the tunnel:
			// 127.0.0.1 or an address listed in [Website] TrustedProxy. A missing or malformed header, or any other peer, gives the
			// peer address. IPv6 is lower-cased so one address cannot take two buckets.
			inline std::string clientIp(std::string_view peer, std::string_view cfConnectingIp, const std::vector<std::string>& trustedProxies)
			{
				const bool trusted = peer == "127.0.0.1" || std::find(trustedProxies.begin(), trustedProxies.end(), peer) != trustedProxies.end();
				if (trusted && isIpText(cfConnectingIp))
				{
					return asciiLower(cfConnectingIp);
				}
				return std::string{ peer };
			}

			inline bool peerIsTrusted(std::string_view peer, const std::vector<std::string>& trustedProxies)
			{
				return peer == "127.0.0.1" || std::find(trustedProxies.begin(), trustedProxies.end(), peer) != trustedProxies.end();
			}

			// ---- rate limiter ----

			// At most maxHits per key in any window (a sliding log of hit times). A refused request is not recorded, so someone
			// who keeps trying does not extend their own block. Keys are bounded: when maxKeys are in use after expired ones are
			// dropped, a new key is refused (fail closed) instead of growing without limit.
			class RateLimiter
			{
			public:
				using Clock = std::chrono::steady_clock;

				RateLimiter(std::size_t maxHits, std::chrono::seconds window, std::size_t maxKeys = 100000)
					: m_maxHits{ maxHits }, m_window{ window }, m_maxKeys{ maxKeys }
				{
				}

				bool allow(const std::string& key, Clock::time_point now = Clock::now())
				{
					std::lock_guard<std::mutex> lock(m_mutex);
					auto it = m_hits.find(key);
					if (it == m_hits.end())
					{
						if (m_hits.size() >= m_maxKeys)
						{
							dropExpired(now);
							if (m_hits.size() >= m_maxKeys)
							{
								return false;
							}
						}
						it = m_hits.emplace(key, std::deque<Clock::time_point>{}).first;
					}
					auto& times = it->second;
					while (!times.empty() && now - times.front() >= m_window)
					{
						times.pop_front();
					}
					if (times.size() >= m_maxHits)
					{
						return false;
					}
					times.push_back(now);
					return true;
				}

				std::size_t keyCount() const
				{
					std::lock_guard<std::mutex> lock(m_mutex);
					return m_hits.size();
				}

			private:
				void dropExpired(Clock::time_point now)
				{
					for (auto it = m_hits.begin(); it != m_hits.end();)
					{
						auto& times = it->second;
						while (!times.empty() && now - times.front() >= m_window)
						{
							times.pop_front();
						}
						it = times.empty() ? m_hits.erase(it) : std::next(it);
					}
				}

				std::size_t m_maxHits;
				std::chrono::seconds m_window;
				std::size_t m_maxKeys;
				mutable std::mutex m_mutex;
				std::unordered_map<std::string, std::deque<Clock::time_point>> m_hits;
			};
		}
	}
}

#endif
