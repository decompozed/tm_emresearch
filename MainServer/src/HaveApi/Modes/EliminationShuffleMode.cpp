/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#include "HaveApi/Modes/EliminationShuffleMode.h"
#include <algorithm>
#include <vector>

namespace Main::HaveApi
{
	namespace
	{
		constexpr std::size_t roundsSetting = 1;

		[[nodiscard]] std::array<std::int32_t, 3> countTeams(std::span<const PartyPlayer> players, std::int32_t PartyPlayer::*field)
		{
			std::array<std::int32_t, 3> counts{};
			for (const PartyPlayer& player : players)
			{
				const std::int32_t team = player.*field;
				if (team == EliminationShuffleMode::red || team == EliminationShuffleMode::blue)
				{
					++counts[static_cast<std::size_t>(team)];
				}
			}
			return counts;
		}
	}

	EliminationShuffleMode::EliminationShuffleMode() : m_random{ std::random_device{}() } {}
	EliminationShuffleMode::EliminationShuffleMode(std::uint32_t seed) : m_random{ seed } {}

	// the match ends when a team won the rounds of the setting (isMatchDecided), however many rounds that takes
	std::uint32_t EliminationShuffleMode::roundsPerMatch(std::uint32_t) const
	{
		return maxRounds;
	}

	bool EliminationShuffleMode::isMatchDecided(const MatchContext&) const
	{
		const auto needed = static_cast<std::int32_t>(m_rounds);
		return m_wins[red] >= needed || m_wins[blue] >= needed;
	}

	void EliminationShuffleMode::sendRounds(MatchContext& match) const
	{
		match.setTeamRounds(m_wins[red], m_wins[blue], static_cast<std::int32_t>(m_rounds));
	}

	// Random and balanced, kept for the whole match.
	void EliminationShuffleMode::onMatchStart(MatchContext& match)
	{
		m_rounds = static_cast<std::uint32_t>(std::clamp(match.settings().values[roundsSetting], 1, 11));
		m_wins = {};
		std::vector<PartyPlayer*> order;
		for (PartyPlayer& player : match.players())
		{
			order.push_back(&player);
		}
		std::ranges::shuffle(order, m_random);
		const std::int32_t first = std::uniform_int_distribution<int>{ 0, 1 }(m_random) == 0 ? red : blue;
		for (std::size_t i = 0; i < order.size(); ++i)
		{
			order[i]->fallbackTeam = i % 2 == 0 ? first : (first == red ? blue : red);
			order[i]->team = order[i]->fallbackTeam;
		}
		sendRounds(match);
	}

	void EliminationShuffleMode::onRoundStart(MatchContext& match)
	{
		if (m_radar)
		{
			match.setRadar(false);
			m_radar = false;
		}
		reviveAll(match);
		sendRounds(match);
	}

	void EliminationShuffleMode::onDeath(MatchContext& match, PartyPlayer& victim, PartyPlayer* killer)
	{
		const std::int32_t victimTeam = victim.fallbackTeam;
		eliminate(match, victim);
		if (killer != nullptr && killer != &victim && killer->fallbackTeam != victimTeam)
		{
			scoreKill(match, *killer);
		}
	}

	// The radar for both teams in the last minute of the round.
	void EliminationShuffleMode::onTick(MatchContext& match)
	{
		const std::uint32_t length = match.roundSeconds();
		const auto elapsed = static_cast<std::uint32_t>(match.roundElapsed().count());
		const bool lastMinute = length != 0 && elapsed + radarSeconds >= length;
		if (lastMinute != m_radar)
		{
			m_radar = lastMinute;
			match.setRadar(lastMinute);
		}
	}

	bool EliminationShuffleMode::isRoundOver(MatchContext& match)
	{
		const auto members = countTeams(match.players(), &PartyPlayer::fallbackTeam);
		const auto alive = countTeams(match.players(), &PartyPlayer::team);
		return members[red] > 0 && members[blue] > 0 && (alive[red] == 0 || alive[blue] == 0);
	}

	void EliminationShuffleMode::onRoundEnd(MatchContext& match, RoundEndReason reason)
	{
		if (m_radar)
		{
			match.setRadar(false);
			m_radar = false;
		}
		// time up: a draw, nobody wins the round
		const auto alive = countTeams(match.players(), &PartyPlayer::team);
		std::int32_t winner = 0;
		if (reason == RoundEndReason::LastStanding && alive[red] != alive[blue])
		{
			winner = alive[red] > alive[blue] ? red : blue;
		}
		if (reason == RoundEndReason::TimeUp)
		{
			match.announce(Common::HaveApi::Announcement::TeamClashTimeUp);
		}
		if (winner != 0)
		{
			++m_wins[static_cast<std::size_t>(winner)];
			for (PartyPlayer& player : match.players())
			{
				if (player.fallbackTeam == winner)
				{
					player.points += match.rules().lastStandingPoints;
					match.updatePoints(player);
				}
			}
			match.announce(Common::HaveApi::teamWon(winner));
		}
		sendRounds(match);
	}

	void EliminationShuffleMode::onMatchEnd(MatchContext& match)
	{
		if (m_wins[red] != m_wins[blue])
		{
			match.announce(Common::HaveApi::teamWon(m_wins[red] > m_wins[blue] ? red : blue));
		}
		else
		{
			match.announce(Common::HaveApi::Announcement::TeamClashTimeUp); // a draw: no winner banner
		}
	}

	std::int32_t EliminationShuffleMode::teamForLateJoiner(MatchContext& match)
	{
		const auto members = countTeams(match.players(), &PartyPlayer::fallbackTeam);
		return members[blue] < members[red] ? blue : red;
	}
}
