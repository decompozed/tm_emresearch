/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#include "HaveApi/Modes/DominationMode.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include "HaveApi/Modes/HardpointMode.h"

namespace Main::HaveApi
{
	namespace
	{
		namespace CH = Common::HaveApi;

		constexpr std::size_t pointsSetting = 0;

		[[nodiscard]] std::int32_t otherTeam(std::int32_t team)
		{
			return team == DominationMode::redTeam ? DominationMode::blueTeam : DominationMode::redTeam;
		}

		[[nodiscard]] CH::Announcement zoneAnnouncement(CH::Announcement first, std::size_t index)
		{
			return static_cast<CH::Announcement>(static_cast<std::int32_t>(first) + static_cast<std::int32_t>(index));
		}
	}

	DominationMode::DominationMode() : m_random{ std::random_device{}() } {}
	DominationMode::DominationMode(std::uint32_t seed) : m_random{ seed } {}

	std::int32_t DominationMode::pointsLimit(const MatchContext& match)
	{
		return (std::max)(1, match.settings().values[pointsSetting]);
	}

	double DominationMode::speed(std::int32_t playersInside)
	{
		return playersInside <= 0 ? 0.0 : playersInside == 1 ? 1.0 : playersInside == 2 ? 1.5 : 2.0;
	}

	bool DominationMode::supportsMap(std::uint32_t mapId)
	{
		return CH::findDominationMap(mapId) != nullptr;
	}

	void DominationMode::onMatchStart(MatchContext& match)
	{
		assignBalancedTeams(match, m_random);
		m_map = CH::findDominationMap(match.mapId());
		if (m_map == nullptr)
		{
			// the room's map has no zones (the DLL lists only Domination maps; the room checks the map too): every player
			// loads a Domination map
			const auto& maps = CH::dominationMaps;
			m_map = &maps[std::uniform_int_distribution<std::size_t>{ 0, maps.size() - 1 }(m_random)];
			match.changeMap(m_map->map);
		}
		m_teamSpawns.reset(m_map->map);
		m_points = {};
	}

	void DominationMode::onRoundStart(MatchContext& match)
	{
		reviveAll(match);
		m_zones = {};
		m_points = {};
		m_running = true;
		m_lastZones.clear();
		match.setZoneScore(0, 0, pointsLimit(match));
		placeZones(match, true);
		sendZones(match);
		m_teamSpawns.update(match, { 0, spawnBaseOf(redTeam), spawnBaseOf(blueTeam) });
	}

	void DominationMode::placeZones(MatchContext& match, bool active)
	{
		for (std::size_t i = 0; i < zoneCount; ++i)
		{
			const auto& spot = m_map->zones[i];
			match.placeSquareZone(i, active, spot.x, spot.y, zoneHalfSize, spot.z - zoneBelow, spot.z + zoneAbove, respawnKeepOut);
		}
	}

	// The 0x37 payload; sent when it differs from the last one (the progress moves every second of a capture).
	void DominationMode::sendZones(MatchContext& match)
	{
		CH::DominationZonesPayload payload{};
		payload.count = m_running ? static_cast<std::int32_t>(zoneCount) : 0;
		payload.halfSize = zoneHalfSize;
		for (std::size_t i = 0; i < zoneCount && m_map != nullptr; ++i)
		{
			const Zone& zone = m_zones[i];
			auto& out = payload.zones[i];
			out.x = m_map->zones[i].x;
			out.y = m_map->zones[i].y;
			out.z = m_map->zones[i].z;
			out.owner = static_cast<std::int8_t>(zone.owner);
			out.capturer = static_cast<std::int8_t>(zone.workTeam);
			out.contested = zone.contested ? 1 : 0;
			out.permille = static_cast<std::int16_t>(std::clamp(zone.work / captureSeconds, 0.0, 1.0) * 1000.0);
			out.permillePerSecond = static_cast<std::int16_t>(zone.speed / captureSeconds * 1000.0);
		}
		std::vector<std::uint8_t> bytes(sizeof(payload));
		std::memcpy(bytes.data(), &payload, sizeof(payload));
		if (bytes == m_lastZones)
		{
			return;
		}
		m_lastZones = std::move(bytes);
		match.showDominationZones(payload);
	}

	// A team respawns at its base; the two teams swap bases (never share one) when a team holds the other's home zone
	// (A red's, B blue's) and has not lost its own to it: e.g. blue holds A while B is neutral or red's -> red respawns
	// at blue's base, blue at red's (next to A).
	std::int32_t DominationMode::spawnBaseOf(std::int32_t team) const
	{
		const std::int32_t ownerOfA = m_zones[0].owner;
		const std::int32_t ownerOfB = m_zones[1].owner;
		const bool swapped = (ownerOfA == blueTeam && ownerOfB != blueTeam) || (ownerOfB == redTeam && ownerOfA != redTeam);
		return swapped ? otherTeam(team) : team;
	}

	void DominationMode::addPoints(MatchContext& match, std::int32_t team, std::int32_t points)
	{
		auto& teamPoints = m_points[team == redTeam ? 0 : 1];
		teamPoints = (std::min)(teamPoints + points, pointsLimit(match));
	}

