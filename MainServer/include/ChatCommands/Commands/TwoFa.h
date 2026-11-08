#ifndef TWO_FA_COMMAND_HEADER
#define TWO_FA_COMMAND_HEADER

#include "ChatCommands/ICommand.h"
#include "ChatCommands/ChatCommands.h"
#include "ChatCommands/TwoFactorHelpers.h"
#include "Utils/Utils.h"
#include "Utils/Totp.h"
#include "MainServer.h"
#include <source_location>
#include <sstream>
#include <vector>
#include <libbcrypt/include/bcrypt/BCrypt.hpp>

namespace Main
{
	namespace Command
	{
		class TwoFa final : public ICommand
		{
		private:
			static constexpr std::uint32_t maxAttempts = 5;

			using Database = Main::Persistence::PersistentDatabase;

			static bool validatePassword(const std::string& plain, std::string hash)
			{
				if (BCrypt::validatePassword(plain, hash))
					return true;

				if (hash.substr(0, 4) == "$2b$")
				{
					hash[2] = 'a';
					return BCrypt::validatePassword(plain, hash);
				}
				return false;
			}

			static std::vector<std::string> hashRecoveryCodes(const std::vector<std::string>& codes)
			{
				std::vector<std::string> hashes;
				hashes.reserve(codes.size());
				for (const auto& code : codes)
				{
					hashes.push_back(BCrypt::generateHash(code, Common::Utils::Totp::recoveryCodeBcryptCost));
				}
				return hashes;
			}

			static void sendRecoveryCodes(std::shared_ptr<Main::Network::Session> session, const std::vector<std::string>& codes)
			{
				session->sendMessage("recovery codes (each works once, shown only now, save them):");
				for (std::size_t i = 0; i < codes.size(); i += 4)
				{
					std::string line;
					for (std::size_t j = i; j < (std::min)(i + 4, codes.size()); ++j)
					{
						line += codes[j] + "  ";
					}
					session->sendMessage(line);
				}
			}

			static void logEvent(MP::MainScheduler& scheduler, std::uint32_t accountId, const std::string& message, const std::string& severity)
			{
				scheduler.immediatePersist(std::source_location::current(), &Database::logGameEvent, "TwoFactor",
					message + " for account " + std::to_string(accountId), severity);
			}

			// A wrong password or code ends the session after a few tries, without touching the account
			static void rejectAttempt(std::shared_ptr<Main::Network::Session> session, MN::SessionsManager& sessionsManager, std::uint32_t& counter, const std::string& error)
			{
				if (++counter >= maxAttempts)
				{
					sessionsManager.removeSession(session->getAccountInfo().uniqueId.session);
					return;
				}
				session->sendMessage(error);
			}

			static bool checkPassword(std::shared_ptr<Main::Network::Session> session, MN::SessionsManager& sessionsManager, MP::MainScheduler& scheduler, const std::string& password)
			{
				const auto hashed = scheduler.immediatePersist(std::source_location::current(), &Database::getColumnByAid, "Password", session->getAccountInfo().accountID);
				if (hashed.has_value() && validatePassword(password, *hashed))
				{
					return true;
				}
				rejectAttempt(session, sessionsManager, session->getPlayer().totalWrongPasswordReset, "error: password is incorrect");
				return false;
			}

			static TwoFactor::CheckResult checkCode(MP::MainScheduler& scheduler, std::uint32_t accountId, const std::string& rawCode, bool allowRecovery)
			{
				const std::string code = Common::Utils::Totp::toUpper(rawCode);
				if (allowRecovery && Common::Utils::Totp::isRecoveryCode(code))
				{
					return TwoFactor::checkRecoveryCode(scheduler, accountId, code);
				}

				const auto encryptedSecret = scheduler.immediatePersist(std::source_location::current(), &Database::getColumnByAid, "Secret", accountId);
				return encryptedSecret.has_value() ? TwoFactor::checkTotp(scheduler, accountId, *encryptedSecret, code) : TwoFactor::CheckResult::Invalid;
			}

