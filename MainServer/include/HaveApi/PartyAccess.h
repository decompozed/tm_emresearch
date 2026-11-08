/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#ifndef MAIN_HAVE_API_PARTY_ACCESS_H
#define MAIN_HAVE_API_PARTY_ACCESS_H

#include <cstdint>
#include <memory>
#include "HaveApi/Catalog.h"
#include "Network/Sessions/MainSession.h"
#include "Rooms/Room.h"
#include "Utils/SetupParser.h"

namespace Main::HaveApi
{
	[[nodiscard]] inline bool isPartyModeOffered(std::int32_t modeId)
	{
		return Common::HaveApi::isPartyModeOffered(modeId, Common::Utils::SetupParser::getInstance().getHaveApiSetup().battleRoyaleEnabled);
	}

	[[nodiscard]] inline bool supportsPartyMode(const Main::Network::Session& session, std::int32_t modeId)
	{
		return session.getPlayer().dllProtocol >= Common::HaveApi::minimumDllProtocol(modeId);
	}

	[[nodiscard]] inline std::shared_ptr<Main::Network::Session> memberWithoutSupport(const Main::Classes::Room& room, std::int32_t modeId)
	{
		if (Common::HaveApi::minimumDllProtocol(modeId) == 0)
		{
			return nullptr;
		}
		for (const auto& [info, session] : room.getAllPlayersWithSessions())
		{
			if (session && !supportsPartyMode(*session, modeId))
			{
				return session;
			}
		}
		return nullptr;
	}

	[[nodiscard]] inline bool refusesPlayer(const Main::Classes::Room& room, const Main::Network::Session& session)
	{
		return room.getRoomSettings().mode == Common::HaveApi::partyRoomMode &&
			!supportsPartyMode(session, room.haveApi().partySettings().modeId);
	}
}

#endif
