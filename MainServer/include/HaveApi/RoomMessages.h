/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#ifndef MAIN_HAVE_API_ROOM_MESSAGES_H
#define MAIN_HAVE_API_ROOM_MESSAGES_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "HaveApi/Catalog.h"
#include "HaveApi/MessageWriter.h"
#include "HaveApi/PartyAccess.h"
#include "HaveApi/Messages.h"
#include "HaveApi/Sender.h"
#include "Managers/RoomsManager.h"
#include "Structures/Room/RoomsList.h"
#include "Network/Sessions/MainSession.h"
#include "Rooms/Room.h"
#include "Utils/Logger.h"
#include "Utils/SetupParser.h"

// Room-level have_api messages: room options (0x06), party game selection (0x0A), room list table (0x0F).
namespace Main::HaveApi
{
	namespace CH = Common::HaveApi;

	inline void sendToRoom(const Main::Classes::Room& room, const std::vector<std::uint8_t>& message)
	{
		for (const auto& [info, session] : room.getAllPlayersWithSessions())
		{
			sendToPlayer(*session, message);
		}
	}

	[[nodiscard]] inline std::vector<std::uint8_t> encodeRoomOptions(const Main::Classes::Room& room)
	{
		return CH::encode(CH::MessageId::RoomOptions, room.haveApi().options());
	}

	[[nodiscard]] inline std::vector<std::uint8_t> encodePartySettings(const Main::Classes::Room& room)
	{
		return CH::encode(CH::MessageId::PartySettings, room.haveApi().partySettings());
	}

	// 0x0F: room number -> party game of every party room, for the lobby room list (the DLL shows the game's name in
	// the mode column and its ribbon). Sent with every room list.
	inline void sendPartyRoomTable(Main::Network::Session& session, Main::Classes::RoomsManager& roomsManager,
		const std::vector<Main::Structures::SingleRoom>& rooms)
	{
		std::vector<CH::MapPartyMode> entries;
		for (const Main::Structures::SingleRoom& listed : rooms)
		{
			// the room list carries the room number - 1 (Room::getRoomInfo); the client keys the table with that value
			const Main::Classes::Room* room = roomsManager.getRoomByNumber(static_cast<std::uint16_t>(listed.roomNumber + 1));
			if (room == nullptr || room->getRoomSettings().mode != CH::partyRoomMode || !room->haveApi().hasPartyGame())
			{
				continue;
			}
			CH::MapPartyMode entry{};
			entry.roomNumber = listed.roomNumber & 0x7FF;
			entry.partyMode = static_cast<std::uint16_t>(room->haveApi().partySettings().modeId) & 0x1F;
			entries.push_back(entry);
		}
		CH::MessageWriter writer{ CH::MessageId::MapModeTable };
		writer.append(static_cast<std::uint16_t>(entries.size()));
		for (const CH::MapPartyMode& entry : entries)
		{
			writer.append(entry);
		}
		sendToPlayer(session, writer.finish());
	}

	// Everything a player needs when entering a room: the room options, the party game.
	inline void sendRoomState(const Main::Classes::Room& room, Main::Network::Session& session)
	{
		sendToPlayer(session, encodeRoomOptions(room));
		sendToPlayer(session, encodePartySettings(room));
	}

	namespace Detail
	{
		// Common checks of the room master's changes. Returns the room when the change may be applied.
		[[nodiscard]] inline Main::Classes::Room* editableRoom(Main::Network::Session& session, Main::Classes::RoomsManager& roomsManager,
			const char* what)
		{
			Main::Classes::Room* room = roomsManager.getRoomByNumber(session.getPlayer().matchContext.roomNumber);
			if (room == nullptr)
			{
				return nullptr; // the DLL also sends its current choices while its player is outside a room
			}
			const std::string player{ playerName(session) };
			if (!room->isHost(session.getAccountInfo().uniqueId))
			{
				Utils::Logger::log("[HaveApi] " + std::string{ what } + " refused: " + player + " is not the room master", Utils::LogType::Warning);
				return nullptr;
			}
			if (room->hasMatchStarted())
			{
				Utils::Logger::log("[HaveApi] " + std::string{ what } + " refused: the match of room " + std::to_string(room->getRoomNumber()) +
					" is running", Utils::LogType::Warning);
				return nullptr;
			}
			return room;
		}

