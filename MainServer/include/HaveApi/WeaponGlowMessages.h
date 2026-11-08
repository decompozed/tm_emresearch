/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#ifndef MAIN_HAVE_API_WEAPON_GLOW_MESSAGES_H
#define MAIN_HAVE_API_WEAPON_GLOW_MESSAGES_H

#include <algorithm>
#include <cstring>
#include "HaveApi/MessageWriter.h"
#include "HaveApi/Messages.h"
#include "HaveApi/Sender.h"
#include "HaveApi/WeaponGlow.h"
#include "Network/MainSessionManager.h"
#include "Network/Sessions/MainSession.h"

// Glow colour of level 8-9 weapons. 0x33: the player's own coloured items (the item window shows the choice);
// 0x32: per player and weapon slot (10..16) the colour of the weapon equipped there, to every online player.
namespace Main::HaveApi
{
	struct EquippedGlow
	{
		std::uint32_t color{};  // 0 = none / the original glow
		std::uint32_t itemId{}; // the equipped weapon's cgd id, 0 = none
	};

	// Colour and item id of the weapon the player has equipped in `slot` (the selected character)
	[[nodiscard]] inline EquippedGlow equippedGlow(Main::Network::Session& player, std::uint8_t slot)
	{
		const auto& glows = player.getPlayer().itemGlows;
		for (const auto& item : player.getPlayer().inventory.getEquippedItemsFor(player.getAccountInfo().latestSelectedCharacter))
		{
			if (item.type != slot || item.serialInfo.itemNumber == 0) continue;
			const auto found = glows.find(static_cast<std::uint32_t>(item.serialInfo.itemNumber));
			return { found != glows.end() ? found->second : 0, static_cast<std::uint32_t>(item.id) };
		}
		return {};
	}

	[[nodiscard]] inline std::vector<std::uint8_t> encodeWeaponGlow(Main::Network::Session& player, std::uint8_t slot, bool reset)
	{
		namespace CH = Common::HaveApi;
		CH::WeaponGlowPayload payload{};
		const auto& nickname = player.getAccountInfo().nickname;
		std::memcpy(payload.nickname.data(), nickname, std::min(sizeof(nickname), payload.nickname.size()));
		payload.slot = slot;
		payload.reset = reset ? 1 : 0;
		const EquippedGlow glow = equippedGlow(player, slot);
		payload.color = glow.color;
		payload.itemId = glow.itemId;
		return CH::encode(CH::MessageId::WeaponGlow, payload);
	}

	// The player's 7 weapon slots to every online player (after a colour or an equipment change)
	inline void broadcastWeaponGlows(Main::Network::SessionsManager& sessionsManager, Main::Network::Session& player)
	{
		namespace CH = Common::HaveApi;
		std::vector<std::vector<std::uint8_t>> messages;
		for (std::uint8_t slot = CH::firstWeaponSlot; slot <= CH::lastWeaponSlot; ++slot) messages.push_back(encodeWeaponGlow(player, slot, false));
		for (auto& [id, session] : sessionsManager.getAllSessions())
		{
			if (!session) continue;
			for (const auto& message : messages) sendToPlayer(*session, message);
		}
	}

	// The cgd id of one of the player's items (inventory or equipped), 0 = not found
	[[nodiscard]] inline std::uint32_t itemIdOf(Main::Network::Session& player, std::uint32_t itemNumber)
	{
		const auto& items = player.getPlayer().inventory.getItems();
		if (const auto found = items.find(itemNumber); found != items.end()) return static_cast<std::uint32_t>(found->second.itemId.itemId);
		for (const auto& item : player.getPlayer().inventory.getEquippedItems())
		{
			if (item.serialInfo.itemNumber == itemNumber) return static_cast<std::uint32_t>(item.id);
		}
		return 0;
	}

	// The player's own coloured items (reset first)
	inline void sendItemGlows(Main::Network::Session& player)
	{
		namespace CH = Common::HaveApi;
		CH::ItemGlowPayload reset{};
		reset.reset = 1;
		sendToPlayer(player, CH::encode(CH::MessageId::ItemGlow, reset));
		for (const auto& [itemNumber, color] : player.getPlayer().itemGlows)
		{
			CH::ItemGlowPayload item{};
			item.itemNumber = itemNumber;
			item.color = color;
			item.itemId = itemIdOf(player, itemNumber);
			sendToPlayer(player, CH::encode(CH::MessageId::ItemGlow, item));
		}
	}

	// After the inventory load at login: the player's items, every online player's coloured slots to it, its slots to them
	inline void sendGlowsAtLogin(Main::Network::SessionsManager& sessionsManager, Main::Network::Session& player)
	{
		namespace CH = Common::HaveApi;
		player.loadItemGlows();
		sendItemGlows(player);
		sendToPlayer(player, encodeWeaponGlow(player, CH::firstWeaponSlot, true)); // reset (with the player's own first slot)
		for (auto& [id, session] : sessionsManager.getAllSessions())
		{
			if (!session || session.get() == &player || session->getPlayer().itemGlows.empty()) continue;
			for (std::uint8_t slot = CH::firstWeaponSlot; slot <= CH::lastWeaponSlot; ++slot)
			{
				if (equippedGlow(*session, slot).color != 0) sendToPlayer(player, encodeWeaponGlow(*session, slot, false));
			}
		}
		broadcastWeaponGlows(sessionsManager, player);
	}
}

#endif
