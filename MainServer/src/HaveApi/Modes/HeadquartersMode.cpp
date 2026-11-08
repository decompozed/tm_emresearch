/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#include "HaveApi/Modes/HeadquartersMode.h"
#include <algorithm>
#include <cmath>

namespace Main::HaveApi
{
	namespace
	{
		namespace CH = Common::HaveApi;

		constexpr std::size_t captureTimeSetting = 0;
		constexpr std::size_t lifetimeSetting = 1;
		constexpr std::size_t pointsSetting = 2;

		constexpr std::int32_t zoneFree = 0;
		constexpr std::int32_t zoneContested = 3;

		[[nodiscard]] std::int32_t otherTeam(std::int32_t team)
		{
			return team == HeadquartersMode::redTeam ? HeadquartersMode::blueTeam : HeadquartersMode::redTeam;
		}
	}

	HeadquartersMode::HeadquartersMode() : m_random{ std::random_device{}() } {}
	HeadquartersMode::HeadquartersMode(std::uint32_t seed) : m_random{ seed } {}

	double HeadquartersMode::captureSeconds(const MatchContext& match)
	{
		return static_cast<double>((std::max)(1, match.settings().values[captureTimeSetting]));
	}

	std::chrono::seconds HeadquartersMode::lifetime(const MatchContext& match)
	{
		return std::chrono::seconds{ (std::max)(10, match.settings().values[lifetimeSetting]) };
	}

	std::int32_t HeadquartersMode::pointsLimit(const MatchContext& match)
	{
		return (std::max)(1, match.settings().values[pointsSetting]);
	}

	double HeadquartersMode::speed(std::int32_t playersInside)
	{
		return playersInside <= 0 ? 0.0 : playersInside == 1 ? 1.0 : playersInside == 2 ? 1.5 : 2.0;
	}

	void HeadquartersMode::onMatchStart(MatchContext& match)
	{
		assignBalancedTeams(match, m_random);
		const auto spots = CH::findHardpointZones(match.mapId());
		m_spots.assign(spots.begin(), spots.end());
		if (m_spots.empty())
		{
			m_spots.push_back({ 0.0f, 0.0f, 0.0f }); // unknown map: the map centre
		}
		m_points = {};
		m_teamSpawns.reset(match.mapId());
	}

	void HeadquartersMode::onRoundStart(MatchContext& match)
	{
		reviveAll(match);
		m_spot = spotFarthestFromBases(m_spots, match.mapId(),
			std::uniform_int_distribution<std::size_t>{ 0, m_spots.size() - 1 }(m_random));
		match.setZoneScore(m_points[0], m_points[1], pointsLimit(match));
		bringOnline(match, false); // the round start announcement says it
		updateSpawns(match);
	}

	// A neutral HQ at m_spot.
	void HeadquartersMode::bringOnline(MatchContext& match, bool announceIt)
	{
		m_phase = Phase::Neutral;
		m_phaseSince = match.roundElapsed();
		m_owner = 0;
		m_workTeam = 0;
		m_work = 0.0;
		const auto& spot = m_spots[m_spot];
		match.placeZone(true, spot.x, spot.y, zoneRadius, respawnKeepOut);
		m_zoneState = -1;
		sendZone(match, zoneFree);
		sendProgress(match, CH::HqProgressKind::Hidden, 0, 0.0, 0.0, 0);
		if (announceIt)
		{
			match.announce(CH::Announcement::HqOnline);
		}
	}

	void HeadquartersMode::sendZone(MatchContext& match, std::int32_t state)
	{
		if (state == m_zoneState)
		{
			return;
		}
		m_zoneState = state;
		const auto& spot = m_spots[m_spot];
		match.showZone(true, spot.x, spot.y, state);
	}

	void HeadquartersMode::sendProgress(MatchContext& match, CH::HqProgressKind kind, std::int32_t team, double progress,
		double ratePerSecond, std::int32_t secondsLeft)
	{
		const double total = captureSeconds(match);
		match.showHqProgress({ static_cast<std::int32_t>(kind), team,
			static_cast<std::int32_t>(std::clamp(progress / total, 0.0, 1.0) * 1000.0),
			static_cast<std::int32_t>(ratePerSecond / total * 1000.0), secondsLeft });
	}

