/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#ifndef MAIN_HAVE_API_PARTY_REWARDS_H
#define MAIN_HAVE_API_PARTY_REWARDS_H

#include <algorithm>
#include <array>
#include <cstdint>

namespace Main::HaveApi
{
	inline constexpr std::array<std::uint64_t, 3> placementBonusMinutes = { 3, 2, 1 };

	[[nodiscard]] constexpr std::uint64_t placementBonusMinutesFor(std::int32_t placement, std::int32_t entrants)
	{
		if (placement < 1 || placement > static_cast<std::int32_t>(placementBonusMinutes.size()))
		{
			return 0;
		}
		const std::int32_t beaten = entrants - placement;
		return beaten <= 1 ? 0 : (std::min)(placementBonusMinutes[static_cast<std::size_t>(placement - 1)], static_cast<std::uint64_t>(beaten - 1));
	}

	[[nodiscard]] constexpr std::uint64_t playedMinutes(std::uint64_t matchStart, std::uint64_t now, std::uint64_t playedUntil, std::uint64_t cap)
	{
		const std::uint64_t end = (std::min)(now, playedUntil);
		return end > matchStart ? (std::min)((end - matchStart) / 1000 / 60, cap) : 0;
	}
}

#endif
