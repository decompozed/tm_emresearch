/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#include "HaveApi/Modes/PropHuntMode.h"
#include <algorithm>
#include <cmath>
#include "HaveApi/PropModels.h"

namespace Main::HaveApi
{
	namespace
	{
		namespace CH = Common::HaveApi;

		constexpr std::size_t hideTimeSetting = 0;
		constexpr std::size_t roundsSetting = 2;
		constexpr std::size_t changesSetting = 3;
		constexpr std::size_t decoysSetting = 4;

		[[nodiscard]] CH::Modifiers harmless()
		{
			CH::Modifiers modifiers = CH::neutralModifiers();
			modifiers.head.canDamageOthers = 0;
			return modifiers;
		}

		[[nodiscard]] CH::Modifiers propModifiers()
		{
			CH::Modifiers modifiers = harmless();
			modifiers.tail.maxHealthOverride = PropHuntMode::propHealth;
			return modifiers;
		}

		[[nodiscard]] CH::Modifiers frozen()
		{
			CH::Modifiers modifiers = harmless();
			modifiers.tail.movementSpeedScale = 0.0f;
			return modifiers;
		}
	}

	PropHuntMode::PropHuntMode() : m_random{ std::random_device{}() } {}
	PropHuntMode::PropHuntMode(std::uint32_t seed) : m_random{ seed } {}

	std::int32_t PropHuntMode::hunterCount(std::size_t players)
	{
		if (players < 2)
		{
			return 1;
		}
		const auto third = static_cast<std::int32_t>(std::lround(static_cast<double>(players) / 3.0));
		return std::clamp(third, 1, static_cast<std::int32_t>(players) - 1);
	}

	std::chrono::seconds PropHuntMode::hideTime(const MatchContext& match)
	{
		return std::chrono::seconds{ (std::max)(5, match.settings().values[hideTimeSetting]) };
	}

	std::int32_t PropHuntMode::modelCount(const MatchContext& match) const
	{
		const std::int32_t count = CH::propModelCount(match.mapId());
		return count > 0 ? count : fallbackModelCount;
	}

	// A random object of the map not used by this prop in the round; when all were used, any other than the current.
	std::int32_t PropHuntMode::nextModel(PropUses& uses, std::int32_t models)
	{
		std::vector<std::int32_t> fresh;
		for (std::int32_t m = 0; m < models; ++m)
		{
			if (std::ranges::find(uses.used, m) == uses.used.end())
			{
				fresh.push_back(m);
			}
		}
		if (fresh.empty())
		{
			uses.used = { uses.model };
			for (std::int32_t m = 0; m < models; ++m)
			{
				if (m != uses.model)
				{
					fresh.push_back(m);
				}
			}
		}
		const std::int32_t model = fresh[std::uniform_int_distribution<std::size_t>{ 0, fresh.size() - 1 }(m_random)];
		uses.used.push_back(model);
		return model;
	}

	std::int32_t PropHuntMode::modelOf(std::int32_t playerId) const
	{
		const auto found = m_props.find(playerId);
		return found != m_props.end() ? found->second.model : -1;
	}

	void PropHuntMode::onMatchStart(MatchContext& match)
	{
		m_rounds = static_cast<std::uint32_t>((std::max)(1, match.settings().values[roundsSetting]));
		m_timesHunted.clear();
	}

	// Roles of the round: the players who hunted least hunt (random among equals).
	void PropHuntMode::onRoundStart(MatchContext& match)
	{
		std::vector<PartyPlayer*> order;
		for (PartyPlayer& player : match.players())
		{
			order.push_back(&player);
		}
		std::ranges::shuffle(order, m_random);
		std::ranges::stable_sort(order, {}, [&](const PartyPlayer* p) { return m_timesHunted[p->playerId]; });
		const std::int32_t hunters = hunterCount(order.size());
		m_props.clear();
		const std::int32_t models = modelCount(match);
		for (std::size_t i = 0; i < order.size(); ++i)
		{
			PartyPlayer& player = *order[i];
			const bool hunter = static_cast<std::int32_t>(i) < hunters;
			player.fallbackTeam = hunter ? hunterTeam : propTeam;
			player.team = player.fallbackTeam;
			player.alive = true;
			match.updatePlayer(player);
			if (hunter)
			{
				++m_timesHunted[player.playerId];
				match.setPropState(player.playerId, -1, false);
				match.setModifiers(player, frozen());
			}
			else
			{
				PropUses uses;
				uses.model = std::uniform_int_distribution<std::int32_t>{ 0, models - 1 }(m_random);
				uses.used.push_back(uses.model);
				uses.changes = (std::max)(1, match.settings().values[changesSetting]);
				uses.decoys = (std::max)(1, match.settings().values[decoysSetting]);
				uses.flashes = flashesPerRound;
				m_props[player.playerId] = uses;
				match.setPropState(player.playerId, uses.model, false);
				match.setModifiers(player, propModifiers(), propHealth); // 5000 hit points once, on every client
			}
		}
		m_phase = Phase::Hiding;
		m_lastFound.clear();
		match.propEvent({ static_cast<std::int32_t>(CH::PropEventKind::ClearDecoys) });
		match.announceToTeam(CH::Announcement::PropHide, propTeam);
		match.announceToTeam(CH::Announcement::PropWait, hunterTeam);
		sendPhases(match);
	}

