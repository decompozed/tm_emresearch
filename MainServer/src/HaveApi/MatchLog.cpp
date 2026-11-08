/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#include "HaveApi/MatchLog.h"
#include <algorithm>
#include <format>
#include "HaveApi/ModeLog.h"

namespace Main::HaveApi
{
	namespace CH = Common::HaveApi;

	void MatchLog::write(std::string_view event) const
	{
		CH::writeModeLog(m_modeId, m_room, m_round, event);
	}

	bool MatchLog::changed(const std::string& key, std::string value)
	{
		auto& last = m_last[key];
		if (last == value)
		{
			return false;
		}
		last = std::move(value);
		return true;
	}

	bool MatchLog::changedAtMostEvery(const std::string& key, std::string value, Clock::duration interval, Clock::time_point now)
	{
		const auto at = m_lastAt.find(key);
		if (at != m_lastAt.end() && now - at->second < interval)
		{
			return false;
		}
		if (!changed(key, std::move(value)))
		{
			return false;
		}
		m_lastAt[key] = now;
		return true;
	}

	void MatchLog::hit(const PartyPlayer& attacker, const PartyPlayer& victim, CH::DeathCause cause)
	{
		m_hits.add(attacker.sessionId, victim.sessionId, cause);
	}

	void MatchLog::writeHits(std::span<const PartyPlayer> players)
	{
		const auto nameOf = [&](std::uint32_t session) {
			const auto found = std::ranges::find(players, session, &PartyPlayer::sessionId);
			return found != players.end() ? found->name : std::string{ "?" };
		};
		for (const auto& entry : m_hits.take())
		{
			write(std::format("hits {} (session {}) -> {} (session {}): {} (weapon {}, explosive {}, other {})", nameOf(entry.attacker),
				entry.attacker, nameOf(entry.victim), entry.victim, entry.total(), entry.weapon, entry.explosive, entry.other));
		}
	}

	std::string MatchLog::describe(const PartyPlayer& player)
	{
		return std::format("{} (id {}, session {}, team {}, fallback {}, {}, {}, points {}, kills {}, deaths {}{})", player.name, player.playerId,
			player.sessionId, player.team, player.fallbackTeam, player.alive ? "alive" : "dead", player.loaded ? "loaded" : "loading", player.points,
			player.kills, player.deaths, player.placement != 0 ? std::format(", placement {}", player.placement) : std::string{});
	}

	std::string MatchLog::describe(std::span<const PartyPlayer> players)
	{
		std::string text;
		for (const PartyPlayer& player : players)
		{
			text += (text.empty() ? "" : "; ") + describe(player);
		}
		return text.empty() ? std::string{ "nobody" } : text;
	}

	std::string MatchLog::describe(const CH::LavaCommand& command)
	{
		return std::format("action {}, walls {}, floor speed {:.2f}, walls close in {:.1f} s, factor {:.2f}", static_cast<int>(command.action),
			command.wallsOn, command.floorSpeed, command.wallCloseSeconds, command.speedFactor);
	}

	std::string MatchLog::describe(const CH::LavaState& state)
	{
		return std::format("active {}, {}, surface {:.1f} speed {:.2f}, walls {} x [{:.1f}, {:.1f}] y [{:.1f}, {:.1f}] speeds {:.2f} {:.2f} {:.2f} {:.2f}",
			state.active, state.fromMap ? "from the map spawns / bounds" : "from the player positions", state.floorZ, state.floorSpeed,
			state.wallsOn ? "on" : "off", state.wallX1, state.wallX2, state.wallY1, state.wallY2, state.wallX1Speed, state.wallX2Speed,
			state.wallY1Speed, state.wallY2Speed);
	}

	std::string MatchLog::describe(const CH::Modifiers& modifiers)
	{
		return std::format("canDamageOthers {}, outgoing damage {:.2f}, movement scale {:.2f} (override {}), max health override {} scale {:.2f}, "
			"self damage {}, incoming damage {:.2f}, marker range {}", modifiers.head.canDamageOthers, modifiers.head.outgoingDamage,
			modifiers.tail.movementSpeedScale, modifiers.tail.movementSpeedOverride, modifiers.tail.maxHealthOverride, modifiers.tail.maxHealthScale,
			modifiers.tail.selfDamageAllowed, modifiers.incomingDamage, modifiers.markerRange);
	}

	std::string_view MatchLog::causeName(CH::DeathCause cause)
	{
		switch (cause)
		{
		case CH::DeathCause::None: return "none";
		case CH::DeathCause::Weapon: return "weapon";
		case CH::DeathCause::Explosive: return "explosive / fall / self";
		case CH::DeathCause::Server: return "server (lava, walls, party rules)";
		}
		return "unknown";
	}
}
