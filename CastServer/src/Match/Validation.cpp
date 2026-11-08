/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#include "Match/Validation.h"

namespace Cast::Match
{
	Structures::UniqueId playerId(const Combat::World& world, std::uint32_t session) noexcept
	{
		Structures::UniqueId id{};
		id.session = session & 0xFFFF;
		id.server = world.serverId() & 0x7FFF;
		return id;
	}

	std::string_view checkMember(const Combat::World& world, const Combat::Packet& packet)
	{
		return world.isAuthorizedMember(packet.sender) ? std::string_view{} : "sender is not a room member";
	}

	std::string_view checkPlayerRequest(const Combat::World& world, const Combat::Packet& packet)
	{
		if (const auto reason = checkMember(world, packet); !reason.empty()) return reason;
		if (world.isHost(packet.sender)) return "request from the host itself";
		return {};
	}

	std::string_view checkHostState(const Combat::World& world, const Combat::Packet& packet)
	{
		if (!world.isHost(packet.sender)) return "sender is not the room host";
		if (!isEmptyCommand(packet)) return "bad header or size";
		return {};
	}

	std::string_view checkHostToPlayer(const Combat::World& world, const Combat::Packet& packet)
	{
		if (!world.isHost(packet.sender)) return "sender is not the room host";
		if (packet.mission != 0) return "bad mission";
		if (!world.isAuthorizedMember(packet.target)) return "recipient is not a room member";
		if (world.isHost(packet.target)) return "recipient is the host";
		return {};
	}
}