	void PropHuntMode::sendPhase(MatchContext& match, const PartyPlayer& player)
	{
		CH::PropPhasePayload phase{};
		phase.phase = static_cast<std::int32_t>(m_phase == Phase::Hiding ? CH::PropPhaseKind::Hiding :
			m_phase == Phase::Hunting ? CH::PropPhaseKind::Hunting : CH::PropPhaseKind::Off);
		const auto now = match.roundElapsed();
		if (m_phase == Phase::Hiding)
		{
			phase.secondsLeft = static_cast<std::int32_t>((std::max)(std::chrono::seconds{ 0 }, hideTime(match) - now).count());
		}
		else if (m_phase == Phase::Hunting && match.roundSeconds() != 0)
		{
			phase.secondsLeft = (std::max)(0, static_cast<std::int32_t>(match.roundSeconds()) - static_cast<std::int32_t>(now.count()));
		}
		phase.map = static_cast<std::int32_t>(match.mapId());
		if (const auto found = m_props.find(player.playerId); found != m_props.end())
		{
			phase.changesLeft = found->second.changes;
			phase.decoysLeft = found->second.decoys;
			phase.flashesLeft = found->second.flashes;
		}
		match.sendPropPhase(player, phase);
	}

	void PropHuntMode::sendPhases(MatchContext& match)
	{
		for (const PartyPlayer& player : match.players())
		{
			sendPhase(match, player);
		}
	}

	void PropHuntMode::onTick(MatchContext& match)
	{
		const auto now = match.roundElapsed();
		if (m_phase == Phase::Hiding && now >= hideTime(match))
		{
			m_phase = Phase::Hunting;
			m_huntSince = now;
			for (PartyPlayer& player : match.players())
			{
				if (player.fallbackTeam == hunterTeam)
				{
					match.setModifiers(player, CH::neutralModifiers());
				}
			}
			match.announce(CH::Announcement::PropHuntersReleased);
		}
		sendPhases(match);
	}

	void PropHuntMode::onHit(MatchContext& match, PartyPlayer& victim, PartyPlayer&, CH::DeathCause)
	{
		if (m_phase != Phase::Hunting || victim.fallbackTeam != propTeam || !victim.alive)
		{
			return;
		}
		const auto now = match.roundElapsed();
		const auto last = m_lastFound.find(victim.playerId);
		if (last == m_lastFound.end() || now - last->second >= std::chrono::seconds{ foundRepeatSeconds })
		{
			m_lastFound[victim.playerId] = now;
			match.announceToPlayer(CH::Announcement::PropFound, victim);
		}
	}

