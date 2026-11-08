/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#include "HaveApi/Modes/BattleRoyaleMode.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <tuple>
#include <utility>
#include <vector>
#include "HaveApi/BattleMessages.h"
#include "HaveApi/PartyMaps.h"

namespace Main::HaveApi
{
	namespace CH = Common::HaveApi;

	namespace
	{
		constexpr std::string_view alarmSound = "sv_combat_alarm_bad.wav";
		constexpr std::array<BR::Arena, 4> settingArenas = { BR::Arena::Castle, BR::Arena::Paper, BR::Arena::Pvc, BR::Arena::Studio };

		[[nodiscard]] double seconds(const MatchContext& match)
		{
			return static_cast<double>(match.roundElapsedMs().count()) / 1000.0;
		}

		[[nodiscard]] bool listed(const std::vector<std::int32_t>& ids, std::int32_t id)
		{
			return std::ranges::find(ids, id) != ids.end();
		}

		[[nodiscard]] bool pairOf(std::span<const StormPlan::Closing> closings, BR::Arena a, BR::Arena b)
		{
			const auto has = [&](BR::Arena arena) { return std::ranges::find(closings, arena, &StormPlan::Closing::arena) != closings.end(); };
			return closings.size() == 2 && has(a) && has(b);
		}

		[[nodiscard]] std::pair<CH::Announcement, std::string> closingWarning(std::span<const StormPlan::Closing> closings)
		{
			for (const auto& [a, b, event] : { std::tuple{ BR::Arena::Paper, BR::Arena::Pvc, CH::Announcement::BrPaperPvcCloseSoon },
					 std::tuple{ BR::Arena::Castle, BR::Arena::Studio, CH::Announcement::BrCastleStudioCloseSoon } })
			{
				if (pairOf(closings, a, b))
				{
					return { event, std::string{ StormPlan::arenaLabel(a) } + " + " + StormPlan::arenaLabel(b) + " CLOSE IN 30 SECONDS" };
				}
			}
			const BR::Arena arena = closings.front().arena;
			return { static_cast<CH::Announcement>(static_cast<std::int32_t>(CH::Announcement::BrCastleClosesSoon) + static_cast<std::int32_t>(arena)),
				std::string{ StormPlan::arenaLabel(arena) } + " CLOSES IN 30 SECONDS" };
		}
	}

	CH::BattleSpawnsPayload BattleRoyaleMode::spreadOverArenas(std::span<const PartyPlayer> players, std::mt19937& random)
	{
		CH::BattleSpawnsPayload areas{};
		for (std::size_t arena = 0; arena < BR::arenaCount; ++arena)
		{
			const BR::ArenaRegion& region = BR::arenas[arena];
			for (std::size_t box = region.firstBox; box < static_cast<std::size_t>(region.firstBox + region.boxCount); ++box)
			{
				if (static_cast<std::size_t>(areas.boxCount) < areas.boxes.size())
				{
					const BR::Box& b = BR::boxes[box];
					areas.boxes[static_cast<std::size_t>(areas.boxCount++)] = { b.x1, b.x2, b.y1, b.y2, static_cast<std::int32_t>(arena) };
				}
			}
		}
		std::vector<std::int32_t> ids;
		for (const PartyPlayer& player : players)
		{
			ids.push_back(player.playerId);
		}
		std::ranges::shuffle(ids, random);
		for (std::size_t i = 0; i < ids.size() && static_cast<std::size_t>(areas.playerCount) < areas.players.size(); ++i)
		{
			areas.players[static_cast<std::size_t>(areas.playerCount++)] = { ids[i], static_cast<std::int32_t>(spawnOrder[i % spawnOrder.size()]) };
		}
		return areas;
	}

	void BattleRoyaleMode::onMatchStart(MatchContext& match)
	{
		std::int32_t team = 1;
		for (PartyPlayer& player : match.players())
		{
			player.team = team;
			player.fallbackTeam = team;
			player.placement = 0;
			++team;
		}
		match.showSpawnAreas(spreadOverArenas(match.players(), m_spawnRandom));
		if (match.mapId() != CH::battleRoyaleMap)
		{
			match.changeMap(CH::battleRoyaleMap);
		}
	}

	void BattleRoyaleMode::onRoundStart(MatchContext& match)
	{
		reviveAll(match);
		m_drop = Drop::Loading;
		m_placed = false;
		m_announced = false;
		arm(match, false);
		startStorm(match);
	}

