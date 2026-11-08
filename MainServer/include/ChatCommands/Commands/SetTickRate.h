#ifndef COMMAND_SET_TICK_RATE_H
#define COMMAND_SET_TICK_RATE_H

#include <memory>
#include <source_location>
#include <string>
#include "ChatCommands/ChatCommands.h"
#include "ChatCommands/ICommand.h"
#include "Detail/IpcUtils.h"
#include "HaveApi/MessageWriter.h"
#include "HaveApi/Messages.h"
#include "HaveApi/Sender.h"
#include "HaveApi/TickRate.h"
#include "MainServer.h"
#include "Utils/Constants.h"

namespace Main
{
	namespace Command
	{
		class SetTickRate final : public ICommand
		{
		private:
			std::uint32_t m_rate{};

			bool parseCommand(const std::string& providedCommand) override
			{
				std::smatch match;
				if (!std::regex_match(providedCommand, match, this->m_pattern))
				{
					return false;
				}
				try
				{
					m_rate = static_cast<std::uint32_t>(std::stoul(match[1].str()));
				}
				catch (...)
				{
					return false;
				}
				return true;
			}

		public:
			explicit SetTickRate(const Common::Enums::PlayerGrade requiredGrade)
				: ICommand{ requiredGrade, "/settickrate <N 5..60>", R"(^\S+\s+(\d{1,3})\s*$)" }
			{
			}

			void execute(const std::string& command, std::shared_ptr<Main::Network::Session> session, MN::SessionsManager& sessionsManager,
				MC::RoomsManager&, MP::MainScheduler& scheduler, std::uint32_t, Main::MainServer&) override
			{
				if (!parseCommand(command))
				{
					session->sendMessage("usage: " + m_commandDescription);
					return;
				}
				if (!Common::HaveApi::isValidTickRate(m_rate))
				{
					session->sendMessage("error: the tick rate must be between " + std::to_string(Common::HaveApi::minTickRate) + " and " +
						std::to_string(Common::HaveApi::maxTickRate));
					return;
				}
				const bool saved = scheduler.immediatePersist(std::source_location::current(),
					&Main::Persistence::PersistentDatabase::setServerSetting, std::string{ "TickRate" }, static_cast<std::int32_t>(m_rate));
				if (!saved)
				{
					session->sendMessage("error: the tick rate could not be saved");
					return;
				}
				namespace CH = Common::HaveApi;
				CH::currentTickRate = m_rate;
				Main::Ipc::M2C_sendServerCommand(Common::Constants::M2C_tickRate, static_cast<std::uint32_t>(session->getId()),
					static_cast<std::uint8_t>(m_rate));
				CH::UdpSessionPayload tick{};
				tick.tickRate = static_cast<std::uint8_t>(m_rate);
				tick.reroutingEnabled = 0;
				const auto message = CH::encode(CH::MessageId::UdpSession, tick);
				for (auto& [id, player] : sessionsManager.getAllSessions())
				{
					if (player) Main::HaveApi::sendToPlayer(*player, message);
				}
				::Utils::Logger::log("[HaveApi] /settickrate " + std::to_string(m_rate) + " by " + std::string{ Main::HaveApi::playerName(*session) },
					::Utils::LogType::Info, "Command::SetTickRate");
				session->sendMessage("Tick rate " + std::to_string(m_rate) + " saved and applied to every online player");
			}
		};

		REGISTER_CMD(SetTickRate, Common::Enums::PlayerGrade::GRADE_TESTER);
	}
}

#endif
