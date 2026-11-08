#ifndef CAST_MATCH_VALIDATION_H
#define CAST_MATCH_VALIDATION_H

#include <cstdint>
#include <string_view>
#include "Combat/Validation.h"

namespace Cast::Match
{
	[[nodiscard]] constexpr bool isEmptyCommand(const Combat::Packet& packet) noexcept
	{
		return Combat::hasEmptyHeader(packet) && packet.payload.empty();
	}

	[[nodiscard]] Structures::UniqueId playerId(const Combat::World& world, std::uint32_t session) noexcept;

	[[nodiscard]] std::string_view checkMember(const Combat::World& world, const Combat::Packet& packet);
	[[nodiscard]] std::string_view checkPlayerRequest(const Combat::World& world, const Combat::Packet& packet);
	[[nodiscard]] std::string_view checkHostToPlayer(const Combat::World& world, const Combat::Packet& packet);
	[[nodiscard]] std::string_view checkHostState(const Combat::World& world, const Combat::Packet& packet);
}

#endif