	void BattleRoyaleMode::startStorm(MatchContext& match)
	{
		const std::int32_t setting = match.settings().values[finalArenaSetting];
		const BR::Arena arena = setting >= 2 && setting <= 5 ? settingArenas[static_cast<std::size_t>(setting - 2)]
			: StormPlan::weightedArena(std::uniform_real_distribution<double>{ 0.0, 1.0 }(m_random));
		const BR::ArenaRegion& region = BR::arenas[static_cast<std::size_t>(arena)];
		const std::size_t point = std::uniform_int_distribution<std::size_t>{ 0, static_cast<std::size_t>((std::max)(region.finalPointCount, std::uint8_t{ 1 }) - 1) }(m_random);
		const std::uint32_t length = match.roundSeconds() != 0 ? match.roundSeconds() : static_cast<std::uint32_t>(StormPlan::baseSeconds);
		m_storm = std::make_unique<StormPlan>(arena, region.finalPoints[point], static_cast<double>(length));
		m_sentSegment = std::numeric_limits<std::size_t>::max();
		m_announcedPhase = 0;
		m_warnedClosings = 0;
		m_inStormAnnounced.clear();
		m_radarPulsed = false;
		m_radarOn = false;
		m_outsideSince.clear();
		m_stormDamage.clear();
		m_stormKills.clear();
		updateStorm(match);
	}

	void BattleRoyaleMode::stopStorm(MatchContext& match)
	{
		if (!m_storm)
		{
			return;
		}
		m_storm.reset();
		match.clearStorm();
		if (m_radarOn)
		{
			m_radarOn = false;
			match.setRadar(false);
		}
	}

	bool BattleRoyaleMode::gradual(const MatchContext& match) const
	{
		return match.settings().values[stormDamageSetting] == static_cast<std::int32_t>(StormDamage::Gradual);
	}

	std::int32_t BattleRoyaleMode::stormDamageOf(std::int32_t playerId) const
	{
		const auto found = m_stormDamage.find(playerId);
		return found != m_stormDamage.end() ? found->second : 0;
	}

	void BattleRoyaleMode::updateStorm(MatchContext& match)
	{
		if (!m_storm)
		{
			return;
		}
		const double t = seconds(match);
		if (const std::size_t segment = m_storm->segmentIndex(t); segment != m_sentSegment)
		{
			m_sentSegment = segment;
			match.setStorm(m_storm->zoneAt(t, match.maxHealth()));
		}
		warnClosings(match, t);
		const StormPlan::Phase& phase = m_storm->phaseAt(t);
		const auto index = static_cast<std::size_t>(&phase - m_storm->phases().data());
		if (index != m_announcedPhase)
		{
			m_announcedPhase = index;
			announcePhase(match, phase);
		}
		if (!m_radarPulsed && t >= m_storm->exitsClosing())
		{
			m_radarPulsed = true;
			m_radarOn = true;
			match.setRadar(true);
		}
		else if (m_radarOn && t >= m_storm->exitsClosing() + radarPulseSeconds)
		{
			m_radarOn = false;
			match.setRadar(false);
		}
		hurt(match, phase, t);
	}

	void BattleRoyaleMode::warnClosings(MatchContext& match, double t)
	{
		const std::span<const StormPlan::Closing> closings = m_storm->closings();
		while (m_warnedClosings < closings.size() && t >= closings[m_warnedClosings].start - closingWarningSeconds)
		{
			const double start = closings[m_warnedClosings].start;
			std::size_t count = 1;
			while (m_warnedClosings + count < closings.size() && std::abs(closings[m_warnedClosings + count].start - start) < 1e-6)
			{
				++count;
			}
			const auto group = closings.subspan(m_warnedClosings, count);
			m_warnedClosings += count;
			if (t < start)
			{
				const auto [event, text] = closingWarning(group);
				match.announce(event, text);
			}
		}
	}

	void BattleRoyaleMode::announcePhase(MatchContext& match, const StormPlan::Phase& phase)
	{
		if (phase.closing)
		{
			match.announce(CH::Announcement::BrZoneClosing, phase.label);
			return;
		}
		for (const PartyPlayer& player : match.players())
		{
			if (player.loaded)
			{
				match.statusText(player, phase.label);
				match.playSound(player, alarmSound);
			}
		}
	}

	void BattleRoyaleMode::hurt(MatchContext& match, const StormPlan::Phase& phase, double t)
	{
		if (phase.damagePermille == 0)
		{
			m_outsideSince.clear();
			return;
		}
		const std::vector<std::int32_t> outside = match.playersOutsideStorm();
		const std::int32_t health = match.maxHealth();
		const std::int32_t damage = (std::max)(1, phase.damagePermille * health / 1000);
		const bool byDamage = gradual(match);
		std::vector<CH::StormHit> hits;
		for (const PartyPlayer& player : match.players())
		{
			if (!player.alive || !listed(outside, player.playerId))
			{
				m_outsideSince.erase(player.playerId);
				continue;
			}
			if (m_stormKills.contains(player.playerId))
			{
				continue;
			}
			const auto [entry, entered] = m_outsideSince.try_emplace(player.playerId, t);
			if (const auto last = m_inStormAnnounced.find(player.playerId);
				entered && (last == m_inStormAnnounced.end() || t - last->second >= inStormRepeatSeconds))
			{
				m_inStormAnnounced[player.playerId] = t;
				match.announceToPlayer(CH::Announcement::BrInStorm, player, "YOU ARE IN THE STORM!");
			}
			if (byDamage)
			{
				std::int32_t& total = m_stormDamage[player.playerId];
				total += damage;
				if (hits.size() < CH::StormDamagePayload{}.hits.size())
				{
					hits.push_back({ player.playerId, damage });
				}
				match.playSound(player, alarmSound);
				if (total > static_cast<std::int32_t>(fallbackDamageFactor * health))
				{
					stormKill(match, player);
				}
				continue;
			}
			const double since = entry->second;
			const auto left = static_cast<std::int32_t>(phase.deathSeconds - std::floor(t - since + 1e-6));
			if (left <= 0)
			{
				stormKill(match, player);
				continue;
			}
			match.statusText(player, "LEAVE THE STORM! " + std::to_string(left));
			match.playSound(player, left >= 2 && left <= 5 ? "sv_countdown_" + std::to_string(left) + ".wav" : std::string{ alarmSound });
		}
		if (!hits.empty())
		{
			match.sendStormDamage(hits);
		}
	}

