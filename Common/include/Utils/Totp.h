#ifndef COMMON_UTILS_TOTP_H
#define COMMON_UTILS_TOTP_H

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <ctime>
#include <exception>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <cryptopp/osrng.h>
#include <cryptopp/misc.h>
#include <libcppotp/otp.h>

// Time-based one-time passwords (RFC 6238) and the single-use recovery codes that go with them.
// Only the logic lives here, so it can be tested without a database: storing the last used step and the
// recovery code hashes is done by the callers, with atomic UPDATE statements.
namespace Common
{
	namespace Utils
	{
		namespace Totp
		{
			inline constexpr std::int64_t stepSeconds = 30;
			inline constexpr std::int64_t driftSteps = 1;           // one step before and after, for phones whose clock is slightly off
			inline constexpr std::size_t codeDigits = 6;
			inline constexpr std::size_t secretBytes = 20;          // 160 bits, the length RFC 4226 recommends
			inline constexpr std::size_t recoveryCodeCount = 8;
			inline constexpr std::size_t recoveryCodeLength = 8;    // 40 bits; short enough for the client's username box
			inline constexpr int recoveryCodeBcryptCost = 10;
			inline constexpr std::uint32_t maxWrongCodesPlayer = 5; // then the second factor is locked for playerLockSeconds
			inline constexpr std::uint64_t playerLockSeconds = 15 * 60;
			inline constexpr std::time_t enrolmentSeconds = 10 * 60;

			inline constexpr std::string_view base32Alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";

			// Run by the Auth and Main servers at start-up (whichever starts first creates them)
			inline constexpr std::array<const char*, 6> schemaStatements{
				"ALTER TABLE Users ADD COLUMN IF NOT EXISTS TwoFactorEnabled TINYINT(1) NOT NULL DEFAULT 0",
				"ALTER TABLE Users ADD COLUMN IF NOT EXISTS TotpLastStep BIGINT UNSIGNED NOT NULL DEFAULT 0",
				"ALTER TABLE Users ADD COLUMN IF NOT EXISTS FailedPasswordCount INT UNSIGNED NOT NULL DEFAULT 0",
				"ALTER TABLE Users ADD COLUMN IF NOT EXISTS Failed2faCount INT UNSIGNED NOT NULL DEFAULT 0",
				"ALTER TABLE Users ADD COLUMN IF NOT EXISTS TwoFactorLockedUntil BIGINT UNSIGNED NOT NULL DEFAULT 0",
				R"(CREATE TABLE IF NOT EXISTS TwoFactorRecoveryCodes (ID INT UNSIGNED AUTO_INCREMENT PRIMARY KEY, AccountID INT NOT NULL,
					CodeHash VARCHAR(255) NOT NULL, UsedAt DATETIME NULL DEFAULT NULL, INDEX idx_recovery_account (AccountID)))"
			};

			enum class MatchStatus { Valid, Replayed, Invalid };

			struct MatchResult
			{
				MatchStatus status{ MatchStatus::Invalid };
				std::uint64_t step{};
			};

			inline std::uint64_t stepForTime(std::time_t now)
			{
				return static_cast<std::uint64_t>(now) / static_cast<std::uint64_t>(stepSeconds);
			}

			inline std::string randomBase32(std::size_t length)
			{
				CryptoPP::AutoSeededRandomPool rng;
				std::string result(length, '\0');
				for (char& c : result)
				{
					// 32 symbols: the low 5 bits of a random byte select one without bias
					c = base32Alphabet[rng.GenerateByte() & 0x1F];
				}
				return result;
			}

			inline std::string generateSecret()
			{
				return randomBase32(secretBytes * 8 / 5);
			}

			inline std::string generateRecoveryCode()
			{
				return randomBase32(recoveryCodeLength);
			}

			inline std::vector<std::string> generateRecoveryCodes()
			{
				std::vector<std::string> codes;
				codes.reserve(recoveryCodeCount);
				while (codes.size() < recoveryCodeCount)
				{
					std::string code = generateRecoveryCode();
					if (std::find(codes.begin(), codes.end(), code) == codes.end())
					{
						codes.push_back(std::move(code));
					}
				}
				return codes;
			}

			inline bool isTotpCode(std::string_view code)
			{
				return code.size() == codeDigits
					&& std::all_of(code.begin(), code.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
			}

			inline bool isBase32(std::string_view text)
			{
				return !text.empty() && text.find_first_not_of(base32Alphabet) == std::string_view::npos;
			}

			inline bool isRecoveryCode(std::string_view code)
			{
				return code.size() == recoveryCodeLength && isBase32(code);
			}

			inline std::string toUpper(std::string text)
			{
				std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
				return text;
			}

			// The code of one time step, without leading zeros; digits can be raised to check the RFC 6238 vectors
			inline std::optional<std::uint32_t> codeForStep(const std::string& secret, std::uint64_t step, std::size_t digits = codeDigits)
			{
				if (!isBase32(secret))
				{
					return std::nullopt;
				}

				try
				{
					return CppTotp::hotp(CppTotp::Bytes::fromUnpaddedBase32(secret), step, digits);
				}
				catch (const std::exception&)
				{
					return std::nullopt;
				}
			}

			inline std::string formatCode(std::uint32_t code)
			{
				std::string text = std::to_string(code);
				return std::string(codeDigits - (std::min)(codeDigits, text.size()), '0') + text;
			}

			// Accepts a code from the current step or its direct neighbours, and only from a step newer than
			// lastUsedStep. Every step is always compared so the time taken does not depend on which one matched.
			inline MatchResult matchCode(const std::string& secret, const std::string& code, std::time_t now, std::uint64_t lastUsedStep)
			{
				MatchResult result{};
				if (!isTotpCode(code))
				{
					return result;
				}

				const std::uint64_t current = stepForTime(now);
				for (std::int64_t offset = -driftSteps; offset <= driftSteps; ++offset)
				{
					const std::uint64_t step = current + static_cast<std::uint64_t>(offset);
					const auto expected = codeForStep(secret, step);
					if (!expected)
					{
						return MatchResult{};
					}

					const std::string expectedText = formatCode(*expected);
					if (!CryptoPP::VerifyBufsEqual(reinterpret_cast<const CryptoPP::byte*>(expectedText.data()),
						reinterpret_cast<const CryptoPP::byte*>(code.data()), codeDigits))
					{
						continue;
					}

					if (step > lastUsedStep)
					{
						result = MatchResult{ MatchStatus::Valid, step };
					}
					else if (result.status != MatchStatus::Valid)
					{
						result = MatchResult{ MatchStatus::Replayed, step };
					}
				}
				return result;
			}

			// Chat is written to the ChatLogs table before commands run. These commands carry passwords or codes (the
			// password ones were removed from the server, but a player can still type them), so only the name is kept.
			inline std::string redactCommandForLog(const std::string& message)
			{
				constexpr std::array<std::string_view, 3> secretCommands{ "/twofa", "/changepw", "/changeusername" };
				const std::size_t start = message.find_first_not_of(" \t");
				if (start == std::string::npos)
				{
					return message;
				}
				const std::string head = toUpper(message.substr(start, 16));
				for (const std::string_view command : secretCommands)
				{
					if (head.starts_with(toUpper(std::string{ command })))
					{
						return std::string{ command } + " [redacted]";
					}
				}
				return message;
			}
		}
	}
}

#endif
