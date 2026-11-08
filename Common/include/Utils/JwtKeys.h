#ifndef COMMON_UTILS_JWT_KEYS_H
#define COMMON_UTILS_JWT_KEYS_H

#include <exception>
#include <string>

#include <jwt-cpp/jwt.h>

// HS256 verification with a key rotation overlap: a token is accepted when it was signed with the current key or, if one
// is configured, with the previous key. New tokens are always signed with the current key (by the website that issues
// them), so removing the previous key ends the overlap. An empty previous key means "none": it is never used to verify.
namespace Common
{
	namespace Utils
	{
		namespace JwtKeys
		{
			// Throws like jwt::verifier::verify when neither key accepts the token (the error of the current key is the one thrown)
			template <typename DecodedJwt>
			void verifyHs256(const DecodedJwt& decoded, const std::string& currentKey, const std::string& previousKey)
			{
				std::exception_ptr currentError;
				try
				{
					jwt::verify().allow_algorithm(jwt::algorithm::hs256{ currentKey }).verify(decoded);
					return;
				}
				catch (const std::exception&)
				{
					currentError = std::current_exception();
				}
				if (previousKey.empty() || previousKey == currentKey)
				{
					std::rethrow_exception(currentError);
				}
				try
				{
					jwt::verify().allow_algorithm(jwt::algorithm::hs256{ previousKey }).verify(decoded);
				}
				catch (const std::exception&)
				{
					std::rethrow_exception(currentError);
				}
			}
		}
	}
}

#endif
