#ifndef STATUS_TEXT_COMMAND_H
#define STATUS_TEXT_COMMAND_H

#include "ChatCommands/ICommand.h"
#include "ChatCommands/ChatCommands.h"
#include "HaveApi/MessageWriter.h"
#include "HaveApi/Sender.h"
#include "MainServer.h"

namespace Main
{
	namespace Command
	{
		// /statustext <text>: shows <text> in the caller's have_api status line (message 0x02).
		struct StatusText final : public ICommand
		{
			explicit StatusText(const Common::Enums::PlayerGrade requiredGrade)
				: ICommand{ requiredGrade, "/statustext <text>", R"(^\S+\s+(.+)$)" }
			{
			}

			bool parseCommand(const std::string& providedCommand) override
			{
				std::smatch match;
				if (!std::regex_match(providedCommand, match, m_pattern))
				{
					return false;
				}
				m_text = match[1].str();
				return true;
			}

			void execute(const std::string& command, std::shared_ptr<Main::Network::Session> session, MN::SessionsManager&, MC::RoomsManager&,
				MP::MainScheduler&, std::uint32_t, Main::MainServer&) override
			{
				if (!parseCommand(command))
				{
					session->sendMessage("usage: /statustext <text>");
					return;
				}
				const auto message = Common::HaveApi::encodeText(Common::HaveApi::MessageId::StatusText, m_text);
				if (!Main::HaveApi::sendToPlayer(*session, message))
				{
					session->sendMessage("error: the status text could not be sent");
				}
			}

		private:
			std::string m_text;
		};

		REGISTER_CMD(StatusText, Common::Enums::PlayerGrade::GRADE_TESTER)
	}
}

#endif
