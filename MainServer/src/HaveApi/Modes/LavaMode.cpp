/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#include "HaveApi/Modes/LavaMode.h"
#include <algorithm>

namespace Main::HaveApi
{
	namespace
	{
		constexpr std::size_t speedSetting = 0;
		constexpr std::size_t graceSetting = 1;
		constexpr std::size_t wallsSetting = 2;
		constexpr std::size_t hotPhaseSetting = 3;
		constexpr std::size_t sleepPhaseSetting = 4;
		constexpr std::int32_t settingOn = 1;
	}

	std::chrono::seconds LavaMode::gracePeriod(const MatchContext& match)
	{
		switch (match.settings().values[graceSetting])
		{
		case 1: return std::chrono::seconds{ 0 };  // Off
		case 3: return std::chrono::seconds{ 10 }; // Long
		default: return std::chrono::seconds{ 5 }; // Normal
		}
	}

	Common::HaveApi::LavaCommand LavaMode::command(const MatchContext& match, Common::HaveApi::LavaAction action, float factor) const
	{
		const auto speed = static_cast<float>(match.settings().values[speedSetting]);
		Common::HaveApi::LavaCommand result;
		result.action = action;
		result.wallsOn = match.settings().values[wallsSetting] == settingOn ? 1 : 0;
		result.floorSpeed = speed * match.rules().lavaSpeedScale;
		result.wallCloseSeconds = match.rules().lavaWallCloseSeconds * 8.0f / (std::max)(speed, 1.0f); // 8 = Normal
		result.speedFactor = factor;
		return result;
	}

	void LavaMode::onMatchStart(MatchContext& match)
	{
		match.controlLava({ Common::HaveApi::LavaAction::ProtectPlayers });
		std::int32_t team = 1;
		for (PartyPlayer& player : match.players())
		{
			player.team = team;           // everyone for themselves: explosives push the others
			player.fallbackTeam = team;
			++team;
		}
	}

	void LavaMode::onRoundStart(MatchContext& match)
	{
		m_started = m_hot = m_sleeping = m_starting = false;
		++m_round;
		reviveAll(match);
		match.setLavaPhase(false, false);
	}

	void LavaMode::onDeath(MatchContext& match, PartyPlayer& victim, PartyPlayer* killer)
	{
		eliminate(match, victim); // lava, walls, falls, own explosives: players cannot kill each other
		(void)killer;
	}

	void LavaMode::onTick(MatchContext& match)
	{
		const auto now = match.roundElapsed();
		if (!m_started)
		{
			// CastServer places the lava around the players' positions: every living player must be in the map first
			const bool everyoneIn = std::ranges::all_of(match.players(), [](const PartyPlayer& p) { return !p.alive || p.loaded; });
			// the CastServer's answer arrives later: one start request at a time, a stale answer (another round) ignored
			if (now >= (std::max)(gracePeriod(match), minimumStart) && everyoneIn && !m_starting)
			{
				m_starting = true;
				match.controlLava(command(match, Common::HaveApi::LavaAction::Start, 1.0f),
					[this, &match, round = m_round](bool started) {
						if (round != m_round)
						{
							return;
						}
						m_starting = false;
						if (!started)
						{
							return; // tried again at the next tick
						}
						m_started = true;
						m_startedAt = m_movingSince = match.roundElapsed();
						match.announce(Common::HaveApi::Announcement::LavaComing);
					});
			}
			return;
		}
		const float factor = m_hot ? 2.0f : 1.0f;
		if (m_sleeping)
		{
			if (now >= m_sleepUntil)
			{
				m_sleeping = false;
				m_movingSince = now;
				match.controlLava(command(match, Common::HaveApi::LavaAction::SetSpeed, factor));
				match.setLavaPhase(m_hot, false);
			}
			return;
		}
		if (!m_hot && match.settings().values[hotPhaseSetting] == settingOn && now - m_startedAt >= hotPhaseAfter)
		{
			m_hot = true;
			match.controlLava(command(match, Common::HaveApi::LavaAction::SetSpeed, 2.0f));
			match.announce(Common::HaveApi::Announcement::LavaHotPhase);
			match.setLavaPhase(true, false);
		}
		else if (match.settings().values[sleepPhaseSetting] == settingOn && now - m_movingSince >= sleepEvery)
		{
			m_sleeping = true;
			m_sleepUntil = now + sleepFor;
			match.controlLava(command(match, Common::HaveApi::LavaAction::SetSpeed, 0.0f));
			match.announce(Common::HaveApi::Announcement::LavaStopped);
			match.setLavaPhase(false, true);
		}
	}

	bool LavaMode::isRoundOver(MatchContext& match)
	{
		return match.players().size() >= 2 && aliveCount(match.players()) <= 1;
	}

	void LavaMode::onRoundEnd(MatchContext& match, RoundEndReason reason)
	{
		// the lava goes before the round-end respawn, otherwise the revived players die in it at once
		match.controlLava({ Common::HaveApi::LavaAction::Stop });
		match.setLavaPhase(false, false);
		m_started = m_starting = false;
		++m_round; // a start answer still on its way belongs to the round that ended
		if (reason == RoundEndReason::TimeUp)
		{
			return;
		}
		const auto survivor = std::ranges::find_if(match.players(), &PartyPlayer::alive);
		if (survivor == match.players().end())
		{
			return;
		}
		survivor->points += match.rules().lastStandingPoints;
		match.updatePoints(*survivor);
		match.announce(Common::HaveApi::Announcement::LavaWinner, survivor->name);
	}

	void LavaMode::onMatchEnd(MatchContext& match)
	{
		if (const PartyPlayer* winner = bestByPoints(match.players()))
		{
			match.announce(Common::HaveApi::Announcement::LavaWinner, winner->name);
		}
	}
}
