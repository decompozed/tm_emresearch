/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#include "Combat/Rules.h"
#include "Combat/Validation.h"
#include "Structures/Items/ItemPackets.h"

namespace Cast::Combat
{
	Verdict inventorySnapshot(World& world, const Packet& packet)
	{
		if (const auto reason = checkSnapshot(world, packet); !reason.empty()) return Verdict::drop(reason);
		if (packet.mission != 0 || packet.extra != 0) return Verdict::drop("bad header");
		if (packet.option > Structures::maxInventoryBlocks) return Verdict::drop("too many inventories");
		std::size_t offset = 0;
		for (std::size_t block = 0; block < packet.option; ++block)
		{
			if (packet.payload.size() - offset < sizeof(Structures::InventoryBlockHeader)) return Verdict::drop("truncated inventory");
			const auto header = load<Structures::InventoryBlockHeader>(packet.payload, offset);
			if (header.unit.unknown != 0) return Verdict::drop("bad unit key");
			offset += sizeof(Structures::InventoryBlockHeader);
			if (header.count > (packet.payload.size() - offset) / sizeof(Structures::InventoryItem)) return Verdict::drop("truncated inventory");
			offset += header.count * sizeof(Structures::InventoryItem);
		}
		if (offset != packet.payload.size()) return Verdict::drop("size does not match the inventories");
		return Verdict::to(Route::Recipient, packet);
	}
}