	void BattleRoyaleMode::stormKill(MatchContext& match, const PartyPlayer& player)
	{
		if (m_stormKills.insert(player.playerId).second)
		{
			match.killPlayer(player);
		}
	}

	void BattleRoyaleMode::onStormLethal(MatchContext& match, PartyPlayer& victim)
	{
		if (!m_storm || !victim.alive || !gradual(match) || !match.wasOutsideStorm(victim.playerId, lethalWindow))
		{
			return;
		}
		stormKill(match, victim);
	}

	void BattleRoyaleMode::arm(MatchContext& match, bool armed)
	{
		CH::Modifiers modifiers = CH::neutralModifiers();
		modifiers.head.canDamageOthers = armed ? 1 : 0;
		for (const PartyPlayer& player : match.players())
		{
			match.setModifiers(player, modifiers);
		}
	}

	void BattleRoyaleMode::onTick(MatchContext& match)
	{
		const auto now = match.roundElapsed();
		if (m_drop == Drop::Loading &&
			(std::ranges::all_of(match.players(), &PartyPlayer::loaded) || now >= dropTimeout))
		{
			m_drop = Drop::Countdown;
			m_countdownFrom = now;
			match.showPreparePanel(dropCountdownSeconds);
		}
		else if (m_drop == Drop::Countdown && now - m_countdownFrom >= std::chrono::seconds{ dropCountdownSeconds })
		{
			m_drop = Drop::Over;
			arm(match, true);
		}
		updateStorm(match);
	}

	void BattleRoyaleMode::onDeath(MatchContext& match, PartyPlayer& victim, PartyPlayer* killer)
	{
		eliminate(match, victim);
		m_outsideSince.erase(victim.playerId);
		victim.placement = static_cast<std::int32_t>(aliveCount(match.players())) + 1;
		const bool killedByOther = killer != nullptr && killer != &victim;
		if (killedByOther)
		{
			scoreKill(match, *killer);
		}
		match.spectate(victim, killedByOther ? killer->playerId : 0);
	}

	void BattleRoyaleMode::onPlayerLeft(MatchContext& match, PartyPlayer& player)
	{
		if (player.alive && player.placement == 0)
		{
			player.placement = static_cast<std::int32_t>(aliveCount(match.players()));
		}
		player.alive = false;
	}

	bool BattleRoyaleMode::isRoundOver(MatchContext& match)
	{
		return match.players().size() >= 2 && aliveCount(match.players()) <= 1;
	}

	void BattleRoyaleMode::placeSurvivors(MatchContext& match)
	{
		if (std::exchange(m_placed, true))
		{
			return;
		}
		std::vector<PartyPlayer*> survivors;
		for (PartyPlayer& player : match.players())
		{
			if (player.alive && player.placement == 0)
			{
				survivors.push_back(&player);
			}
		}
		std::ranges::stable_sort(survivors, [](const PartyPlayer* a, const PartyPlayer* b) {
			return a->kills != b->kills ? a->kills > b->kills : a->points > b->points;
		});
		std::int32_t place = 1;
		for (PartyPlayer* player : survivors)
		{
			player->placement = place++;
		}
		for (PartyPlayer& player : match.players())
		{
			if (player.placement >= 1 && player.placement <= static_cast<std::int32_t>(placementPoints.size()))
			{
				player.points += placementPoints[static_cast<std::size_t>(player.placement - 1)];
				match.updatePoints(player);
			}
		}
	}

	void BattleRoyaleMode::announceWinner(MatchContext& match)
	{
		if (m_announced)
		{
			return;
		}
		const auto winner = std::ranges::find(match.players(), 1, &PartyPlayer::placement);
		if (winner != match.players().end())
		{
			m_announced = true;
			match.announce(CH::Announcement::BrLastToyStanding, winner->name);
		}
	}

	void BattleRoyaleMode::onRoundEnd(MatchContext& match, RoundEndReason reason)
	{
		stopStorm(match);
		placeSurvivors(match);
		if (reason == RoundEndReason::TimeUp)
		{
			match.announce(CH::Announcement::ArenaTimeUp);
			return;
		}
		announceWinner(match);
	}

	void BattleRoyaleMode::onMatchEnd(MatchContext& match)
	{
		stopStorm(match);
		placeSurvivors(match);
		announceWinner(match);
	}
}
