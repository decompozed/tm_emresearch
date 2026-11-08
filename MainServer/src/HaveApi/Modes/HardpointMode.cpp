/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#include "HaveApi/Modes/HardpointMode.h"
#include <algorithm>
#include <cmath>
#include <iterator>
#include <numeric>

namespace Main::HaveApi
{
	namespace
	{
		constexpr std::size_t zoneTimeSetting = 0;
		constexpr std::size_t pointsSetting = 1;
	}

	void assignBalancedTeams(MatchContext& match, std::mt19937& random)
	{
		std::vector<PartyPlayer*> order;
		for (PartyPlayer& player : match.players())
		{
			order.push_back(&player);
		}
		std::ranges::shuffle(order, random);
		const std::int32_t first = std::uniform_int_distribution<int>{ 0, 1 }(random) == 0 ? HardpointMode::redTeam : HardpointMode::blueTeam;
		const std::int32_t second = first == HardpointMode::redTeam ? HardpointMode::blueTeam : HardpointMode::redTeam;
		for (std::size_t i = 0; i < order.size(); ++i)
		{
			order[i]->fallbackTeam = i % 2 == 0 ? first : second;
			order[i]->team = order[i]->fallbackTeam;
		}
	}

	std::size_t spotFarthestFromBases(std::span<const Common::HaveApi::ZoneSpot> spots, std::uint32_t mapId, std::size_t fallback)
	{
		std::size_t best = fallback;
		float bestDistance = -1.0f;
		const auto bases = Common::HaveApi::findMapSpawns(mapId);
		for (std::size_t i = 0; i < spots.size(); ++i)
		{
			float nearestBase = 1e30f;
			for (const Common::HaveApi::MapSpawn& spawn : bases)
			{
				if (spawn.team != 0)
				{
					nearestBase = (std::min)(nearestBase, std::hypot(spawn.x - spots[i].x, spawn.y - spots[i].y));
				}
			}
			if (nearestBase < 1e30f && nearestBase > bestDistance)
			{
				bestDistance = nearestBase;
				best = i;
			}
		}
		return best;
	}

	std::size_t spotAwayFromSpawns(std::span<const Common::HaveApi::ZoneSpot> spots, std::span<const SpawnPoint> spawns,
		float keepOut, std::size_t current, std::mt19937& random)
	{
		std::vector<std::size_t> candidates;
		for (std::size_t i = 0; i < spots.size(); ++i)
		{
			if (i != current || spots.size() == 1)
			{
				candidates.push_back(i);
			}
		}
		if (candidates.empty())
		{
			return current;
		}
		const auto nearestSpawn = [&](std::size_t i) {
			float nearest = 1e30f;
			for (const SpawnPoint& spawn : spawns)
			{
				nearest = (std::min)(nearest, std::hypot(spawn.x - spots[i].x, spawn.y - spots[i].y));
			}
			return nearest;
		};
		std::vector<std::size_t> clear;
		std::ranges::copy_if(candidates, std::back_inserter(clear), [&](std::size_t i) { return nearestSpawn(i) > keepOut; });
		if (!clear.empty())
		{
			return clear[std::uniform_int_distribution<std::size_t>{ 0, clear.size() - 1 }(random)];
		}
		return *std::ranges::max_element(candidates, {}, nearestSpawn); // every spot near a spawn: the least near
	}

	std::int32_t smallerOfTwoTeams(std::span<const PartyPlayer> players)
	{
		std::array<std::int32_t, 3> sizes{};
		for (const PartyPlayer& player : players)
		{
			if (player.fallbackTeam == HardpointMode::redTeam || player.fallbackTeam == HardpointMode::blueTeam)
			{
				++sizes[static_cast<std::size_t>(player.fallbackTeam)];
			}
		}
		return sizes[HardpointMode::redTeam] <= sizes[HardpointMode::blueTeam] ? HardpointMode::redTeam : HardpointMode::blueTeam;
	}

	HardpointMode::HardpointMode() : m_random{ std::random_device{}() } {}
	HardpointMode::HardpointMode(std::uint32_t seed) : m_random{ seed } {}

	std::chrono::seconds HardpointMode::zoneTime(const MatchContext& match)
	{
		return std::chrono::seconds{ (std::max)(10, match.settings().values[zoneTimeSetting]) };
	}

	std::int32_t HardpointMode::pointsLimit(const MatchContext& match)
	{
		return (std::max)(1, match.settings().values[pointsSetting]);
	}

	void HardpointMode::onMatchStart(MatchContext& match)
	{
		assignBalancedTeams(match, m_random);
		const auto spots = Common::HaveApi::findHardpointZones(match.mapId());
		m_spots.assign(spots.begin(), spots.end());
		if (m_spots.empty())
		{
			m_spots.push_back({ 0.0f, 0.0f, 0.0f }); // unknown map: the map centre
		}
		m_points = {};
		m_teamSpawns.reset(match.mapId());
	}

	// The first zone: the spot farthest from both teams' bases (the map's TDM spawn nodes), so nobody starts in it. The
	// zone goes to CastServer now, before anybody loads in (occupants, spawn grace from the first spawn on).
	void HardpointMode::onRoundStart(MatchContext& match)
	{
		reviveAll(match);
		m_state = ZoneState::Free;
		m_spot = spotFarthestFromBases(m_spots, match.mapId(),
			std::uniform_int_distribution<std::size_t>{ 0, m_spots.size() - 1 }(m_random));
		m_zoneSince = match.roundElapsed();
		m_warned = false;
		match.setZoneScore(m_points[0], m_points[1], pointsLimit(match));
		const auto& spot = m_spots[m_spot];
		match.placeZone(true, spot.x, spot.y, zoneRadius, respawnKeepOut);
		sendZone(match);
		updateSpawns(match);
	}

