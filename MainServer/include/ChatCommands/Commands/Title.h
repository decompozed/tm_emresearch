#ifndef COMMAND_TITLE_H
#define COMMAND_TITLE_H

#include <source_location>
#include <string>
#include "ChatCommands/ChatCommands.h"
#include "ChatCommands/ICommand.h"
#include "HaveApi/Profile.h"
#include "MainServer.h"

namespace Main
{
	namespace Command
	{
		// /title <text>: the title shown in the player's info dialog (have_api profile, Users.Title); letters and single
		// spaces, at most Main::HaveApi::maxTitleLength characters. /title alone removes it. Every player.
		class Title final : public ICommand
		{
		private:
			std::string m_title;

			bool parseCommand(const std::string& providedCommand) override
			{
				std::smatch match;
				if (!std::regex_match(providedCommand, match, this->m_pattern))
				{
					return false;
				}
				m_title = match[1].matched ? match[1].str() : std::string{};
				return true;
			}

		public:
			explicit Title(const Common::Enums::PlayerGrade requiredGrade)
				: ICommand{ requiredGrade, "/title <text> (letters and spaces, at most 20; /title alone removes it)", R"(^\S+(?:\s+(.*\S))?\s*$)" }
			{
			}

			void execute(const std::string& command, std::shared_ptr<Main::Network::Session> session, MN::SessionsManager&, MC::RoomsManager&,
				MP::MainScheduler& scheduler, std::uint32_t, Main::MainServer&) override
			{
				if (!parseCommand(command))
				{
					session->sendMessage("usage: " + m_commandDescription);
					return;
				}
				if (!Main::HaveApi::isValidTitle(m_title))
				{
					session->sendMessage("error: the title can only contain letters and single spaces, at most " +
						std::to_string(Main::HaveApi::maxTitleLength) + " characters");
					return;
				}
				const bool saved = scheduler.immediatePersist(std::source_location::current(),
					&Main::Persistence::PersistentDatabase::updateTitle, session->getAccountInfo().accountID, m_title);
				if (!saved)
				{
					session->sendMessage("error: the title could not be saved");
					return;
				}
				session->getPlayer().title = m_title;
				Main::HaveApi::sendProfile(*session, *session, true);
				session->sendMessage(m_title.empty() ? "success: your title has been removed" : "success: your title is now " + m_title);
			}
		};

		REGISTER_CMD(Title, Common::Enums::PlayerGrade::GRADE_NORMAL);
	}
}

#endif
