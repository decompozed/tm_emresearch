/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#include "HaveApi/Modes/ChickenRunMode.h"
#include <algorithm>
#include <vector>

namespace Main::HaveApi
{
	namespace
	{
		constexpr std::size_t explosivesSetting = 1;
		constexpr std::size_t bonusSetting = 2;
		constexpr std::int32_t settingOn = 1;
	}

	ChickenRunMode::ChickenRunMode() : m_random{ std::random_device{}() } {}
	ChickenRunMode::ChickenRunMode(std::uint32_t seed) : m_random{ seed } {}

	PartyPlayer* ChickenRunMode::find(MatchContext& match, std::int32_t playerId) const
	{
		const auto found = std::ranges::find(match.players(), playerId, &PartyPlayer::playerId);
		return found != match.players().end() ? &*found : nullptr;
	}

	void ChickenRunMode::onMatchStart(MatchContext& match)
	{
		for (PartyPlayer& player : match.players())
		{
			player.team = playerTeam;
			player.fallbackTeam = playerTeam;
		}
	}

	void ChickenRunMode::makeChicken(MatchContext& match, PartyPlayer& player, std::int32_t previous)
	{
		if (PartyPlayer* old = find(match, m_chicken); old != nullptr && old != &player)
		{
			old->team = playerTeam;
			old->fallbackTeam = playerTeam;
			match.updatePlayer(*old);
		}
		m_chicken = player.playerId;
		player.team = chickenTeam;
		player.fallbackTeam = chickenTeam;
		match.updatePlayer(player);
		m_tagBackBlocked = previous;
		m_tagBackUntil = match.roundElapsed() + tagBackTime;
		match.setMarked(m_chicken, previous);
	}

	void ChickenRunMode::drawChicken(MatchContext& match, std::int32_t excludedPlayerId)
	{
		std::vector<PartyPlayer*> candidates;
		for (PartyPlayer& player : match.players())
		{
			if (player.playerId != excludedPlayerId)
			{
				candidates.push_back(&player);
			}
		}
		if (candidates.empty())
		{
			m_chicken = 0;
			return;
		}
		std::uniform_int_distribution<std::size_t> pick{ 0, candidates.size() - 1 };
		makeChicken(match, *candidates[pick(m_random)], 0);
	}

	void ChickenRunMode::onRoundStart(MatchContext& match)
	{
		for (PartyPlayer& player : match.players())
		{
			player.alive = true;
			player.team = playerTeam;
			player.fallbackTeam = playerTeam;
		}
		m_chicken = 0;
		drawChicken(match, 0);
		for (PartyPlayer& player : match.players())
		{
			if (player.playerId != m_chicken)
			{
				match.updatePlayer(player);
			}
		}
	}

	void ChickenRunMode::onHit(MatchContext& match, PartyPlayer& victim, PartyPlayer& attacker, Common::HaveApi::DeathCause cause)
	{
		if (attacker.playerId != m_chicken || victim.playerId == m_chicken)
		{
			return;
		}
		if (victim.playerId == m_tagBackBlocked && match.roundElapsed() < m_tagBackUntil)
		{
			return; // no tag back
		}
		if (cause == Common::HaveApi::DeathCause::Explosive && match.settings().values[explosivesSetting] != settingOn)
		{
			return;
		}
		if (match.settings().values[bonusSetting] == settingOn)
		{
			scoreKill(match, attacker);
		}
		makeChicken(match, victim, attacker.playerId);
		match.announce(Common::HaveApi::Announcement::ChickenChosen, victim.name);
	}

	void ChickenRunMode::onPlayerLeft(MatchContext& match, PartyPlayer& player)
	{
		if (player.playerId == m_chicken)
		{
			m_chicken = 0;
			drawChicken(match, player.playerId);
			if (PartyPlayer* chosen = find(match, m_chicken))
			{
				match.announce(Common::HaveApi::Announcement::ChickenChosen, chosen->name);
			}
		}
	}

	void ChickenRunMode::onTick(MatchContext& match)
	{
		if (m_tagBackBlocked != 0 && match.roundElapsed() >= m_tagBackUntil)
		{
			m_tagBackBlocked = 0;
			match.setMarked(m_chicken, 0);
		}
	}

	void ChickenRunMode::onRoundEnd(MatchContext& match, RoundEndReason)
	{
		PartyPlayer* loser = find(match, m_chicken);
		for (PartyPlayer& player : match.players())
		{
			if (&player != loser)
			{
				player.points += match.rules().lastStandingPoints;
				match.updatePoints(player);
			}
		}
		if (loser != nullptr)
		{
			match.announce(Common::HaveApi::Announcement::ChickenPlayerEliminated, loser->name); // "<name> WAS ELIMINATED!"
		}
	}

	void ChickenRunMode::onMatchEnd(MatchContext& match)
	{
		if (const PartyPlayer* winner = bestByPoints(match.players()))
		{
			match.announce(Common::HaveApi::Announcement::ArenaWinner, winner->name); // "<name> HAS WON"
		}
	}

	std::optional<std::pair<Common::HaveApi::Announcement, std::string>> ChickenRunMode::roundStartAnnouncement(
		const MatchContext& match) const
	{
		const auto players = const_cast<MatchContext&>(match).players();
		const auto chosen = std::ranges::find(players, m_chicken, &PartyPlayer::playerId);
		if (chosen == players.end())
		{
			return std::nullopt;
		}
		return std::pair{ Common::HaveApi::Announcement::ChickenChosen, chosen->name };
	}
}
