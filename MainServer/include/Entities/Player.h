#ifndef PLAYER_CLASS_H
#define PLAYER_CLASS_H

#include <ctime>
#include "Structures/Item/MainEquippedItem.h"
#include "Structures/Item/MainBoughtItem.h"
#include "Structures/AccountInfo/MainAccountInfo.h"
#include "Structures/PlayerLists/Friend.h"
#include "Structures/Mailbox/Mailbox.h"
#include "Structures/PlayerLists/BlockedPlayer.h"
#include "Structures/Item/MainItem.h"
#include "Persistence/MainScheduler.h"
#include "Structures/TradeSystem/TradeSystemItem.h"
#include "Structures/ClientData/Structures.h"
#include "Entities/Inventory.h"
#include "Entities/SocialInfo.h"
#include "Entities/TradeInfo.h"
#include "Structures/ModerationInfo.h"
#include "Structures/MatchContext.h"
#include "Entities/Mailbox.h"

#include <unordered_map>
#include <unordered_set>
#include "HaveApi/DllProof.h"
#include <vector>
#include <array>
#include <optional>

namespace Common { namespace Network { class Session; } }
namespace Main { namespace Network { class Session; } }
namespace Main
{
	namespace Classes
	{
		struct Player
		{
			using AccountInfo = Main::Structures::AccountInfo;
			using ModerationInfo = Main::Structures::ModerationInfo;
			using MatchContext = Main::Structures::MatchContext;

			AccountInfo accountInfo{};
			Inventory inventory{ accountInfo };
			SocialInfo socialInfo{};
			TradeInfo tradeInfo{};
			ModerationInfo moderationInfo{};
			MatchContext matchContext{};
			Main::Classes::Mailbox mailbox{};
			Common::Enums::PlayerState playerState{};
			std::uint16_t ping{};
			std::string latestWeeklyRewardDay{};
			std::string latestMonthlyRewardDay{};
			std::uint32_t totalWrongPasswordReset{};
			std::uint32_t totalWrong2FaUsernameChange{};
			std::uint32_t totalWrongUsernameChange{};
			std::uint32_t totalWrong2FaReset{};
			std::string pendingTwoFaSecret{}; // /twofa setup: kept only in memory until the first code confirms it
			std::time_t pendingTwoFaSince{};
			std::string hwid{ "" };
			std::uint64_t hwidLastUpdatedTimestamp{};
			std::string gradedHwid{ "" };
			std::string gradedHwidSalt{ "" };
			std::string title{}; // have_api profile: the title the player chose (Users.Title, /title)
			std::uint32_t chatColor{}; // have_api chat colour (Users.ChatColor): 0xRRGGBB + bit 24, 0 = the game's
			std::unordered_map<std::uint32_t, std::uint32_t> itemGlows{}; // have_api weapon glow: item number -> colour
			std::unordered_set<std::uint32_t> lockedItems{}; // have_api item lock: item numbers (UserItems.IsLocked)
			std::uint32_t dllCheckNonce{}; // have_api DLL check: nonce of the pending challenge, 0 = none pending
			Common::HaveApi::DllChallenge dllChallenge{}; // the pending challenge
			// The build(s) the pending challenge is checked against: the allowed builds at login, then only the build that
			// passed the login check (a new build copied to Setup never fails the periodic checks of a running session)
			std::shared_ptr<const std::vector<Common::HaveApi::DllImage>> dllImages{};
			bool dllConfirmed{};             // the DLL answered the nonce
			bool dllEverConfirmed{};         // the login check passed (later checks are not logged)
			std::uint32_t dllProtocol{};
			bool dllDisconnecting{};         // the DLL check is closing this session

			void setPlayerName(const char* playerName);
			bool isInLobby() const;
			bool isInMatch() const;
			void leaveRoom();
			void storeBatteryObtainedInMatch();
		};
	}
}

#endif
