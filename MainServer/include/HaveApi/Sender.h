/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#ifndef MAIN_HAVE_API_SENDER_H
#define MAIN_HAVE_API_SENDER_H

#include <cstdint>
#include <cstring>
#include <string_view>
#include <memory>
#include <span>
#include <vector>
#include "HaveApi/MessageReader.h"
#include "HaveApi/MessageId.h"
#include "HaveApi/PacketLog.h"
#include "Network/Packet.h"
#include "Network/Sessions/MainSession.h"

namespace Main::HaveApi
{
	[[nodiscard]] inline std::string_view playerName(const Main::Network::Session& session)
	{
		const auto& nickname = session.getAccountInfo().nickname;
		return { nickname, strnlen(nickname, sizeof(nickname)) };
	}

	// Sends one encoded have_api message (Common::HaveApi::MessageWriter output) to a player's MainServer
	// session: order 81, extra 0x24, option 5, the same encryption as the other player packets.
	inline bool sendToPlayer(Main::Network::Session& session, const std::vector<std::uint8_t>& data)
	{
		namespace CH = Common::HaveApi;
		const auto message = CH::ReceivedMessage::parse(data);
		if (!message)
		{
			return false;
		}
		Common::Network::Packet packet;
		packet.setCommand(CH::order, 0, CH::serverToClientExtra, CH::option);
		packet.setData(data.data(), static_cast<std::uint16_t>(data.size()));
		packet.setTcpHeader(static_cast<std::uint32_t>(session.getId()), Common::Enums::USER_LARGE_ENCRYPTION); // size from the data

		PacketLog::write(Direction::ServerToClient, session.getId(), playerName(session), message->id(),
			std::span{ data }.subspan(sizeof(std::uint32_t)));
		return session.asyncWrite(packet);
	}
}

#endif
