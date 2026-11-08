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
		constexpr std::uint8_t idlessLaunch = 4;

		std::string_view checkLaunch(const Packet& packet, const Structures::ShotTrace& trace)
		{
			if (packet.option < 1 || packet.option > 4) return "weapon slot out of 1..4";
			if (packet.extra != 0 && packet.extra != 1 && packet.extra != idlessLaunch) return "unknown extra";
			if (trace.tag != 0 && (packet.option != 1 || packet.extra != 0)) return "unexpected trace tag";
			if (!isFinite(trace)) return "trace is not finite";
			return {};
		}
	}

	Verdict projectileLaunch(World& world, const Packet& packet)
	{
		if (const auto reason = checkHostOutcome(world, packet); !reason.empty()) return Verdict::drop(reason);
		if (packet.mission != 0 || packet.payload.size() != sizeof(Structures::ProjectileLaunchPacket)) return Verdict::drop("bad mission or size");
		const auto launch = load<Structures::ProjectileLaunchPacket>(packet.payload);
		if (const auto reason = checkLaunch(packet, launch.trace); !reason.empty()) return Verdict::drop(reason);
		if ((launch.ammoId == 0) != (packet.extra == idlessLaunch)) return Verdict::drop("bad ammo id");

		const Unit owner = resolve(world, launch.owner);
		if (!owner.inMatch()) return Verdict::drop("owner is not in the match");
		if (const auto session = owner.playerSession(); session && launch.ammoId != 0)
		{
			world.rememberExplosive(launch.ammoId, *session);
		}
		return Verdict::to(Route::Match, packet);
	}

	Verdict projectileRequest(World& world, const Packet& packet)
	{
		if (const auto reason = checkRequester(world, packet); !reason.empty()) return Verdict::drop(reason);
		if (packet.payload.size() != sizeof(Structures::ShotTrace)) return Verdict::drop("bad size");
		if (const auto reason = checkLaunch(packet, load<Structures::ShotTrace>(packet.payload)); !reason.empty()) return Verdict::drop(reason);
		return Verdict::to(Route::Host, packet);
	}
}
