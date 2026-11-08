#ifndef SHUTDOWN_SIMPLECOMMAND_HEADER
#define SHUTDOWN_SIMPLECOMMAND_HEADER

#include <asio.hpp>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <regex>
#include <string>
#include "ChatCommands/ICommand.h"
#include "ChatCommands/ChatCommands.h"
#include "MainServer.h"
#include "Utils/Logger.h"

namespace Main
{
	namespace Command
	{
		class Shutdown final : public ICommand
		{
		private:
			static constexpr std::uint32_t defaultDelaySeconds = 60;
			// the client disconnects by itself 180 s after 502 (0xAEC790)
			static constexpr std::uint32_t maxDelaySeconds = 180;
			static constexpr std::uint16_t shutdownNotice = 502;

			std::uint32_t m_delaySeconds{ defaultDelaySeconds };

			bool parseCommand(const std::string& providedCommand) override
			{
				std::smatch match;
				if (!std::regex_match(providedCommand, match, this->m_pattern))
				{
					return false;
				}
				try
				{
					m_delaySeconds = match[1].matched ? static_cast<std::uint32_t>(std::stoul(match[1].str())) : defaultDelaySeconds;
				}
				catch (...)
				{
					return false;
				}
				return m_delaySeconds <= maxDelaySeconds;
			}

		public:
			explicit Shutdown(const Common::Enums::PlayerGrade requiredGrade)
				: ICommand{ requiredGrade, "/shutdown [seconds 0..180, default 60]", R"(^\S+(?:\s+(\d{1,3}))?\s*$)" }
			{
			}

			void execute(const std::string& command, std::shared_ptr<Main::Network::Session> session, MN::SessionsManager& sessionsManager,
				MC::RoomsManager&, MP::MainScheduler&, std::uint32_t, Main::MainServer& server) override
			{
				if (!parseCommand(command))
				{
					session->sendMessage("usage: " + m_commandDescription);
					return;
				}

				Common::Network::Packet notice;
				notice.setCommand(shutdownNotice, 0, 0, 0);
				for (auto& [id, player] : sessionsManager.getAllSessions())
				{
					if (!player) continue;
					notice.setTcpHeader(player->getId(), Common::Enums::USER_LARGE_ENCRYPTION);
					player->asyncWrite(notice);
				}
				::Utils::Logger::log("/shutdown in " + std::to_string(m_delaySeconds) + " s", ::Utils::LogType::Info, "Command::Shutdown");
				session->sendMessage("Server shutting down in " + std::to_string(m_delaySeconds) + " seconds");

				auto timer = std::make_shared<asio::steady_timer>(server.getIoContext(), std::chrono::seconds(m_delaySeconds));
				timer->async_wait([timer, &sessionsManager](const asio::error_code& error)
					{
						if (error) return;
						for (auto& [id, player] : sessionsManager.getAllSessions())
						{
							if (!player) continue;
							player->persistNow();
							player->closeSocket();
						}
						::Utils::Logger::log("/shutdown: every player saved, exiting", ::Utils::LogType::Info, "Command::Shutdown");
						::Utils::Logger::flushNow();
						std::_Exit(EXIT_SUCCESS);
					});
			}
		};

		REGISTER_CMD(Shutdown, Common::Enums::PlayerGrade::GRADE_TESTER)
	}
}

#endif
