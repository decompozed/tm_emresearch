#ifndef ACCOUNT_INFO_HANDLER_MAIN_H
#define ACCOUNT_INFO_HANDLER_MAIN_H

#include "Network/Session.h"
#include "Network/Packet.h"
#include "MainEnums.h"
#include "Structures/AccountInfo/MainAccountInfo.h"
#include "Network/Sessions/MainSession.h"
#include "Network/MainSessionManager.h"
#include "Structures/AccountInfo/MuteInfo.h"
#include "HaveApi/Profile.h"
#include "HaveApi/TickRate.h"
#include "HaveApi/ChatColorMessages.h"
#include <source_location>

namespace Main
{
	namespace Handlers
	{
		inline void handleAccountInformation(const Common::Network::Packet& request, std::shared_ptr<Main::Network::Session> session,
			Main::Network::SessionsManager& sessionsManager, Main::Persistence::MainScheduler& scheduler, Main::Structures::AccountInfo& accountInfo, 
			std::uint64_t timeSinceLastServerRestart, std::uint32_t serverId, std::uint32_t extra = 1)
		{
			START_BENCHMARK

			if (extra == 59)
			{
				session->sendAccountInfoConfirmation();
				return;
			}

			if (auto foundSession = sessionsManager.getSessionByAccountId(accountInfo.accountID))
			{ // the session is already online, disonnect it 
				foundSession->closeSocket();
			}

			accountInfo.uniqueId.session = session->getId();
			accountInfo.uniqueId.server = serverId;
			accountInfo.serverTime = accountInfo.getUtcTimeMs() - timeSinceLastServerRestart;
			session->setAccountInfo(accountInfo);
			sessionsManager.addSession(session);

			const auto friends = scheduler.immediatePersist(std::source_location::current(),
				&Main::Persistence::PersistentDatabase::loadFriends, accountInfo.accountID);
			session->getPlayer().socialInfo.setFriendList(friends);
			for (const auto& currentFriend : friends)
			{
				if (auto targetSession = sessionsManager.getSessionByAccountId(currentFriend.targetAccountId))
				{ // This friend is online. Notify them that we're online
					session->updateFriendSession(targetSession);
					targetSession->updateFriendSession(session);
					targetSession->logFriend(Main::Enums::FriendLogType::LOGIN, accountInfo.accountID);
				}
			}

			
			session->getPlayer().socialInfo.setBlockedPlayers(scheduler.immediatePersist(std::source_location::current(),
				&Main::Persistence::PersistentDatabase::loadBlockedPlayers, accountInfo.accountID));
			session->setMute(scheduler.immediatePersist(std::source_location::current(), &Main::Persistence::PersistentDatabase::isMuted, accountInfo.accountID));
			if (scheduler.immediatePersist(std::source_location::current(), &Main::Persistence::PersistentDatabase::isRoomCreationDisabled, accountInfo.accountID))
			{
				session->getPlayer().moderationInfo.isRoomCreationEnabled = false;
			}
			if (scheduler.immediatePersist(std::source_location::current(), &Main::Persistence::PersistentDatabase::isVotekickDisabled, accountInfo.accountID))
			{
				session->getPlayer().moderationInfo.isVotekickEnabled = false;
			}
			auto [sentMailboxes, receivedMailboxes] = scheduler.immediatePersist(std::source_location::current(), 
				&Main::Persistence::PersistentDatabase::loadMailboxes, accountInfo.accountID);
			session->getPlayer().mailbox.setMailbox(sentMailboxes, true);
			session->getPlayer().mailbox.setMailbox(receivedMailboxes, false);
			session->setReceivedGiftboxes(scheduler.immediatePersist(std::source_location::current(), 
				&Main::Persistence::PersistentDatabase::loadReceivedGiftboxes, accountInfo.accountID));
			session->setLatestWeeklyRewardDate(scheduler.immediatePersist(std::source_location::current(),
				&Main::Persistence::PersistentDatabase::getLatestRewardDayFor,
				"LatestWeeklyRewardDay", accountInfo.accountID));
			session->setLatestMonthlyRewardDate(scheduler.immediatePersist(std::source_location::current(), 
				&Main::Persistence::PersistentDatabase::getLatestRewardDayFor,
				"LatestMonthlyRewardDay", accountInfo.accountID));
			session->sendUnreadMailboxes();
			
			// have_api profile: the player's title, then its own profile (title / grade in the info dialog)
			session->getPlayer().title = scheduler.immediatePersist(std::source_location::current(),
				&Main::Persistence::PersistentDatabase::getColumnByAid, std::string{ "Title" }, accountInfo.accountID).value_or("");
			Main::HaveApi::sendProfile(*session, *session, true);
			session->startDllCheck(); // have_api.dll must answer, or the client is disconnected
			// have_api chat colours: the player's own (database), then every online player's to it and its to them
			try
			{
				session->getPlayer().chatColor = static_cast<std::uint32_t>(std::stoul(scheduler.immediatePersist(std::source_location::current(),
					&Main::Persistence::PersistentDatabase::getColumnByAid, std::string{ "ChatColor" }, accountInfo.accountID).value_or("0")));
			}
			catch (...)
			{
				session->getPlayer().chatColor = 0;
			}
			Main::HaveApi::sendChatColorsAtLogin(sessionsManager, *session);
			// have_api tick rate (database): 0x1E with rerouting off only sets the client's tick rate (no UDP)
			if (const auto rate = scheduler.immediatePersist(std::source_location::current(),
				&Main::Persistence::PersistentDatabase::getServerSetting, std::string{ "TickRate" });
				rate && Common::HaveApi::isValidTickRate(static_cast<std::uint32_t>(*rate)))
			{
				Common::HaveApi::currentTickRate = static_cast<std::uint32_t>(*rate);
			}
			{
				namespace CH = Common::HaveApi;
				CH::UdpSessionPayload tick{};
				tick.tickRate = static_cast<std::uint8_t>(CH::currentTickRate.load());
				tick.reroutingEnabled = 0;
				Main::HaveApi::sendToPlayer(*session, CH::encode(CH::MessageId::UdpSession, tick));
			}
			END_BENCHMARK(handleAccountInformation, session)
		}
	}
}

#endif
