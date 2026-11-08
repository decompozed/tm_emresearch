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

namespace Cast::Combat
{
	namespace
	{
		constexpr std::uint16_t meleeRequest = 149;
		constexpr std::uint16_t rifleRequest = 152;

		bool isSender(const World& world, const Packet& packet, const Structures::UniqueId& id)
		{
			return id.session == packet.sender && id.server == world.serverId();
		}

		std::string_view checkHitEntry(const World& world, const Packet& packet, const Structures::HitRequestEntry& entry)
		{
			if (rawId(entry.target) == 0 || isSender(world, packet, entry.target)) return "bad target";
			if (!isFinite(entry.hitPoint)) return "hit point is not finite";
			if (!resolve(world, entry.target).inMatch()) return "target is not in the match";
			return {};
		}

		std::string_view checkSingleHit(const World& world, const Packet& packet)
		{
			const bool meleeSwing = packet.order == meleeRequest && packet.payload.size() == sizeof(Structures::ShotTrace);
			if (meleeSwing)
			{
				if (packet.extra != 0 || packet.option > 1) return "bad extra or option";
				const auto trace = load<Structures::ShotTrace>(packet.payload);
				return trace.tag == 0 && isFinite(trace) ? std::string_view{} : "bad trace";
			}
			if (packet.payload.size() != sizeof(Structures::HitRequest)) return "bad size";
			if (packet.extra != 1 || packet.option > (packet.order == rifleRequest ? 0 : 1)) return "bad extra or option";
			const auto request = load<Structures::HitRequest>(packet.payload);
			if (request.trace.tag != 0 || !isFinite(request.trace)) return "bad trace";
			if (request.hit.unused != 0) return "unused bits set";
			return checkHitEntry(world, packet, request.hit);
		}
	}

	Verdict itemDamageRequest(World& world, const Packet& packet)
	{
		if (const auto reason = checkRequester(world, packet); !reason.empty()) return Verdict::drop(reason);
		if (packet.extra != 0 || packet.option != 0) return Verdict::drop("bad header");
		if (packet.payload.size() != sizeof(Structures::ItemDamageRequest)) return Verdict::drop("bad size");
		if (!resolve(world, load<Structures::ItemDamageRequest>(packet.payload).target).inMatch()) return Verdict::drop("target is not in the match");
		return Verdict::to(Route::Host, packet);
	}

	Verdict multiHitRequest(World& world, const Packet& packet)
	{
		if (const auto reason = checkRequester(world, packet); !reason.empty()) return Verdict::drop(reason);
		if (packet.extra != 1 || packet.option == 0 || packet.option > Structures::maxShotEntries) return Verdict::drop("bad extra or option");
		constexpr std::size_t entrySize = sizeof(Structures::HitRequestEntry);
		if (packet.payload.size() != sizeof(Structures::ShotTrace) + packet.option * entrySize) return Verdict::drop("size does not match the entry count");
		if (!isFinite(load<Structures::ShotTrace>(packet.payload))) return Verdict::drop("trace is not finite");
		for (std::size_t i = 0; i < packet.option; ++i)
		{
			const auto entry = load<Structures::HitRequestEntry>(packet.payload, sizeof(Structures::ShotTrace) + i * entrySize);
			if (const auto reason = checkHitEntry(world, packet, entry); !reason.empty()) return Verdict::drop(reason);
		}
		return Verdict::to(Route::Host, packet);
	}

	Verdict singleHitRequest(World& world, const Packet& packet)
	{
		if (const auto reason = checkRequester(world, packet); !reason.empty()) return Verdict::drop(reason);
		if (const auto reason = checkSingleHit(world, packet); !reason.empty()) return Verdict::drop(reason);
		return Verdict::to(Route::Host, packet);
	}
}