		// A refused change is undone in the sender's client by resending the room's current state.
		inline void revert(Main::Network::Session& session, Main::Classes::RoomsManager& roomsManager)
		{
			if (const Main::Classes::Room* room = roomsManager.getRoomByNumber(session.getPlayer().matchContext.roomNumber))
			{
				sendToPlayer(session, encodeRoomOptions(*room));
				sendToPlayer(session, encodePartySettings(*room));
			}
		}
	}

	// 0x06 from the DLL: the room master changed a room option.
	// The DLL sends an option again whenever the server's 0x06 sets its control (check box), and resends its "Auto"
	// choice in the modes without room options: a value the room already has changes nothing and gets no answer, and in
	// a mode without room options the change is dropped without resending the room's options (no message loop).
	inline void onRoomOption(Main::Network::Session& session, Main::Classes::RoomsManager& roomsManager, const CH::RoomOptionPayload& option)
	{
		if (const Main::Classes::Room* current = roomsManager.getRoomByNumber(session.getPlayer().matchContext.roomNumber);
			current != nullptr && current->haveApi().option(option.kind) == option.value)
		{
			return;
		}
		Main::Classes::Room* room = Detail::editableRoom(session, roomsManager, "room option");
		if (room == nullptr)
		{
			Detail::revert(session, roomsManager);
			return;
		}
		const std::uint32_t roomMode = room->getRoomSettings().mode;
		const bool partyBattle = roomMode == CH::partyRoomMode && room->haveApi().hasPartyGame();
		if ((partyBattle || !CH::roomOptionsApplyIn(roomMode)) && option.kind != CH::RoomOptionKind::OptionCheckBox)
		{
			return;
		}
		if (partyBattle || !CH::roomOptionsApplyIn(roomMode) || !CH::isValidRoomOption(option.kind, option.value))
		{
			Utils::Logger::log("[HaveApi] room option refused: kind " + std::to_string(static_cast<int>(option.kind)) + " value " +
				std::to_string(option.value) + " in room mode " + std::to_string(roomMode), Utils::LogType::Warning);
			Detail::revert(session, roomsManager);
			return;
		}
		room->haveApi().setOption(option.kind, option.value);
		sendToRoom(*room, encodeRoomOptions(*room));
	}

	// 0x0A from the DLL: the room master picked a party game (or none) and its settings.
	inline void onPartySettings(Main::Network::Session& session, Main::Classes::RoomsManager& roomsManager, const CH::PartySettingsPayload& settings)
	{
		if (const Main::Classes::Room* current = roomsManager.getRoomByNumber(session.getPlayer().matchContext.roomNumber);
			current != nullptr && current->haveApi().partySettings().modeId == settings.modeId &&
			current->haveApi().partySettings().values == settings.values)
		{
			return; // nothing changes (no answer: see onRoomOption)
		}
		Main::Classes::Room* room = Detail::editableRoom(session, roomsManager, "party game");
		if (room == nullptr)
		{
			Detail::revert(session, roomsManager);
			return;
		}
		if (room->getRoomSettings().mode != CH::partyRoomMode || !CH::isValidPartySettings(settings))
		{
			Utils::Logger::log("[HaveApi] party game refused: mode " + std::to_string(settings.modeId) + " in room mode " +
				std::to_string(room->getRoomSettings().mode), Utils::LogType::Warning);
			Detail::revert(session, roomsManager);
			return;
		}
		if (!isPartyModeOffered(settings.modeId))
		{
			session.sendMessage("Battle Royale is not enabled on this server");
			Detail::revert(session, roomsManager);
			return;
		}
		if (const auto member = memberWithoutSupport(*room, settings.modeId))
		{
			session.sendMessage(std::string{ playerName(*member) } + " needs the updated have_api.dll for this party game");
			Detail::revert(session, roomsManager);
			return;
		}
		room->haveApi().setPartySettings(settings);
		sendToRoom(*room, encodePartySettings(*room));
	}

	// A room that leaves SquareMode loses its party game (the DLL only runs party games in SquareMode rooms).
	inline void onRoomModeChanged(Main::Classes::Room& room)
	{
		if (room.getRoomSettings().mode != CH::partyRoomMode && room.haveApi().hasPartyGame())
		{
			room.haveApi().clearPartyGame();
			sendToRoom(room, encodePartySettings(room));
		}
	}
}

#endif
