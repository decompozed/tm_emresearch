/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#ifndef COMMON_HAVE_API_CHAT_COLORS_H
#define COMMON_HAVE_API_CHAT_COLORS_H

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

// Custom chat colours: chosen in a palette next to the chat box (DLL, message 0x34; the swatches in this list's order
// after DEFAULT), every colour for everybody; the other players see it (have_api message 0x30, the DLL colours the
// player's normal / team / clan / GM lines). Staff are told apart by their chat tags ([Moderator] grade 3, [GM] grades
// 4 and 7), not by colour.
namespace Common::HaveApi
{
	struct ChatColor
	{
		std::string_view name;
		std::uint32_t rgb; // 0xRRGGBB
	};

	inline constexpr std::array<ChatColor, 13> chatColors = { {
		{ "RED", 0xE63C3C },
		{ "ORANGE", 0xFF8C1A },
		{ "BLUE", 0x3C78FF },
		{ "LIGHTBLUE", 0x78BEFF },
		{ "CYAN", 0x1ED7E6 },
		{ "PURPLE", 0xA05AE6 },
		{ "VIOLET", 0xC88CFF },
		{ "BROWN", 0xB4783C },
		{ "GRAY", 0xAAAAAA },
		{ "WHITE", 0xFFFFFF },
		{ "GREEN", 0x3CC83C },
		{ "YELLOW", 0xFFDC32 },
		{ "PINK", 0xFF82C8 },
	} };
	inline constexpr std::string_view chatColorDefault = "DEFAULT"; // back to the game's colour

	// The colour of a name (any case); nullopt: unknown name. DEFAULT gives 0 (no custom colour).
	[[nodiscard]] inline std::optional<std::uint32_t> chatColorByName(std::string name)
	{
		std::ranges::transform(name, name.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
		if (name == chatColorDefault) return 0u;
		for (const ChatColor& color : chatColors)
		{
			if (color.name == name) return color.rgb | 0x01000000u; // bit 24: a colour is set (black is not offered anyway)
		}
		return std::nullopt;
	}

	// 0 (the game's colour) or a colour of the list with bit 24
	[[nodiscard]] inline bool isChatColor(std::uint32_t color)
	{
		if (color == 0) return true;
		if ((color & 0xFF000000u) != 0x01000000u) return false;
		return std::ranges::any_of(chatColors, [color](const ChatColor& c) { return c.rgb == (color & 0xFFFFFFu); });
	}

	[[nodiscard]] inline std::string chatColorNames()
	{
		std::string names;
		for (const ChatColor& color : chatColors) names += std::string{ color.name } + ' ';
		return names + std::string{ chatColorDefault };
	}
}

#endif