	void DominationMode::capture(MatchContext& match, std::size_t index, std::int32_t team, std::span<PartyPlayer* const> capturers)
	{
		Zone& zone = m_zones[index];
		zone.owner = team;
		zone.workTeam = 0;
		zone.work = 0.0;
		zone.attackAnnounced = false;
		for (PartyPlayer* player : capturers)
		{
			player->points += match.rules().lastStandingPoints;
			match.updatePoints(*player);
		}
		match.announceToTeam(zoneAnnouncement(CH::Announcement::DominationCapturedA, index), team);
		match.announceToTeam(zoneAnnouncement(CH::Announcement::DominationEnemyCapturedA, index), otherTeam(team));
	}

	void DominationMode::onTick(MatchContext& match)
	{
		if (!m_running || m_map == nullptr)
		{
			return;
		}
		for (std::size_t i = 0; i < zoneCount; ++i)
		{
			Zone& zone = m_zones[i];
			// the living red / blue players CastServer sees in the zone
			const auto inside = match.playersInZone(i);
			std::array<std::vector<PartyPlayer*>, 3> byTeam;
			for (PartyPlayer& player : match.players())
			{
				if (player.alive && (player.fallbackTeam == redTeam || player.fallbackTeam == blueTeam) &&
					std::ranges::find(inside, player.playerId) != inside.end())
				{
					byTeam[static_cast<std::size_t>(player.fallbackTeam)].push_back(&player);
				}
			}
			const auto count = [&](std::int32_t team) { return static_cast<std::int32_t>(byTeam[static_cast<std::size_t>(team)].size()); };
			zone.contested = count(redTeam) > 0 && count(blueTeam) > 0;
			zone.speed = 0.0;
			const std::int32_t alone = zone.contested ? 0 : count(redTeam) > 0 ? redTeam : count(blueTeam) > 0 ? blueTeam : 0;
			if (zone.contested)
			{
				continue; // paused: the capture work stays
			}
			if (alone == 0 || alone == zone.owner)
			{
				zone.workTeam = 0; // nobody capturing (empty, or only its owners): the progress is lost
				zone.work = 0.0;
				zone.attackAnnounced = false;
				continue;
			}
			if (zone.workTeam != alone)
			{
				zone.workTeam = alone;
				zone.work = 0.0;
				zone.attackAnnounced = false;
			}
			if (zone.owner != 0 && !zone.attackAnnounced)
			{
				match.announceToTeam(zoneAnnouncement(CH::Announcement::DominationUnderAttackA, i), zone.owner);
				zone.attackAnnounced = true;
			}
			zone.speed = speed(count(alone));
			zone.work += zone.speed;
			if (zone.work >= captureSeconds)
			{
				capture(match, i, alone, byTeam[static_cast<std::size_t>(alone)]);
			}
		}
		const auto before = m_points;
		for (const Zone& zone : m_zones)
		{
			if (zone.owner == redTeam || zone.owner == blueTeam)
			{
				addPoints(match, zone.owner, pointsPerZone);
			}
		}
		if (m_points != before)
		{
			match.setZoneScore(m_points[0], m_points[1], pointsLimit(match));
		}
		sendZones(match);
		m_teamSpawns.update(match, { 0, spawnBaseOf(redTeam), spawnBaseOf(blueTeam) });
	}

	void DominationMode::onDeath(MatchContext& match, PartyPlayer& victim, PartyPlayer* killer)
	{
		victim.alive = false; // out of every zone until it respawns
		if (killer != nullptr && killer != &victim && killer->fallbackTeam != victim.fallbackTeam)
		{
			scoreKill(match, *killer);
		}
	}

	void DominationMode::onRespawn(MatchContext&, PartyPlayer& player)
	{
		player.alive = true;
	}

	bool DominationMode::isRoundOver(MatchContext& match)
	{
		return m_points[0] >= pointsLimit(match) || m_points[1] >= pointsLimit(match);
	}

	std::int32_t DominationMode::winner() const
	{
		if (m_points[0] == m_points[1])
		{
			return 0; // draw
		}
		return m_points[0] > m_points[1] ? redTeam : blueTeam;
	}

	void DominationMode::onRoundEnd(MatchContext& match, RoundEndReason)
	{
		m_running = false;
		if (m_map != nullptr)
		{
			placeZones(match, false);
		}
		sendZones(match); // count 0: the DLL hides the zones
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
			match.announce(CH::teamWon(team));
		}
		else
		{
			match.announce(CH::Announcement::TeamClashTimeUp); // "ROUND TIME UP!": a draw
		}
	}

	void DominationMode::onMatchEnd(MatchContext& match)
	{
		if (const std::int32_t team = winner(); team != 0)
		{
			match.announce(CH::teamWon(team));
		}
	}

	std::optional<std::pair<CH::Announcement, std::string>> DominationMode::roundStartAnnouncement(const MatchContext&) const
	{
		return std::pair{ CH::Announcement::DominationStart, std::string{} };
	}

	std::int32_t DominationMode::teamForLateJoiner(MatchContext& match)
	{
		return smallerOfTwoTeams(match.players()); // its spawns follow at the next tick (TeamBaseSpawns)
	}
}
