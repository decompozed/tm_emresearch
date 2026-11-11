#ifndef COMMON_UTILS_PWNED_PASSWORDS_H
#define COMMON_UTILS_PWNED_PASSWORDS_H

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <cryptopp/sha.h>

// The pure half of the breached password check (k-anonymity range search of Pwned Passwords): only the first 5 hex
// characters of the SHA-1 leave the server, the answer lists every known suffix with that prefix and the match happens
// here. The HTTPS request is in MainServer (Network/PwnedPasswords.cpp).
namespace Common
{
	namespace Utils
	{
		namespace PwnedPasswords
		{
			inline constexpr std::size_t prefixLength = 5;
			inline constexpr std::size_t suffixLength = 35;

			// SHA-1 of the password as 40 upper-case hex characters, the form the range API uses
			inline std::string sha1Hex(std::string_view text)
			{
				std::array<CryptoPP::byte, CryptoPP::SHA1::DIGESTSIZE> digest{};
				CryptoPP::SHA1().CalculateDigest(digest.data(), reinterpret_cast<const CryptoPP::byte*>(text.data()), text.size());
				static constexpr char hex[] = "0123456789ABCDEF";
				std::string out;
				out.reserve(digest.size() * 2);
				for (const CryptoPP::byte b : digest)
				{
					out.push_back(hex[b >> 4]);
					out.push_back(hex[b & 0x0F]);
				}
				return out;
			}

			inline bool sameHex(std::string_view a, std::string_view b)
			{
				if (a.size() != b.size())
				{
					return false;
				}
				for (std::size_t i = 0; i < a.size(); ++i)
				{
					const auto upper = [](char c) { return (c >= 'a' && c <= 'f') ? static_cast<char>(c - 'a' + 'A') : c; };
					if (upper(a[i]) != upper(b[i]))
					{
						return false;
					}
				}
				return true;
			}

			// How often the suffix appears in a range answer ("SUFFIX:COUNT" lines, CRLF or LF). 0 when it is not listed, which
			// includes the padding lines (count 0) the API adds with Add-Padding. Nothing when the text has no valid line at all,
			// so an error page is never read as "not breached".
			inline std::optional<std::uint64_t> countInRange(std::string_view body, std::string_view suffix)
			{
				bool anyValid = false;
				std::uint64_t found = 0;
				while (!body.empty())
				{
					const std::size_t end = body.find('\n');
					std::string_view line = body.substr(0, end);
					body = end == std::string_view::npos ? std::string_view{} : body.substr(end + 1);
					if (!line.empty() && line.back() == '\r')
					{
						line.remove_suffix(1);
					}

					const std::size_t colon = line.find(':');
					if (colon != suffixLength)
					{
						continue;
					}
					const std::string_view count = line.substr(colon + 1);
					std::uint64_t value = 0;
					const auto [ptr, error] = std::from_chars(count.data(), count.data() + count.size(), value);
					if (count.empty() || error != std::errc{} || ptr != count.data() + count.size())
					{
						continue;
					}
					anyValid = true;
					if (sameHex(line.substr(0, colon), suffix))
					{
						found = value;
					}
				}
				if (!anyValid)
				{
					return std::nullopt;
				}
				return found;
			}
		}
	}
}

#endif
