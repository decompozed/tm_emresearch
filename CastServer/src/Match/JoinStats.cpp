/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#include <array>
#include "Combat/Validation.h"
#include "Enums/GameEnums.h"
#include "Match/Rules.h"
#include "Match/Validation.h"
#include "Structures/Match/JoinStatePackets.h"

namespace Cast::Match
{
	namespace
	{
		constexpr std::size_t entrySize = sizeof(Structures::PlayerJoinStats);

		Structures::MatchStats copiedStats(const Structures::MatchStats& in) noexcept
		{
			Structures::MatchStats out{};
			out.meleeKills = in.meleeKills;
			out.rifleKills = in.rifleKills;
			out.shotgunKills = in.shotgunKills;
			out.sniperKills = in.sniperKills;
			out.mgKills = in.mgKills;
			out.bazookaKills = in.bazookaKills;
			out.grenadeKills = in.grenadeKills;
			out.totalKills = in.totalKills;
			out.totalDeaths = in.totalDeaths;
			out.headshots = in.headshots;
			out.assists = in.assists;
			out.modeCounter = in.modeCounter;
			out.roundsWon = in.roundsWon;
			return out;
		}

		Structures::PlayerJoinState joinState(const Structures::UniqueId& player, bool dead) noexcept
		{
			Structures::PlayerJoinState state{};
			state.player = player;
			state.hp = dead ? 0 : Structures::fullHp;
			state.weaponSlot = 0;
			state.state = dead ? Common::Enums::STATE_DYING : Common::Enums::STATE_NORMAL;
			state.unused = 0;
			return state;
		}

		template<class T, std::size_t N>
		std::span<const std::uint8_t> bytes(const std::array<T, N>& entries, std::size_t count) noexcept
		{
			return { reinterpret_cast<const std::uint8_t*>(entries.data()), count * sizeof(T) };
		}
	}

	Verdict joinStats(World& world, const Packet& packet)
	{
		if (const auto reason = checkHostToPlayer(world, packet); !reason.empty()) return Verdict::drop(reason);
		if (packet.extra != 0) return Verdict::drop("bad extra");
		const std::size_t count = packet.option;
		if (count == 0 || count > Structures::maxJoinPlayers || packet.payload.size() != count * entrySize)
		{
			return Verdict::drop("size does not match the entry count");
		}

		std::array<Structures::PlayerJoinStats, Structures::maxJoinPlayers> stats{};
		std::array<Structures::PlayerJoinState, Structures::maxJoinPlayers> states{};
		std::size_t known = 0;
		for (std::size_t i = 0; i < count; ++i)
		{
			const auto entry = Combat::load<Structures::PlayerJoinStats>(packet.payload, i * entrySize);
			if (entry.player.unknown != 0) return Verdict::drop("bad player key");
			stats[i].stats = copiedStats(entry.stats);
			stats[i].player = entry.player;
			stats[i].respawnTick = entry.respawnTick;
			if (const auto unit = Combat::resolve(world, entry.player); unit.isPlayer())
			{
				states[known++] = joinState(entry.player, unit.state.dead);
			}
		}

		if (!world.send({ Combat::Route::Recipient, packet.sender, packet.target, Structures::joinStatsOrder, 0, 0,
			static_cast<std::uint8_t>(count), bytes(stats, count) }))
		{
			return Verdict::drop("recipient not in the room");
		}
		if (known > 0)
		{
			world.send({ Combat::Route::Recipient, packet.sender, packet.target, Structures::joinStatsOrder, Structures::serverJoinStateMission, 0,
				static_cast<std::uint8_t>(known), bytes(states, known) });
		}
		return Verdict::to(Combat::Route::Handled, packet);
	}
}
