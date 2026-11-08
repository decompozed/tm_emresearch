/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#ifndef COMMON_HAVE_API_STORM_H
#define COMMON_HAVE_API_STORM_H

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include "Utils/Macros.h"

namespace Common::HaveApi
{
	inline constexpr std::size_t maxStormBoxes = 16;
	inline constexpr std::size_t maxStormPlayers = 16;

	enum class StormAction : std::uint8_t
	{
		Set = 1,
		Clear = 2,
		Query = 3,
	};

PACK_PUSH(1)
	struct StormBox
	{
		float x1{}, x1Speed{}, x2{}, x2Speed{};
		float y1{}, y1Speed{}, y2{}, y2Speed{};
		float moveSeconds{};
	};

	struct StormCommand
	{
		StormAction action{};
		std::uint8_t boxCount{};
		std::uint8_t memberCount{};
		std::uint8_t reserved{};
		std::array<StormBox, maxStormBoxes> boxes{};
		std::array<std::uint32_t, maxStormPlayers> members{};
	};

	struct StormAnswer
	{
		std::uint8_t done{};
		std::uint8_t count{};
		std::uint16_t reserved{};
		std::array<std::uint32_t, maxStormPlayers> sessions{};
	};
PACK_POP()

	static_assert(sizeof(StormBox) == 36);
	static_assert(sizeof(StormCommand) == 4 + 36 * maxStormBoxes + 4 * maxStormPlayers);
	static_assert(sizeof(StormAnswer) == 4 + 4 * maxStormPlayers);

	struct StormRect
	{
		float x1{}, x2{}, y1{}, y2{};

		[[nodiscard]] constexpr bool empty() const noexcept { return !(x1 < x2 && y1 < y2); }
		[[nodiscard]] constexpr bool contains(float x, float y) const noexcept
		{
			return !empty() && x >= x1 && x <= x2 && y >= y1 && y <= y2;
		}
		friend constexpr bool operator==(const StormRect&, const StormRect&) = default;
	};

	[[nodiscard]] constexpr StormRect stormRectAt(const StormBox& box, float seconds) noexcept
	{
		const float t = std::clamp(seconds, 0.0f, (std::max)(box.moveSeconds, 0.0f));
		return { box.x1 + box.x1Speed * t, box.x2 + box.x2Speed * t, box.y1 + box.y1Speed * t, box.y2 + box.y2Speed * t };
	}

	[[nodiscard]] constexpr bool stormContains(std::span<const StormBox> boxes, float seconds, float x, float y) noexcept
	{
		return std::ranges::any_of(boxes, [&](const StormBox& box) { return stormRectAt(box, seconds).contains(x, y); });
	}
}

#endif
