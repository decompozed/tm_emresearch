/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#include "HaveApi/Modes/ChaserMode.h"
#include <algorithm>

namespace Main::HaveApi
{
	namespace
	{
		constexpr std::size_t maxTargets = 15; // markers of the DLL
	}

	ChaserMode::ChaserMode() : m_random{ std::random_device{}() } {}
	ChaserMode::ChaserMode(std::uint32_t seed) : m_random{ seed } {}

	PartyPlayer* ChaserMode::find(MatchContext& match, std::int32_t playerId) const
	{
		const auto found = std::ranges::find(match.players(), playerId, &PartyPlayer::playerId);
		return found != match.players().end() ? &*found : nullptr;
	}

	bool ChaserMode::isTarget(std::int32_t playerId) const
	{
		return std::ranges::find(m_targets, playerId) != m_targets.end();
	}

	std::int32_t ChaserMode::markedTarget(MatchContext& match) const
	{
		for (const std::int32_t target : m_targets)
		{
			if (const PartyPlayer* player = find(match, target); player != nullptr && player->alive)
			{
				return target;
			}
		}
		return 0;
	}

	void ChaserMode::sendChaserState(MatchContext& match)
	{
		std::vector<std::int8_t> states;
		for (const std::int32_t target : m_targets)
		{
			const PartyPlayer* player = find(match, target);
			states.push_back(player != nullptr && player->alive ? targetAlive : targetEliminated);
		}
		match.setChaser(markedTarget(match), states);
	}

	void ChaserMode::onMatchStart(MatchContext& match)
	{
		for (PartyPlayer& player : match.players())
		{
			player.team = runnerTeam;
			player.fallbackTeam = runnerTeam;
		}
	}

	void ChaserMode::onRoundStart(MatchContext& match)
	{
		m_chaser = 0;
		m_targets.clear();
		auto players = match.players();
		if (!players.empty())
		{
			// the chaser: among the players who were the chaser the fewest times
			int fewest = m_timesChaser[players.front().playerId];
			for (const PartyPlayer& player : players)
			{
				fewest = (std::min)(fewest, m_timesChaser[player.playerId]);
			}
			std::vector<std::int32_t> candidates;
			for (const PartyPlayer& player : players)
			{
				if (m_timesChaser[player.playerId] == fewest)
				{
					candidates.push_back(player.playerId);
				}
			}
			std::uniform_int_distribution<std::size_t> pick{ 0, candidates.size() - 1 };
			m_chaser = candidates[pick(m_random)];
			++m_timesChaser[m_chaser];

			// the marked targets: one runner in three, at least one
			std::vector<std::int32_t> runners;
			for (const PartyPlayer& player : players)
			{
				if (player.playerId != m_chaser)
				{
					runners.push_back(player.playerId);
				}
			}
			std::ranges::shuffle(runners, m_random);
			const std::size_t count = (std::min)({ runners.size(), (runners.size() + 2) / 3, maxTargets });
			m_targets.assign(runners.begin(), runners.begin() + static_cast<std::ptrdiff_t>(count));
		}
		for (PartyPlayer& player : players)
		{
			player.fallbackTeam = player.playerId == m_chaser ? chaserTeam : runnerTeam;
		}
		reviveAll(match);
		match.setMarked(m_chaser, 0);
		sendChaserState(match);
	}

	void ChaserMode::onDeath(MatchContext& match, PartyPlayer& victim, PartyPlayer* killer)
	{
		eliminate(match, victim);
		if (killer != nullptr && killer != &victim)
		{
			const bool chaserKill = killer->playerId == m_chaser;
			if (chaserKill || victim.playerId == m_chaser) // runners cannot hurt each other (DLL), but a report could
			{
				scoreKill(match, *killer);
			}
			if (chaserKill && isTarget(victim.playerId))
			{
				killer->points += match.rules().markedTargetPoints;
				match.updatePoints(*killer);
			}
		}
		if (isTarget(victim.playerId))
		{
			sendChaserState(match);
		}
	}

	void ChaserMode::onPlayerLeft(MatchContext& match, PartyPlayer& player)
	{
		if (isTarget(player.playerId))
		{
			std::erase(m_targets, player.playerId);
			sendChaserState(match);
		}
		// a leaving chaser: the match checks the round again after removing it (isRoundOver: no chaser)
	}

	bool ChaserMode::isRoundOver(MatchContext& match)
	{
		if (match.players().size() < 2)
		{
			return false; // a single player waits for the time limit (or the match ends)
		}
		const PartyPlayer* chaser = find(match, m_chaser);
		if (chaser == nullptr || !chaser->alive)
		{
			return true;
		}
		return std::ranges::none_of(match.players(), [&](const PartyPlayer& p) { return p.alive && p.playerId != m_chaser; });
	}

	void ChaserMode::onRoundEnd(MatchContext& match, RoundEndReason reason)
	{
		PartyPlayer* chaser = find(match, m_chaser);
		const bool chaserWon = reason == RoundEndReason::LastStanding && chaser != nullptr && chaser->alive;
		if (chaserWon)
		{
			chaser->points += match.rules().lastStandingPoints;
			match.updatePoints(*chaser);
			match.announce(Common::HaveApi::Announcement::ChaserAllEliminated);
			return;
		}
		for (PartyPlayer& player : match.players())
		{
			if (player.alive && player.playerId != m_chaser)
			{
				player.points += match.rules().lastStandingPoints;
				match.updatePoints(player);
			}
		}
		match.announce(reason == RoundEndReason::TimeUp ? Common::HaveApi::Announcement::ChaserTimeUp
			: Common::HaveApi::Announcement::ChaserEliminated);
	}

	void ChaserMode::onMatchEnd(MatchContext& match)
	{
		if (const PartyPlayer* winner = bestByPoints(match.players()))
		{
			match.announce(Common::HaveApi::Announcement::ArenaWinner, winner->name); // "<name> HAS WON"
		}
	}

	std::optional<std::pair<Common::HaveApi::Announcement, std::string>> ChaserMode::roundStartAnnouncement(
		const MatchContext& match) const
	{
		const auto players = const_cast<MatchContext&>(match).players();
		const auto chosen = std::ranges::find(players, m_chaser, &PartyPlayer::playerId);
		if (chosen == players.end())
		{
			return std::nullopt;
		}
		return std::pair{ Common::HaveApi::Announcement::ChaserChosen, chosen->name };
	}
}
