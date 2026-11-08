/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#include "Combat/PacketUnits.h"
#include <bit>
#include "Combat/Validation.h"

namespace Cast::Combat
{
	namespace
	{
		constexpr std::uint8_t projectileExplosion = 1;
		constexpr std::uint8_t stateKillExtra = 1;

		template<class T>
		[[nodiscard]] std::optional<T> loadAt(const Packet& packet, std::size_t offset = 0)
		{
			if (packet.payload.size() < offset + sizeof(T))
			{
				return std::nullopt;
			}
			return load<T>(packet.payload, offset);
		}

		PacketUnits explosion(const Packet& packet)
		{
			PacketUnits units;
			if (const auto header = loadAt<Structures::ExplosionDamageHeader>(packet))
			{
				if (packet.extra == projectileExplosion)
				{
					units.ammoId = header->sourceId;
				}
				else
				{
					units.attacker = std::bit_cast<Structures::UniqueId>(header->sourceId);
				}
			}
			if (const auto first = loadAt<Structures::DamageEntry>(packet, sizeof(Structures::ExplosionDamageHeader)))
			{
				units.target = first->target;
			}
			return units;
		}

		PacketUnits effect(const Packet& packet)
		{
			if (packet.extra == stateKillExtra)
			{
				const auto kill = loadAt<Structures::StateKillPacket>(packet);
				return kill ? PacketUnits{ kill->attacker, std::nullopt, kill->victim } : PacketUnits{};
			}
			const auto use = loadAt<Structures::ItemEffectPacket>(packet);
			return use ? PacketUnits{ use->user, std::nullopt, use->target } : PacketUnits{};
		}

		PacketUnits multiHit(const Packet& packet)
		{
			PacketUnits units;
			if (const auto header = loadAt<Structures::MultiHitHeader>(packet))
			{
				units.attacker = header->attacker;
			}
			if (const auto first = loadAt<Structures::DamageEntry>(packet, sizeof(Structures::MultiHitHeader)))
			{
				units.target = first->target;
			}
			return units;
		}
	}

	PacketUnits packetUnits(const Packet& packet)
	{
		switch (packet.order)
		{
		case 264:
			return explosion(packet);
		case 265:
			return effect(packet);
		case 266:
		case 269:
			return multiHit(packet);
		case 267:
		case 268:
		case 270:
		{
			const auto shot = loadAt<Structures::SingleHitPacket>(packet);
			return shot ? PacketUnits{ shot->attacker, std::nullopt, shot->target } : PacketUnits{};
		}
		case 272:
		{
			const auto launch = loadAt<Structures::ProjectileLaunchPacket>(packet);
			return launch ? PacketUnits{ launch->owner, launch->ammoId, std::nullopt } : PacketUnits{};
		}
		default:
			return {};
		}
	}
}
