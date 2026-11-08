/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#include <algorithm>
#include <array>
#include "Match/Rules.h"
#include "Match/Validation.h"
#include "Structures/Match/JoinStatePackets.h"

namespace Cast::Match
{
	namespace
	{
		using Structures::ModeSyncLayout;

		constexpr std::size_t headerSize = sizeof(std::uint32_t);
		constexpr std::size_t entrySize = sizeof(Structures::ModeSyncEntry);

		Verdict request(const World& world, const Packet& packet)
		{
			if (const auto reason = checkPlayerRequest(world, packet); !reason.empty()) return Verdict::drop(reason);
			if (packet.mission != 0 || packet.extra != Structures::modeSyncRequestExtra || packet.option != 0 || !packet.payload.empty())
			{
				return Verdict::drop("bad header or size");
			}
			return Verdict::to(Combat::Route::Host, packet);
		}

		bool countMatches(ModeSyncLayout layout, const Packet& packet)
		{
			switch (layout)
			{
			case ModeSyncLayout::Team:
			case ModeSyncLayout::TeamUntilEmptyEntry:
				return Combat::load<Structures::TeamModeSyncHeader>(packet.payload).count == packet.option;
			case ModeSyncLayout::Solo:
				return Combat::load<std::uint32_t>(packet.payload) == packet.option;
			case ModeSyncLayout::Scrimmage:
				return packet.option > 0 && Combat::load<Structures::ScrimmageModeSyncHeader>(packet.payload).count == packet.option;
			case ModeSyncLayout::Unread:
				return true;
			}
			return false;
		}

		Verdict withEmptyEntry(World& world, const Packet& packet)
		{
			std::array<std::uint8_t, headerSize + (Structures::maxJoinPlayers + 1) * entrySize> out{};
			std::ranges::copy(packet.payload, out.begin());
			if (!world.send({ Combat::Route::Recipient, packet.sender, packet.target, Structures::modeSyncOrder, 0, 0, packet.option,
				{ out.data(), packet.payload.size() + entrySize } }))
			{
				return Verdict::drop("recipient not in the room");
			}
			return Verdict::to(Combat::Route::Handled, packet);
		}

		Verdict reply(World& world, const Packet& packet)
		{
			if (const auto reason = checkHostToPlayer(world, packet); !reason.empty()) return Verdict::drop(reason);
			if (packet.extra != 0) return Verdict::drop("bad extra");
			const std::size_t count = packet.option;
			if (count > Structures::maxJoinPlayers || packet.payload.size() != headerSize + count * entrySize)
			{
				return Verdict::drop("size does not match the entry count");
			}
			if (!world.modeKnown() || world.mode() >= Structures::modeSyncLayouts.size()) return Verdict::drop("room mode unknown");
			const auto layout = Structures::modeSyncLayouts[world.mode()];
			if (!countMatches(layout, packet)) return Verdict::drop("entry count field does not match the option");
			if (layout == ModeSyncLayout::TeamUntilEmptyEntry && count < Structures::maxJoinPlayers) return withEmptyEntry(world, packet);
			return Verdict::to(Combat::Route::Recipient, packet);
		}
	}

	Verdict modeSync(World& world, const Packet& packet)
	{
		return world.isHost(packet.sender) ? reply(world, packet) : request(world, packet);
	}
}
