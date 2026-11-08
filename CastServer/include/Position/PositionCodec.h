#ifndef CAST_POSITION_POSITION_CODEC_H
#define CAST_POSITION_POSITION_CODEC_H

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include "Structures/Position/PositionPackets.h"

namespace Cast::Position
{
	[[nodiscard]] std::size_t reportSize(const Structures::PositionReport& report) noexcept;

	[[nodiscard]] std::string_view decode(std::uint8_t mission, std::uint8_t extra, std::uint8_t option,
		std::span<const std::uint8_t> payload, Structures::PlayerPosition& out) noexcept;

	[[nodiscard]] std::size_t encode(std::span<std::uint8_t, Structures::maxPositionEntrySize> out, std::uint32_t sessionId,
		const Structures::PlayerPosition& position, bool hidden) noexcept;
}

#endif
