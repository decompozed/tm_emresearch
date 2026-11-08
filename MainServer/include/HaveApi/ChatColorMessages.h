/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#ifndef MAIN_HAVE_API_CHAT_COLOR_MESSAGES_H
#define MAIN_HAVE_API_CHAT_COLOR_MESSAGES_H

#include <algorithm>
#include <cstring>
#include "HaveApi/MessageWriter.h"
#include "HaveApi/Messages.h"
#include "HaveApi/Sender.h"
#include "Network/MainSessionManager.h"
#include "Network/Sessions/MainSession.h"

// Custom chat colours: the DLLs colour a player's chat lines by the player's name (message 0x30).
namespace Main::HaveApi
{
	[[nodiscard]] inline std::vector<std::uint8_t> encodeChatColor(Main::Network::Session& player, bool reset)
	{
		namespace CH = Common::HaveApi;
		CH::ChatColorPayload payload{};
		const auto& nickname = player.getAccountInfo().nickname;
		std::memcpy(payload.nickname.data(), nickname, std::min(sizeof(nickname), payload.nickname.size()));
		payload.color = player.getPlayer().chatColor;
		payload.reset = reset ? 1 : 0;
		return CH::encode(CH::MessageId::ChatColor, payload);
	}

	// A player's colour (set or cleared) to every online player
	inline void broadcastChatColor(Main::Network::SessionsManager& sessionsManager, Main::Network::Session& player)
	{
		const auto message = encodeChatColor(player, false);
		for (auto& [id, session] : sessionsManager.getAllSessions())
		{
			if (session) sendToPlayer(*session, message);
		}
	}

	// At login: the new player gets every online player's colour (first message: reset), the others get the new player's
	inline void sendChatColorsAtLogin(Main::Network::SessionsManager& sessionsManager, Main::Network::Session& player)
	{
		namespace CH = Common::HaveApi;
		CH::ChatColorPayload reset{};
		reset.reset = 1;
		sendToPlayer(player, CH::encode(CH::MessageId::ChatColor, reset));
		for (auto& [id, session] : sessionsManager.getAllSessions())
		{
			if (!session) continue;
			if (session.get() != &player && session->getPlayer().chatColor != 0) sendToPlayer(player, encodeChatColor(*session, false));
		}
		if (player.getPlayer().chatColor != 0) broadcastChatColor(sessionsManager, player);
	}
}

#endif