			static bool passCode(std::shared_ptr<Main::Network::Session> session, MN::SessionsManager& sessionsManager, MP::MainScheduler& scheduler, const std::string& rawCode, bool allowRecovery)
			{
				const auto result = checkCode(scheduler, session->getAccountInfo().accountID, rawCode, allowRecovery);
				if (result == TwoFactor::CheckResult::Valid)
				{
					return true;
				}

				if (result == TwoFactor::CheckResult::Replayed)
				{
					session->sendMessage("error: this code was already used, wait for the next one");
					return false;
				}
				rejectAttempt(session, sessionsManager, session->getPlayer().totalWrong2FaReset, "error: invalid 2FA code");
				return false;
			}

			static void status(std::shared_ptr<Main::Network::Session> session, MP::MainScheduler& scheduler, bool active)
			{
				if (!active)
				{
					session->sendMessage("2FA is off. Turn it on with: /twofa setup <Password>");
					return;
				}

				const auto rows = scheduler.immediatePersist(std::source_location::current(), &Database::getUnusedRecoveryCodes, session->getAccountInfo().accountID);
				session->sendMessage("2FA is on, recovery codes left: " + std::to_string(rows.size()));
			}

			static void setup(std::shared_ptr<Main::Network::Session> session, MN::SessionsManager& sessionsManager, MP::MainScheduler& scheduler, const std::string& password)
			{
				if (!checkPassword(session, sessionsManager, scheduler, password))
				{
					return;
				}

				auto& player = session->getPlayer();
				player.pendingTwoFaSecret = Common::Utils::Totp::generateSecret();
				player.pendingTwoFaSince = std::time(nullptr);

				session->sendMessage("add this key to your authenticator app (time based): " + player.pendingTwoFaSecret);
				session->sendMessage("then finish with: /twofa confirm <6-digit code> (within 10 minutes)");
			}

			static void confirm(std::shared_ptr<Main::Network::Session> session, MP::MainScheduler& scheduler, const std::string& code)
			{
				auto& player = session->getPlayer();
				if (player.pendingTwoFaSecret.empty() || std::time(nullptr) - player.pendingTwoFaSince > Common::Utils::Totp::enrolmentSeconds)
				{
					session->sendMessage("error: no 2FA setup in progress, start with: /twofa setup <Password>");
					return;
				}

				const auto match = Common::Utils::Totp::matchCode(player.pendingTwoFaSecret, code, std::time(nullptr), 0);
				if (match.status != Common::Utils::Totp::MatchStatus::Valid)
				{
					session->sendMessage("error: invalid 2FA code, check the key and the clock of your phone");
					return;
				}

				const auto encryptedSecret = Common::Utils::encrypt2FASecret(player.pendingTwoFaSecret, Common::Utils::SetupParser::getInstance().getGeneralSetup().twoFaSecret);
				if (!encryptedSecret.has_value())
				{
					session->sendMessage("error: failed to encrypt 2FA secret");
					return;
				}

				const auto codes = Common::Utils::Totp::generateRecoveryCodes();
				const std::uint32_t accountId = session->getAccountInfo().accountID;
				if (!scheduler.immediatePersist(std::source_location::current(), &Database::enableTwoFactor, accountId, *encryptedSecret, match.step, hashRecoveryCodes(codes)))
				{
					session->sendMessage("error: failed to save 2FA settings");
					return;
				}

				player.pendingTwoFaSecret.clear();
				logEvent(scheduler, accountId, "2FA enabled", "LOW");
				session->sendMessage("success: 2FA is on. From now on log in with username+code as the username");
				sendRecoveryCodes(session, codes);
			}

