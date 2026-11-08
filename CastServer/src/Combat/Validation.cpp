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
#include <algorithm>
#include <array>
#include "Enums/RoomEnums.h"

namespace Cast::Combat
{
	namespace
	{
		constexpr std::size_t entrySize = sizeof(Structures::DamageEntry);
		constexpr std::size_t maxEntries = Structures::maxPayloadSize / entrySize;

		bool diedInThisPacket(const World& world, std::uint32_t session)
		{
			const auto state = world.player(session);
			return !state || state->dead;
		}
	}

	bool allowsNpcs(std::uint32_t mode) noexcept
	{
		return mode == Common::Enums::AiBattle || mode == Common::Enums::BossBattle;
	}

	Unit resolve(const World& world, const Structures::UniqueId& id)
	{
		if (id.server == world.serverId())
		{
			if (const auto state = world.player(id.session))
			{
				return { UnitKind::Player, id.session, *state };
			}
		}
		if (allowsNpcs(world.mode()) && rawId(id) != 0)
		{
			return { UnitKind::Npc };
		}
		return {};
	}

	std::string_view checkHostOutcome(const World& world, const Packet& packet)
	{
		return world.isHost(packet.sender) ? std::string_view{} : "sender is not the room host";
	}

	std::string_view checkMatchRequester(const World& world, const Packet& packet)
	{
		if (!world.isAuthorizedMember(packet.sender)) return "sender is not a room member";
		if (world.isHost(packet.sender)) return "request from the host itself";
		const auto state = world.player(packet.sender);
		if (!state || !state->inMatch) return "sender is not in the match";
		return {};
	}

	std::string_view checkLiveRequester(const World& world, const Packet& packet)
	{
		if (const auto reason = checkMatchRequester(world, packet); !reason.empty()) return reason;
		if (world.player(packet.sender)->observer) return "sender is an observer";
		return {};
	}

	std::string_view checkRequester(const World& world, const Packet& packet)
	{
		if (packet.mission != 0) return "mission is not 0";
		return checkLiveRequester(world, packet);
	}

	std::string_view checkSnapshot(const World& world, const Packet& packet)
	{
		if (const auto reason = checkHostOutcome(world, packet); !reason.empty()) return reason;
		if (world.isHost(packet.target) || !world.isAuthorizedMember(packet.target) || !world.player(packet.target)) return "recipient is not a room member";
		return {};
	}

	bool isRoomPlayer(const World& world, const Structures::UniqueId& id)
	{
		return id.unknown == 0 && resolve(world, id).isPlayer();
	}

	void reviveIfStale(World& world, Unit& unit)
	{
		if (unit.staleDeadPlayer())
		{
			world.revive(unit.session);
			unit.state.dead = false;
			unit.state.staleDeath = false;
		}
	}

	bool spareLethalHit(const World& world, std::optional<std::uint32_t> attacker, std::uint32_t target, Structures::HpField& hp)
	{
		if (hp.hp != 0 || !attacker || *attacker == target || !world.sparesPlayers())
		{
			return false;
		}
		hp.hp = 1;
		return true;
	}

	void reportDamage(World& world, std::uint32_t target, const Structures::HpField& hp, std::optional<std::uint32_t> attacker, Cause cause, bool spared)
	{
		if (hp.hp == 0)
		{
			world.death(target, attacker, cause);
		}
		else
		{
			world.hit(target, attacker, cause, spared);
		}
	}

	Verdict applyDamageEntries(World& world, const Packet& packet, std::size_t headerSize, const EntryRules& rules)
	{
		const std::size_t count = packet.option;
		if (count > maxEntries || packet.payload.size() != headerSize + count * entrySize || packet.payload.size() > Structures::maxPayloadSize)
		{
			return Verdict::drop("size does not match the entry count");
		}

		std::array<Unit, maxEntries> units{};
		for (std::size_t i = 0; i < count; ++i)
		{
			const auto entry = load<Structures::DamageEntry>(packet.payload, headerSize + i * entrySize);
			if (entry.hp.reserved != 0) return Verdict::drop("reserved HP bits set");
			if (rules.excludedTarget && rawId(entry.target) == *rules.excludedTarget) return Verdict::drop("attacker among the targets");
			units[i] = resolve(world, entry.target);
			if (!units[i].inMatch()) return Verdict::drop("target is not in the match");
		}

		for (std::size_t i = 0; i < count; ++i)
		{
			auto entry = load<Structures::DamageEntry>(packet.payload, headerSize + i * entrySize);
			if (const auto target = units[i].playerSession())
			{
				const bool spared = spareLethalHit(world, rules.attacker, *target, entry.hp);
				if (!units[i].deadPlayer() && !diedInThisPacket(world, *target))
				{
					reportDamage(world, *target, entry.hp, rules.attacker, rules.cause, spared);
				}
			}
			store(packet.payload, headerSize + i * entrySize, entry);
		}
		return Verdict::to(Route::Match, packet);
	}
}
