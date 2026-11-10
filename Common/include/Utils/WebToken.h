#ifndef COMMON_UTILS_WEB_TOKEN_H
#define COMMON_UTILS_WEB_TOKEN_H

#include <chrono>
#include <cstdint>
#include <exception>
#include <optional>
#include <string>

#include <jwt-cpp/jwt.h>

#include "Utils/JwtKeys.h"

// The token the website gets from POST /account/login: HS256, signed with the MV_JWT key (the one that also signs staff
// tokens), but with iss "toymayhem-api" and aud "toymayhem-web". The staff endpoints refuse every token that has that
// audience, whatever its role, so a player who logs in on the website never gets near an admin function.
namespace Common
{
	namespace Utils
	{
		namespace WebToken
		{
			inline constexpr const char* issuer = "toymayhem-api";
			inline constexpr const char* audience = "toymayhem-web";
			inline constexpr std::chrono::seconds lifetime{ 3600 };

			struct Claims
			{
				std::uint32_t accountId{};
				std::int64_t role{};
			};

			inline std::string issue(std::uint32_t accountId, std::int64_t role, const std::string& key,
				std::chrono::system_clock::time_point now = std::chrono::system_clock::now())
			{
				return jwt::create()
					.set_type("JWT")
					.set_issuer(issuer)
					.set_audience(std::string{ audience })
					.set_subject(std::to_string(accountId))
					.set_payload_claim("role", jwt::claim(picojson::value(role)))
					.set_issued_at(now)
					.set_expires_at(now + lifetime)
					.sign(jwt::algorithm::hs256{ key });
			}

			// A token for the website's endpoints: signature (current key, or the previous one during a rotation), expiry, and
			// exactly this issuer, this audience, an exp claim, a numeric sub and an integer role. Anything else gives nothing,
			// so a staff token (no website audience) is not accepted here either.
			inline std::optional<Claims> verify(const std::string& token, const std::string& currentKey, const std::string& previousKey)
			{
				try
				{
					const auto decoded = jwt::decode(token);
					JwtKeys::verifyHs256(decoded, currentKey, previousKey);

					if (!decoded.has_issuer() || decoded.get_issuer() != issuer)
					{
						return std::nullopt;
					}
					if (!decoded.has_audience())
					{
						return std::nullopt;
					}
					const auto audiences = decoded.get_audience();
					if (audiences.size() != 1 || *audiences.begin() != audience)
					{
						return std::nullopt;
					}
					if (!decoded.has_expires_at() || !decoded.has_subject() || !decoded.has_payload_claim("role"))
					{
						return std::nullopt;
					}

					const std::string subject = decoded.get_subject();
					if (subject.empty() || subject.size() > 10 || subject.find_first_not_of("0123456789") != std::string::npos)
					{
						return std::nullopt;
					}
					const unsigned long long id = std::stoull(subject);
					if (id == 0 || id > 0xFFFFFFFFull)
					{
						return std::nullopt;
					}

					Claims claims;
					claims.accountId = static_cast<std::uint32_t>(id);
					claims.role = decoded.get_payload_claim("role").as_integer();
					return claims;
				}
				catch (const std::exception&)
				{
					return std::nullopt;
				}
			}

			// False for every token that was issued for the website. A malformed audience counts as the website's too (the
			// caller treats an exception as a refusal).
			template <typename DecodedJwt>
			bool staffTokenAllowed(const DecodedJwt& decoded)
			{
				if (!decoded.has_audience())
				{
					return true;
				}
				for (const auto& aud : decoded.get_audience())
				{
					if (aud == audience)
					{
						return false;
					}
				}
				return true;
			}
		}
	}
}

#endif