	void PropHuntMode::onPropAction(MatchContext& match, PartyPlayer& player, const CH::PropActionPayload& action)
	{
		if (static_cast<CH::PropActionKind>(action.action) == CH::PropActionKind::PenaltyDeath)
		{
			if (m_phase == Phase::Hunting && player.alive && player.fallbackTeam == hunterTeam)
			{
				match.killPlayer(player); // its DLL counted the decoy hits down to 0 hit points
			}
			return;
		}
		if (static_cast<CH::PropActionKind>(action.action) == CH::PropActionKind::DecoyHit)
		{
			if (m_phase == Phase::Hunting && player.alive && player.fallbackTeam == hunterTeam)
			{
				CH::PropEventPayload removed{};
				removed.kind = static_cast<std::int32_t>(CH::PropEventKind::RemoveDecoy);
				removed.playerId = player.playerId;
				removed.x = action.x;
				removed.y = action.y;
				removed.z = action.z;
				match.propEvent(removed); // THAT'S A DECOY! is shown by the hunter's DLL at the first hit
			}
			return;
		}
		const auto found = m_props.find(player.playerId);
		if (m_phase == Phase::Off || !player.alive || player.fallbackTeam != propTeam || found == m_props.end())
		{
			return;
		}
		PropUses& uses = found->second;
		switch (static_cast<CH::PropActionKind>(action.action))
		{
		case CH::PropActionKind::Change:
		{
			const std::int32_t models = modelCount(match);
			if (uses.changes <= 0 || models < 2)
			{
				return;
			}
			--uses.changes;
			uses.model = nextModel(uses, models);
			match.setPropState(player.playerId, uses.model, uses.locked, uses.wall);
			break;
		}
		case CH::PropActionKind::Lock:
			uses.locked = !uses.locked;
			match.setPropState(player.playerId, uses.model, uses.locked, uses.wall);
			break;
		case CH::PropActionKind::Wall:
			uses.wall = CH::PropWall{ 1, action.x, action.y, action.z, action.yaw, action.anchorX, action.anchorY };
			match.setPropState(player.playerId, uses.model, uses.locked, uses.wall);
			break;
		case CH::PropActionKind::Decoy:
		{
			if (uses.decoys <= 0)
			{
				return;
			}
			--uses.decoys;
			CH::PropEventPayload decoy{};
			decoy.kind = static_cast<std::int32_t>(CH::PropEventKind::Decoy);
			decoy.playerId = player.playerId;
			decoy.x = action.x;
			decoy.y = action.y;
			decoy.z = action.z;
			decoy.model = uses.model;
			match.propEvent(decoy);
			break;
		}
		case CH::PropActionKind::Flash:
		{
			if (uses.flashes <= 0 || m_phase != Phase::Hunting)
			{
				return;
			}
			--uses.flashes;
			CH::PropEventPayload flash{};
			flash.kind = static_cast<std::int32_t>(CH::PropEventKind::Flash);
			flash.playerId = player.playerId;
			flash.x = action.x;
			flash.y = action.y;
			flash.z = action.z;
			flash.radius = flashRadius;
			match.propEvent(flash);
			std::vector<std::int32_t> hunters;
			for (const PartyPlayer& hunter : match.players())
			{
				if (hunter.alive && hunter.fallbackTeam == hunterTeam)
				{
					hunters.push_back(hunter.playerId);
				}
			}
			// the CastServer's positions arrive later: the hit / miss notice then, to the prop if still in the match
			match.requestPositions(std::move(hunters), [&match, propId = player.playerId, x = action.x, y = action.y, z = action.z](
				const MatchContext::Positions& positions) {
				bool blinded = false;
				for (const auto& [id, at] : positions)
				{
					const float dx = at[0] - x;
					const float dy = at[1] - y;
					const float dz = at[2] - z;
					blinded = blinded || dx * dx + dy * dy + dz * dz <= flashRadius * flashRadius;
				}
				const auto prop = std::ranges::find(match.players(), propId, &PartyPlayer::playerId);
				if (prop != match.players().end())
				{
					match.announceToPlayer(blinded ? CH::Announcement::PropFlashHit : CH::Announcement::PropFlashMissed, *prop);
				}
			});
			break;
		}
		default:
			return;
		}
		sendPhase(match, player);
	}

	void PropHuntMode::onDeath(MatchContext& match, PartyPlayer& victim, PartyPlayer* killer)
	{
		eliminate(match, victim);
		if (victim.fallbackTeam == propTeam)
		{
			m_props.erase(victim.playerId);
			match.setPropState(victim.playerId, -1, false); // the found prop shows its character again
			if (killer != nullptr && killer != &victim && killer->fallbackTeam == hunterTeam)
			{
				scoreKill(match, *killer);
			}
		}
	}

	bool PropHuntMode::isRoundOver(MatchContext& match)
	{
		bool props = false;
		bool hunters = false;
		for (const PartyPlayer& player : match.players())
		{
			props = props || (player.alive && player.fallbackTeam == propTeam);
			hunters = hunters || (player.alive && player.fallbackTeam == hunterTeam);
		}
		return !props || !hunters;
	}

	void PropHuntMode::onRoundEnd(MatchContext& match, RoundEndReason)
	{
		bool propsAlive = false;
		for (const PartyPlayer& player : match.players())
		{
			propsAlive = propsAlive || (player.alive && player.fallbackTeam == propTeam);
		}
		const std::int32_t winners = propsAlive ? propTeam : hunterTeam;
		for (PartyPlayer& player : match.players())
		{
			const bool won = player.fallbackTeam == winners && (winners == hunterTeam || player.alive);
			if (won)
			{
				player.points += match.rules().lastStandingPoints;
				match.updatePoints(player);
			}
			match.setPropState(player.playerId, -1, false);
			match.setModifiers(player, CH::neutralModifiers());
		}
		match.announce(propsAlive ? CH::Announcement::PropPropsWin : CH::Announcement::PropHuntersWin);
		m_phase = Phase::Off;
		m_props.clear();
		match.propEvent({ static_cast<std::int32_t>(CH::PropEventKind::ClearDecoys) });
		sendPhases(match);
	}
}