	void HeadquartersMode::addPoints(MatchContext& match, std::int32_t team, std::int32_t points)
	{
		auto& teamPoints = m_points[team == redTeam ? 0 : 1];
		teamPoints = (std::min)(teamPoints + points, pointsLimit(match));
		match.setZoneScore(m_points[0], m_points[1], pointsLimit(match));
	}

	void HeadquartersMode::capture(MatchContext& match, std::int32_t team, std::span<PartyPlayer* const> capturers)
	{
		m_phase = Phase::Held;
		m_phaseSince = match.roundElapsed();
		m_owner = team;
		m_workTeam = 0;
		m_work = 0.0;
		addPoints(match, team, captureBonus);
		for (PartyPlayer* player : capturers)
		{
			player->points += match.rules().lastStandingPoints;
			match.updatePoints(*player);
		}
		sendZone(match, team);
		match.setRespawnBlockedTeam(team);
		match.announceToTeam(CH::Announcement::HqCaptured, team);
		match.announceToTeam(CH::Announcement::HqEnemyCaptured, otherTeam(team));
	}

	// Destroyed or expired: the owners' dead come back, the HQ goes offline until the next one.
	void HeadquartersMode::endHq(MatchContext& match, CH::Announcement event)
	{
		m_phase = Phase::Waiting;
		m_phaseSince = match.roundElapsed();
		m_owner = 0;
		m_workTeam = 0;
		m_work = 0.0;
		match.setRespawnBlockedTeam(0);
		match.showZone(false, 0.0f, 0.0f, zoneFree);
		m_zoneState = -1;
		match.placeZone(false, 0.0f, 0.0f, 0.0f, 0.0f);
		match.announce(event);
		sendProgress(match, CH::HqProgressKind::NextHq, 0, 0.0, 0.0, static_cast<std::int32_t>(nextHqDelay.count()));
	}

	void HeadquartersMode::onTick(MatchContext& match)
	{
		tickZone(match);
		updateSpawns(match);
	}

	// Each player its team's base spawns: at the round start, then for late joiners / team changes.
	void HeadquartersMode::updateSpawns(MatchContext& match)
	{
		m_teamSpawns.update(match);
	}

	void HeadquartersMode::tickZone(MatchContext& match)
	{
		const auto now = match.roundElapsed();
		if (m_phase == Phase::Waiting)
		{
			const auto waited = now - m_phaseSince;
			if (waited < nextHqDelay)
			{
				sendProgress(match, CH::HqProgressKind::NextHq, 0, 0.0, 0.0, static_cast<std::int32_t>((nextHqDelay - waited).count()));
				return;
			}
			m_spot = spotAwayFromSpawns(m_spots, match.spawnPoints(), respawnKeepOut, m_spot, m_random);
			bringOnline(match, true);
			return;
		}
		if (m_phase == Phase::Held && now - m_phaseSince >= lifetime(match))
		{
			endHq(match, CH::Announcement::HqExpired);
			return;
		}

		// the living players CastServer sees in the HQ
		const auto inside = match.playersInZone();
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
		const bool contested = count(redTeam) > 0 && count(blueTeam) > 0;

		if (m_phase == Phase::Neutral)
		{
			const std::int32_t capturer = contested ? 0 : count(redTeam) > 0 ? redTeam : count(blueTeam) > 0 ? blueTeam : 0;
			if (contested && m_workTeam != 0)
			{
				// paused
			}
			else if (capturer == 0)
			{
				m_workTeam = 0; // the capturers left: the progress is lost
				m_work = 0.0;
			}
			else
			{
				if (capturer != m_workTeam)
				{
					m_workTeam = capturer;
					m_work = 0.0;
				}
				m_work += speed(count(capturer));
				if (m_work >= captureSeconds(match))
				{
					capture(match, capturer, byTeam[static_cast<std::size_t>(capturer)]);
					sendProgress(match, CH::HqProgressKind::Held, capturer, 0.0, 0.0, static_cast<std::int32_t>(lifetime(match).count()));
					return;
				}
			}
			if (contested && m_zoneState != zoneContested)
			{
				match.announce(CH::Announcement::HqContested);
			}
			sendZone(match, contested ? zoneContested : zoneFree);
			if (m_workTeam != 0)
			{
				sendProgress(match, CH::HqProgressKind::Capturing, m_workTeam, m_work, contested ? 0.0 : speed(count(m_workTeam)), 0);
			}
			else
			{
				sendProgress(match, CH::HqProgressKind::Hidden, 0, 0.0, 0.0, 0);
			}
			return;
		}

		// Held
		addPoints(match, m_owner, pointsPerSecond);
		const std::int32_t attackers = otherTeam(m_owner);
		const bool destroying = count(attackers) > 0 && count(m_owner) == 0;
		if (destroying)
		{
			m_workTeam = attackers;
			m_work += speed(count(attackers));
			if (m_work >= captureSeconds(match))
			{
				addPoints(match, attackers, captureBonus);
				for (PartyPlayer* player : byTeam[static_cast<std::size_t>(attackers)])
				{
					player->points += match.rules().lastStandingPoints;
					match.updatePoints(*player);
				}
				endHq(match, CH::Announcement::HqDestroyed);
				return;
			}
		}
		else if (!contested)
		{
			m_workTeam = 0; // the attackers left (or were killed): the progress is lost
			m_work = 0.0;
		}
		if (contested && m_zoneState != zoneContested)
		{
			match.announce(CH::Announcement::HqContested);
		}
		sendZone(match, contested ? zoneContested : m_owner);
		const auto left = static_cast<std::int32_t>((lifetime(match) - (now - m_phaseSince)).count());
		if (m_workTeam != 0)
		{
			sendProgress(match, CH::HqProgressKind::Destroying, m_workTeam, m_work, destroying ? speed(count(attackers)) : 0.0, left);
		}
		else
		{
			sendProgress(match, CH::HqProgressKind::Held, m_owner, 0.0, 0.0, left);
		}
	}

