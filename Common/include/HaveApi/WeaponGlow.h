/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#ifndef COMMON_HAVE_API_WEAPON_GLOW_H
#define COMMON_HAVE_API_WEAPON_GLOW_H

#include <algorithm>
#include <array>
#include <cstdint>

// Glow colour of level 8-9 weapons: chosen per weapon (UserItems.GlowColor) from a palette in the item window (DLL),
// seen by everybody (message 0x32: player + weapon slot -> colour). The DLL recolours the materials of the weapon's
// upgrade effect (fx.dat weapon/wpn_up_lv8 / lv9_*.nif: billboard glows and particles).
namespace Common::HaveApi
{
	inline constexpr std::uint32_t glowColorSet = 0x01000000u; // bit 24: a colour is chosen (0 = the effect's own colours)
	inline constexpr std::uint32_t glowHidden = 0x02000000u;   // no glow at all: the effect is hidden, for everybody

	// The palette (0xRRGGBB); the same list is in the DLL (ui/inventory/WeaponGlow.cpp) and its swatches
	inline constexpr std::array<std::uint32_t, 14> glowPalette = {
		0xFF2A2A, // red
		0xFF8C1A, // orange
		0xFFE135, // yellow
		0xFFD700, // gold
		0x9BFF2A, // lime
		0x2AD94A, // green
		0x1EE6E6, // cyan
		0x6EC8FF, // light blue
		0x2A5BFF, // blue
		0x8C3CFF, // purple
		0xC88CFF, // violet
		0xFF6EC8, // pink
		0xFF2AD4, // magenta
		0xFFFFFF, // white
	};

	// 0 (the original glow), glowHidden or a palette colour with bit 24
	[[nodiscard]] inline bool isGlowColor(std::uint32_t color)
	{
		if (color == 0 || color == glowHidden) return true;
		return (color & glowColorSet) != 0 && std::ranges::find(glowPalette, color & 0xFFFFFFu) != glowPalette.end() &&
			(color & 0xFE000000u) == 0;
	}

	inline constexpr std::uint8_t firstWeaponSlot = 10; // ItemType MELEE .. GRENADE = the client's weapon slots 10..16
	inline constexpr std::uint8_t lastWeaponSlot = 16;
}

#endif
