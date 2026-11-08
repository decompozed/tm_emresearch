#ifndef MAIN_TWO_FACTOR_HELPERS_H
#define MAIN_TWO_FACTOR_HELPERS_H

#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <source_location>
#include <string>

#include <libbcrypt/include/bcrypt/BCrypt.hpp>

#include "Persistence/MainScheduler.h"
#include "Utils/SetupParser.h"
#include "Utils/Totp.h"
#include "Utils/Utils.h"

// Used by /twofa, with the same replay protection as the login
namespace Main
{
	namespace Command
	{
		namespace TwoFactor
		{
			enum class CheckResult { Valid, Invalid, Replayed };

			inline bool isEnabled(Main::Persistence::MainScheduler& scheduler, std::uint32_t accountId)
			{
				const auto flag = scheduler.immediatePersist(std::source_location::current(),
					&Main::Persistence::PersistentDatabase::getColumnByAid, "TwoFactorEnabled", accountId);
				return flag.has_value() && *flag == "1";
			}

			inline CheckResult checkTotp(Main::Persistence::MainScheduler& scheduler, std::uint32_t accountId, const std::string& encryptedSecret, const std::string& code)
			{
				const auto secret = Common::Utils::decrypt2FASecret(encryptedSecret, Common::Utils::SetupParser::getInstance().getGeneralSetup().twoFaSecret);
				const auto lastStep = scheduler.immediatePersist(std::source_location::current(),
					&Main::Persistence::PersistentDatabase::getColumnByAid, "TotpLastStep", accountId);
				if (!secret || !lastStep)
				{
					return CheckResult::Invalid;
				}

				const auto match = Common::Utils::Totp::matchCode(*secret, code, std::time(nullptr), std::strtoull(lastStep->c_str(), nullptr, 10));
				if (match.status == Common::Utils::Totp::MatchStatus::Invalid)
				{
					return CheckResult::Invalid;
				}

				if (match.status == Common::Utils::Totp::MatchStatus::Replayed
					|| !scheduler.immediatePersist(std::source_location::current(), &Main::Persistence::PersistentDatabase::consumeTotpStep, accountId, match.step))
				{
					return CheckResult::Replayed;
				}
				return CheckResult::Valid;
			}

			inline CheckResult checkRecoveryCode(Main::Persistence::MainScheduler& scheduler, std::uint32_t accountId, const std::string& code)
			{
				const auto rows = scheduler.immediatePersist(std::source_location::current(), &Main::Persistence::PersistentDatabase::getUnusedRecoveryCodes, accountId);
				for (const auto& [id, hash] : rows)
				{
					if (BCrypt::validatePassword(code, hash))
					{
						return scheduler.immediatePersist(std::source_location::current(), &Main::Persistence::PersistentDatabase::consumeRecoveryCode, id)
							? CheckResult::Valid : CheckResult::Replayed;
					}
				}
				return CheckResult::Invalid;
			}
		}
	}
}

#endif