	void HeadquartersMode::onDeath(MatchContext& match, PartyPlayer& victim, PartyPlayer* killer)
	{
		victim.alive = false; // out of the HQ until it respawns; an owner stays dead while its team holds the HQ
		if (killer != nullptr && killer != &victim && killer->fallbackTeam != victim.fallbackTeam)
		{
			scoreKill(match, *killer);
		}
	}

	void HeadquartersMode::onRespawn(MatchContext&, PartyPlayer& player)
	{
		player.alive = true;
	}

	// A player joining the team that holds the HQ does not appear (the owners cannot respawn): it waits dead like them,
	// its death camera seen by nobody else, and comes back with them when the HQ is destroyed or expires
	// (PartyMatch::setRespawnBlockedTeam respawns the team's dead).
	bool HeadquartersMode::keepsLateJoinerDead(const PartyPlayer& player) const
	{
		return m_phase == Phase::Held && player.fallbackTeam == m_owner;
	}

	bool HeadquartersMode::isRoundOver(MatchContext& match)
	{
		return m_points[0] >= pointsLimit(match) || m_points[1] >= pointsLimit(match);
	}

	std::int32_t HeadquartersMode::winner() const
	{
		if (m_points[0] == m_points[1])
		{
			return 0;
		}
		return m_points[0] > m_points[1] ? redTeam : blueTeam;
	}

	void HeadquartersMode::onRoundEnd(MatchContext& match, RoundEndReason)
	{
		match.setRespawnBlockedTeam(0);
		match.showZone(false, 0.0f, 0.0f, zoneFree);
		match.placeZone(false, 0.0f, 0.0f, 0.0f, 0.0f);
		sendProgress(match, CH::HqProgressKind::Hidden, 0, 0.0, 0.0, 0);
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
	}

	void HeadquartersMode::onMatchEnd(MatchContext& match)
	{
		if (const std::int32_t team = winner(); team != 0)
		{
			match.announce(CH::teamWon(team));
		}
	}

	std::optional<std::pair<CH::Announcement, std::string>> HeadquartersMode::roundStartAnnouncement(const MatchContext&) const
	{
		return std::pair{ CH::Announcement::HqOnline, std::string{} };
	}

	std::int32_t HeadquartersMode::teamForLateJoiner(MatchContext& match)
	{
		return smallerOfTwoTeams(match.players());
	}
}
