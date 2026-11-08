/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#include "Combat/Validation.h"
#include "Match/Rules.h"
#include "Match/Validation.h"
#include "Structures/Match/JoinStatePackets.h"

namespace Cast::Match
{
	namespace
	{
		constexpr std::size_t entrySize = sizeof(Structures::UnitStateEntry);

		Structures::UnitStateEntry copiedState(const Structures::UnitStateEntry& in) noexcept
		{
			Structures::UnitStateEntry out{};
			out.state = in.state;
			out.weaponType = in.weaponType;
			out.hpMasked = in.hpMasked;
			out.hpMaskedNot = in.hpMaskedNot;
			out.hpKey = in.hpKey;
			out.character = in.character;
			out.unit = in.unit;
			out.respawnTick = in.respawnTick;
			return out;
		}
	}

	Verdict unitStates(World& world, const Packet& packet)
	{
		if (const auto reason = checkHostToPlayer(world, packet); !reason.empty()) return Verdict::drop(reason);
		if (packet.extra != 0) return Verdict::drop("bad extra");
		const std::size_t count = packet.option;
		if (count == 0 || count > Structures::maxUnitStates || packet.payload.size() != count * entrySize)
		{
			return Verdict::drop("size does not match the entry count");
		}
		for (std::size_t i = 0; i < count; ++i)
		{
			const auto entry = Combat::load<Structures::UnitStateEntry>(packet.payload, i * entrySize);
			if (entry.unit.unknown != 0) return Verdict::drop("bad unit key");
			Combat::store(packet.payload, i * entrySize, copiedState(entry));
		}
		return Verdict::to(Combat::Route::Recipient, packet);
	}
}
