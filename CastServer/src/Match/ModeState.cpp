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
#include "Combat/Validation.h"
#include "Match/Rules.h"
#include "Match/Validation.h"
#include "Structures/Match/JoinStatePackets.h"

namespace Cast::Match
{
	namespace
	{
		template<class T, class Fn>
		bool parses(std::span<const std::uint8_t> payload, Fn&& reservedClear)
		{
			return payload.size() == sizeof(T) && reservedClear(Combat::load<T>(payload));
		}

		bool validState(Structures::ModeStateLayout layout, std::span<const std::uint8_t> payload)
		{
			using namespace Structures;
			switch (layout)
			{
			case ModeStateLayout::Team:
				return parses<TeamModeState>(payload, [](const TeamModeState& s) { return s.reserved == 0; });
			case ModeStateLayout::Solo:
				return parses<SoloModeState>(payload, [](const SoloModeState& s) { return s.reserved == 0; });
			case ModeStateLayout::Point:
				return parses<PointModeState>(payload, [](const PointModeState& s) { return s.reserved == 0; });
			case ModeStateLayout::Bomb:
				return parses<BombModeState>(payload, [](const BombModeState& s) { return s.team.reserved == 0 && s.planter.unknown == 0; });
			case ModeStateLayout::Pve:
				return parses<PveModeState>(payload, [](const PveModeState& s) {
					return s.reservedBits == 0 && std::ranges::all_of(s.reserved, [](std::uint8_t b) { return b == 0; });
				});
			}
			return false;
		}
	}

	Verdict modeState(World& world, const Packet& packet)
	{
		if (const auto reason = checkHostToPlayer(world, packet); !reason.empty()) return Verdict::drop(reason);
		if (packet.extra != Structures::joinStateExtra) return Verdict::drop("bad extra");
		if (packet.option >= Structures::modeStateLayouts.size()) return Verdict::drop("bad mode type");
		if (world.modeKnown() && packet.option != world.mode()) return Verdict::drop("mode type is not the room mode");
		if (!validState(Structures::modeStateLayouts[packet.option], packet.payload)) return Verdict::drop("bad size or reserved bits");
		return Verdict::to(Combat::Route::Recipient, packet);
	}
}
