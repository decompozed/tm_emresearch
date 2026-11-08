#ifndef CAST_COMBAT_PACKET_UNITS_H
#define CAST_COMBAT_PACKET_UNITS_H

#include <cstdint>
#include <optional>
#include "Combat/World.h"
#include "Structures/Combat/CombatPackets.h"

namespace Cast::Combat
{
	// The units a combat packet names (the logs of dropped packets): the host sends the outcome of the other players'
	// shots, so the attacker is often not the sender.
	struct PacketUnits
	{
		std::optional<Structures::UniqueId> attacker;
		std::optional<std::uint32_t> ammoId; // order 264 of a projectile: the attacker is the explosive's owner
		std::optional<Structures::UniqueId> target; // the first target
	};

	[[nodiscard]] PacketUnits packetUnits(const Packet& packet);
}

#endif