			static void regenerateRecovery(std::shared_ptr<Main::Network::Session> session, MN::SessionsManager& sessionsManager, MP::MainScheduler& scheduler, const std::string& code)
			{
				if (!passCode(session, sessionsManager, scheduler, code, false))
				{
					return;
				}

				const auto codes = Common::Utils::Totp::generateRecoveryCodes();
				const std::uint32_t accountId = session->getAccountInfo().accountID;
				if (!scheduler.immediatePersist(std::source_location::current(), &Database::replaceRecoveryCodes, accountId, hashRecoveryCodes(codes)))
				{
					session->sendMessage("error: failed to save the recovery codes");
					return;
				}

				logEvent(scheduler, accountId, "Recovery codes regenerated", "MEDIUM");
				sendRecoveryCodes(session, codes);
			}

			static void disable(std::shared_ptr<Main::Network::Session> session, MN::SessionsManager& sessionsManager, MP::MainScheduler& scheduler,
				const std::string& password, const std::string& code)
			{
				if (!checkPassword(session, sessionsManager, scheduler, password) || !passCode(session, sessionsManager, scheduler, code, true))
				{
					return;
				}

				const std::uint32_t accountId = session->getAccountInfo().accountID;
				if (!scheduler.immediatePersist(std::source_location::current(), &Database::disableTwoFactor, accountId))
				{
					session->sendMessage("error: failed to turn off 2FA");
					return;
				}

				logEvent(scheduler, accountId, "2FA disabled", "HIGH");
				session->sendMessage("success: 2FA is off");
			}

		public:
			explicit TwoFa(const Common::Enums::PlayerGrade requiredGrade)
				: ICommand{ requiredGrade, "/twofa status | setup <Password> | confirm <Code> | recovery <Code> | disable <Password> <Code>" }
			{
			}

			void execute(const std::string& command, std::shared_ptr<Main::Network::Session> session, MN::SessionsManager& sessionsManager,
				MC::RoomsManager&, MP::MainScheduler& scheduler, std::uint32_t, Main::MainServer&) override
			{
				if (Common::Utils::SetupParser::getInstance().getGeneralSetup().twoFaSecret.empty())
				{
					session->sendMessage("error: 2FA is not available on this server");
					return;
				}

				std::istringstream stream(command);
				std::vector<std::string> args;
				for (std::string word; stream >> word;)
				{
					args.push_back(word);
				}
				const std::string action = args.size() > 1 ? args[1] : "";

				const auto& ainfo = session->getAccountInfo();
				const bool enhancedSecurity = Common::Utils::SetupParser::getInstance().getAuthSetup().enhancedSecurity;
				const bool managedStaff = enhancedSecurity && ainfo.playerGrade >= Common::Enums::PlayerGrade::GRADE_MOD;
				const bool active = managedStaff || TwoFactor::isEnabled(scheduler, ainfo.accountID);

				if (action == "status" && args.size() == 2)
				{
					status(session, scheduler, active);
				}
				else if (action == "setup" && args.size() == 3)
				{
					active ? session->sendMessage("error: 2FA is already on") : setup(session, sessionsManager, scheduler, args[2]);
				}
				else if (action == "confirm" && args.size() == 3)
				{
					active ? session->sendMessage("error: 2FA is already on") : confirm(session, scheduler, args[2]);
				}
				else if (action == "recovery" && args.size() == 3)
				{
					active ? regenerateRecovery(session, sessionsManager, scheduler, args[2]) : session->sendMessage("error: 2FA is off");
				}
				else if (action == "disable" && args.size() == 4)
				{
					if (managedStaff)
					{
						session->sendMessage("error: 2FA is mandatory for staff accounts");
					}
					else
					{
						active ? disable(session, sessionsManager, scheduler, args[2], args[3]) : session->sendMessage("error: 2FA is off");
					}
				}
				else
				{
					sendCommandUsage(session);
				}
			}
		};

		REGISTER_CMD(TwoFa, Common::Enums::PlayerGrade::GRADE_NORMAL)
	};
}

#endif
