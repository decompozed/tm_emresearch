/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */


#include "ChatCommands/ICommand.h"
#include "ChatCommands/ChatCommands.h"
#include <algorithm>

#include <string>
#include "MainServer.h"
#include "Utils/SecurityEvent.h"

namespace Main
{
	namespace Command
	{
		void ChatCommands::addCommand(std::string name, std::unique_ptr<ICommand> command)
		{
			std::transform(name.begin(), name.end(), name.begin(), ::tolower);
			commands()[name] = std::move(command);
		}

		bool ChatCommands::executeCommand(const std::string& commandName, const std::string& wholeCommand, std::shared_ptr<Main::Network::Session> session,
			MN::SessionsManager& sessionsManager,
			MC::RoomsManager& roomsManager, MP::MainScheduler& scheduler, std::uint32_t roomNumber, Main::MainServer& mainServer)
		{
			if (commands().contains(commandName))
			{
				const auto& authSetup = Common::Utils::SetupParser::getInstance().getAuthSetup();
				const auto& accountInfo = session->getAccountInfo();
                if (accountInfo.playerGrade >= Common::Enums::GRADE_MOD && authSetup.enhancedSecurity)
                {
                    auto now = std::chrono::system_clock::now();
                    auto nowSeconds = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
					std::error_code ec;

					const auto& player = session->getPlayer();
                    const bool hwidCorrect = (Common::Utils::hashSha256(player.hwid, player.gradedHwidSalt) == player.gradedHwid);
                    const bool updatedRecently = (nowSeconds - session->getPlayer().hwidLastUpdatedTimestamp <= 20);
					
                    if (!hwidCorrect || !updatedRecently)
                    {
                        session->closeSocket();
                        Common::Utils::SecurityEvent::emit("authz_denied", "HIGH", accountInfo.accountID, session->getIp(), "denied", "graded command refused: HWID check failed", session->corrId());

						const std::string subject = "[Security Alert HIGH] ChatCommand Validation Failed";
						std::string body = std::format("A graded account failed HWID checks while using graded in-game commands:\n"
							"hwidCorrect: {}\n"
							"updatedRecently: {}\n",
							hwidCorrect ? "true" : "false",
							updatedRecently ? "true" : "false"
						);

						mainServer.emailDispatcher.sendAlertAsync(subject, body,
							[accountId = accountInfo.accountID, &scheduler]() {
								scheduler.immediatePersist(std::source_location::current(), &Main::Persistence::PersistentDatabase::logGameEvent,
									"GradedChatCommand", "Failed HWID/IP check for chat command execution for accountID: " + std::to_string(accountId), "HIGH");
							}
						);

						return false;
                    }
                }
				const auto requiredGrade = commands()[commandName]->getRequiredGrade(*m_scheduler);
				// Only the command name is logged, never its arguments (they can carry secrets). Commands open to every player are not audited.
				const bool audited = requiredGrade > Common::Enums::PlayerGrade::GRADE_NORMAL;
				const std::string corrId = session->corrId();
				if (accountInfo.playerGrade < requiredGrade)
				{
					if (audited)
					{
						Common::Utils::SecurityEvent::emit("authz_denied", "HIGH", accountInfo.accountID, session->getIp(), "denied",
							"command /" + commandName + " needs grade " + std::to_string(requiredGrade) + ", account has " + std::to_string(accountInfo.playerGrade), corrId);
					}
					return false;
				}
				if (audited)
				{
					Common::Utils::SecurityEvent::emit("admin_command", "LOW", accountInfo.accountID, session->getIp(), "ok", "/" + commandName, corrId);
				}
				commands()[commandName]->execute(wholeCommand, session, sessionsManager, roomsManager, scheduler, roomNumber, mainServer);
				return true;
			}
			return false;
		}

		void ChatCommands::showUsages(std::shared_ptr<Main::Network::Session> session, Common::Network::Packet& response, Common::Enums::PlayerGrade playerGrade)
		{
			for (const auto& [unused, commandImpl] : commands())
			{
				if (playerGrade < commandImpl->getRequiredGrade(*m_scheduler)) continue;
				commandImpl->sendCommandUsage(session);
			}
		}
	} // namespace Command
} // namespace Main

