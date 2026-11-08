#ifndef COMMON_UTILS_PASSWORD_HASH_H
#define COMMON_UTILS_PASSWORD_HASH_H

#include <cstddef>
#include <cstdint>
#include <exception>
#include <optional>
#include <string>
#include <string_view>

#include <sodium.h>
#include <libbcrypt/include/bcrypt/BCrypt.hpp>

// Password storage: Argon2id over an HMAC with a server-side pepper. Old bcrypt hashes stay valid and are replaced
// after the next successful login. Stored form: p<pepper version>$argon2id$v=19$m=...,t=...,p=1$<salt>$<hash>
namespace Common
{
	namespace Utils
	{
		namespace PasswordHash
		{
			inline constexpr std::size_t pepperBytes = 32;
			inline constexpr unsigned long long opsLimit = 3;
			inline constexpr std::size_t memLimit = 64 * 1024 * 1024;

			struct Pepper
			{
				std::uint32_t version{};
				std::string key{};
			};

			struct Peppers
			{
				std::optional<Pepper> current{};
				std::optional<Pepper> previous{}; // only used to verify, while passwords move to the current pepper
			};

			struct Parsed
			{
				std::uint32_t pepperVersion{};
				std::string argon2{};
			};

			inline bool ready()
			{
				static const bool initialized = sodium_init() >= 0;
				return initialized;
			}

			inline bool isBcrypt(std::string_view stored)
			{
				return stored.starts_with("$2");
			}

			inline std::optional<Parsed> parse(std::string_view stored)
			{
				const std::size_t dollar = stored.find('$');
				if (!stored.starts_with('p') || dollar == std::string_view::npos || dollar == 1 || dollar > 10
					|| !stored.substr(dollar).starts_with("$argon2id$"))
				{
					return std::nullopt;
				}

				std::uint32_t version = 0;
				for (const char c : stored.substr(1, dollar - 1))
				{
					if (c < '0' || c > '9')
					{
						return std::nullopt;
					}
					version = version * 10 + static_cast<std::uint32_t>(c - '0');
				}
				return Parsed{ version, std::string(stored.substr(dollar)) };
			}

			// The fixed-size HMAC is what gets hashed: the pepper is mixed in, and the password length no longer matters
			inline void applyPepper(unsigned char (&out)[crypto_auth_hmacsha256_BYTES], const std::string& password, const std::string& key)
			{
				crypto_auth_hmacsha256_state state;
				crypto_auth_hmacsha256_init(&state, reinterpret_cast<const unsigned char*>(key.data()), key.size());
				crypto_auth_hmacsha256_update(&state, reinterpret_cast<const unsigned char*>(password.data()), password.size());
				crypto_auth_hmacsha256_final(&state, out);
				sodium_memzero(&state, sizeof(state));
			}

			inline std::optional<std::string> hash(const std::string& password, const Pepper& pepper)
			{
				if (!ready())
				{
					return std::nullopt;
				}

				unsigned char input[crypto_auth_hmacsha256_BYTES];
				applyPepper(input, password, pepper.key);

				char output[crypto_pwhash_STRBYTES];
				const int result = crypto_pwhash_str(output, reinterpret_cast<const char*>(input), sizeof(input), opsLimit, memLimit);
				sodium_memzero(input, sizeof(input));
				if (result != 0)
				{
					return std::nullopt;
				}
				return "p" + std::to_string(pepper.version) + output;
			}

			inline bool verifyBcrypt(const std::string& password, std::string stored)
			{
				try
				{
					if (BCrypt::validatePassword(password, stored))
					{
						return true;
					}

					if (stored.substr(0, 4) == "$2b$")
					{
						stored[2] = 'a';
						return BCrypt::validatePassword(password, stored);
					}
				}
				catch (const std::exception&)
				{
				}
				return false;
			}

			inline bool verify(const std::string& password, const std::string& stored, const Peppers& peppers)
			{
				if (isBcrypt(stored))
				{
					return verifyBcrypt(password, stored);
				}

				const auto parsed = parse(stored);
				if (!parsed || !ready())
				{
					return false;
				}

				const Pepper* pepper = nullptr;
				if (peppers.current && peppers.current->version == parsed->pepperVersion)
				{
					pepper = &*peppers.current;
				}
				else if (peppers.previous && peppers.previous->version == parsed->pepperVersion)
				{
					pepper = &*peppers.previous;
				}
				if (!pepper)
				{
					return false;
				}

				unsigned char input[crypto_auth_hmacsha256_BYTES];
				applyPepper(input, password, pepper->key);
				const int result = crypto_pwhash_str_verify(parsed->argon2.c_str(), reinterpret_cast<const char*>(input), sizeof(input));
				sodium_memzero(input, sizeof(input));
				return result == 0;
			}

			// Decides from the stored value alone; only call it after the password was verified
			inline bool needsUpgrade(const std::string& stored, const Peppers& peppers)
			{
				if (!peppers.current)
				{
					return false;
				}

				if (isBcrypt(stored))
				{
					return true;
				}

				const auto parsed = parse(stored);
				return parsed && ready() && (parsed->pepperVersion != peppers.current->version
					|| crypto_pwhash_str_needs_rehash(parsed->argon2.c_str(), opsLimit, memLimit) != 0);
			}
		}
	}
}

#endif
