/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#ifndef MAIN_HAVE_API_ROUTER_H
#define MAIN_HAVE_API_ROUTER_H

#include <format>
#include <memory>
#include <string>
#include <span>
#include "Handlers/Player/AuthorizationHandler.h"
#include "HaveApi/MessageId.h"
#include "HaveApi/MessageReader.h"
#include "HaveApi/PacketLog.h"
#include "HaveApi/RoomMessages.h"
#include "HaveApi/WeaponGlowMessages.h"
#include "HaveApi/ChatColorMessages.h"
#include "HaveApi/Sender.h"
#include "Managers/RoomsManager.h"
#include "Network/Packet.h"
#include "Network/Sessions/MainSession.h"
#include "Utils/Logger.h"

namespace Main::HaveApi
{
	[[nodiscard]] inline bool isHaveApiPacket(const Common::Network::Packet& request) noexcept
	{
		return request.getExtra() == Common::HaveApi::clientToServerExtra && request.getOption() == Common::HaveApi::option;
	}

	namespace Detail
	{
		// Decodes the payload of one DLL message and hands it to its handler; short payloads are dropped.
		template<typename Payload, typename Handler>
		void dispatch(const Common::HaveApi::ReceivedMessage& message, Main::Network::Session& session, Handler&& handler)
		{
			if (const auto payload = message.payloadAs<Payload>())
			{
				handler(*payload);
				return;
			}
			Utils::Logger::log("[HaveApi] message 0x" + std::format("{:02X}", message.id()) + " too short from " +
				std::string{ playerName(session) }, Utils::LogType::Warning);
		}
	}

	// A message the DLL sent (order 81, extra 0x34, option 5): logged, then handled by id.
	inline void handleClientMessage(const Common::Network::Packet& request, std::shared_ptr<Main::Network::Session> session,
		Main::Classes::RoomsManager& roomsManager, Main::Network::SessionsManager& sessionsManager)
	{
		const std::span<const std::uint8_t> data{ request.getData(), request.getDataSize() };
		const auto message = Common::HaveApi::ReceivedMessage::parse(data);
		if (!message)
		{
			Utils::Logger::log("[HaveApi] malformed message from session " + std::to_string(session->getId()), Utils::LogType::Warning);
			return;
		}
		PacketLog::write(Direction::ClientToServer, session->getId(), playerName(*session), message->id(),
			data.subspan(sizeof(std::uint32_t)));

		namespace CH = Common::HaveApi;
		switch (static_cast<CH::MessageId>(message->id()))
		{
		case CH::MessageId::RoomOptions:
			Detail::dispatch<CH::RoomOptionPayload>(*message, *session,
				[&](const CH::RoomOptionPayload& option) { onRoomOption(*session, roomsManager, option); });
			break;
		case CH::MessageId::PartySettings:
			Detail::dispatch<CH::PartySettingsPayload>(*message, *session,
				[&](const CH::PartySettingsPayload& settings) { onPartySettings(*session, roomsManager, settings); });
			break;
		case CH::MessageId::PropAction:
			Detail::dispatch<CH::PropActionPayload>(*message, *session, [&](const CH::PropActionPayload& action) {
				Main::Classes::Room* room = roomsManager.getRoomByNumber(session->getPlayer().matchContext.roomNumber);
				if (PartyMatch* match = room != nullptr ? room->haveApi().match() : nullptr)
				{
					match->onPropAction(*session, action);
				}
			});
			break;
		case CH::MessageId::StormLethal:
			Detail::dispatch<CH::StormLethalPayload>(*message, *session, [&](const CH::StormLethalPayload& lethal) {
				Main::Classes::Room* room = roomsManager.getRoomByNumber(session->getPlayer().matchContext.roomNumber);
				if (PartyMatch* match = room != nullptr ? room->haveApi().match() : nullptr)
				{
					match->onStormLethal(*session, lethal.playerId);
				}
			});
			break;
		case CH::MessageId::ChatColorRequest:
			Detail::dispatch<CH::ChatColorRequestPayload>(*message, *session, [&](const CH::ChatColorRequestPayload& chat) {
				if (session->setChatColor(chat.color)) broadcastChatColor(sessionsManager, *session);
			});
			break;
		case CH::MessageId::GlowRequest:
			Detail::dispatch<CH::GlowRequestPayload>(*message, *session, [&](const CH::GlowRequestPayload& glow) {
				if (session->setItemGlow(glow.itemNumber, glow.color)) broadcastWeaponGlows(sessionsManager, *session);
			});
			break;
		case CH::MessageId::DllCheck:
			Detail::dispatch<CH::DllCheckPayload>(*message, *session,
				[&](const CH::DllCheckPayload& check) { session->onDllCheckAnswer(check.nonce, check.version, check.proof); });
			break;
		case CH::MessageId::ItemLock:
			Detail::dispatch<CH::ItemLockPayload>(*message, *session,
				[&](const CH::ItemLockPayload& lock) { session->setItemLock(lock.itemNumber, lock.locked != 0); });
			break;
		case CH::MessageId::ItemOption:
			Detail::dispatch<CH::ItemOptionPayload>(*message, *session,
				[&](const CH::ItemOptionPayload& item) { session->onItemOptionRequest(item.itemNumber, item.action == 1); });
			break;
		case CH::MessageId::UpgradeTicket:
			Detail::dispatch<CH::UpgradeTicketPayload>(*message, *session, [&](const CH::UpgradeTicketPayload& ticket) {
				if (ticket.action == 1) session->useUpgradeTicket(ticket.itemNumber, ticket.branch, ticket.level);
				else session->sendUpgradeTicketState(ticket.itemNumber);
			});
			break;
		default:
			break; // 0x0B (UDP session lost) is handled with the UDP channel; other ids are not sent by the DLL
		}
	}

	// Order 81 carries both the have_api messages and the game's HWID report.
	inline void handleOrder81(const Common::Network::Packet& request, std::shared_ptr<Main::Network::Session> session,
		Main::Classes::RoomsManager& roomsManager, Main::Network::SessionsManager& sessionsManager)
	{
		if (isHaveApiPacket(request))
		{
			handleClientMessage(request, session, roomsManager, sessionsManager);
			return;
		}
		Main::Handlers::handleHwid(request, session);
	}
}

#endif