	void HardpointMode::sendZone(MatchContext& match)
	{
		const auto& spot = m_spots[m_spot];
		match.showZone(true, spot.x, spot.y, static_cast<std::int32_t>(m_state));
	}

	void HardpointMode::moveZone(MatchContext& match, bool announceMove)
	{
		m_spot = spotAwayFromSpawns(m_spots, match.spawnPoints(), respawnKeepOut, m_spot, m_random);
		m_zoneSince = match.roundElapsed();
		m_warned = false;
		m_state = ZoneState::Free;
		const auto& spot = m_spots[m_spot];
		match.placeZone(true, spot.x, spot.y, zoneRadius, respawnKeepOut);
		sendZone(match);
		if (announceMove)
		{
			match.announce(Common::HaveApi::Announcement::HardpointZoneMoved);
		}
	}

	void HardpointMode::onTick(MatchContext& match)
	{
		tickZone(match);
		updateSpawns(match);
	}

	// Each player its team's base spawns: at the round start, then for late joiners / team changes.
	void HardpointMode::updateSpawns(MatchContext& match)
	{
		m_teamSpawns.update(match);
	}

	void HardpointMode::tickZone(MatchContext& match)
	{
		const auto now = match.roundElapsed();
		if (now - m_zoneSince >= zoneTime(match))
		{
			moveZone(match, true);
			return;
		}
		if (!m_warned && now - m_zoneSince >= zoneTime(match) - moveWarning)
		{
			m_warned = true;
			match.announce(Common::HaveApi::Announcement::HardpointZoneMoving);
		}

		// who holds the zone: the living players the CastServer sees inside
		const auto inside = match.playersInZone();
		bool red = false, blue = false;
		std::vector<PartyPlayer*> holders;
		for (PartyPlayer& player : match.players())
		{
			if (!player.alive || std::ranges::find(inside, player.playerId) == inside.end())
			{
				continue;
			}
			red = red || player.fallbackTeam == redTeam;
			blue = blue || player.fallbackTeam == blueTeam;
			holders.push_back(&player);
		}
		const ZoneState state = red && blue ? ZoneState::Contested : red ? ZoneState::Red : blue ? ZoneState::Blue : ZoneState::Free;
		if (state != m_state)
		{
			m_state = state;
			sendZone(match);
			if (state == ZoneState::Red)
				match.announce(Common::HaveApi::Announcement::HardpointRedHolding);
			else if (state == ZoneState::Blue)
				match.announce(Common::HaveApi::Announcement::HardpointBlueHolding);
			else if (state == ZoneState::Contested)
				match.announce(Common::HaveApi::Announcement::HardpointContested);
		}
		if (state == ZoneState::Red || state == ZoneState::Blue)
		{
			auto& teamPoints = m_points[state == ZoneState::Red ? 0 : 1];
			teamPoints = (std::min)(teamPoints + pointsPerSecond, pointsLimit(match));
			match.setZoneScore(m_points[0], m_points[1], pointsLimit(match));
			for (PartyPlayer* holder : holders)
			{
				holder->points += pointsPerSecond;
				match.updatePoints(*holder);
			}
		}
	}

	void HardpointMode::onDeath(MatchContext& match, PartyPlayer& victim, PartyPlayer* killer)
	{
		if (killer != nullptr && killer != &victim && killer->fallbackTeam != victim.fallbackTeam)
		{
			scoreKill(match, *killer);
		}
	}

	bool HardpointMode::isRoundOver(MatchContext& match)
	{
		return m_points[0] >= pointsLimit(match) || m_points[1] >= pointsLimit(match);
	}

	std::int32_t HardpointMode::winner() const
	{
		if (m_points[0] == m_points[1])
		{
			return 0;
		}
		return m_points[0] > m_points[1] ? redTeam : blueTeam;
	}

	void HardpointMode::onRoundEnd(MatchContext& match, RoundEndReason)
	{
		match.showZone(false, 0.0f, 0.0f, 0);
		match.placeZone(false, 0.0f, 0.0f, 0.0f, 0.0f);
		if (const std::int32_t team = winner(); team != 0)
		{
			for (PartyPlayer& player : match.players())
			{
				if (player.fallbackTeam == team)
				{
					player.points += match.rules().lastStandingPoints;
					match.updatePoints(player);
				}
			}
			match.announce(Common::HaveApi::teamWon(team));
		}
	}

	void HardpointMode::onMatchEnd(MatchContext& match)
	{
		if (const std::int32_t team = winner(); team != 0)
		{
			match.announce(Common::HaveApi::teamWon(team));
		}
	}

	std::optional<std::pair<Common::HaveApi::Announcement, std::string>> HardpointMode::roundStartAnnouncement(const MatchContext&) const
	{
		return std::pair{ Common::HaveApi::Announcement::HardpointStart, std::string{} };
	}

	std::int32_t HardpointMode::teamForLateJoiner(MatchContext& match)
	{
		return smallerOfTwoTeams(match.players());
	}
}
