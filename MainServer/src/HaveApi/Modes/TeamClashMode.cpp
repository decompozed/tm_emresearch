/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#include "HaveApi/Modes/TeamClashMode.h"
#include <algorithm>
#include <array>
#include <numeric>
#include <vector>

namespace Main::HaveApi
{
	namespace
	{
		constexpr std::size_t keepTeamsSetting = 1;
		constexpr std::size_t radarPhaseSetting = 2;
		constexpr std::size_t teamCountSetting = 3;
		constexpr std::int32_t settingOn = 1;

		// players per team among the living (team) or among all (fallbackTeam)
		[[nodiscard]] std::array<std::int32_t, Common::HaveApi::teamClashMaxTeams + 1> countTeams(std::span<const PartyPlayer> players,
			std::int32_t PartyPlayer::*field)
		{
			std::array<std::int32_t, Common::HaveApi::teamClashMaxTeams + 1> counts{};
			for (const PartyPlayer& player : players)
			{
				const std::int32_t team = player.*field;
				if (team >= 1 && team <= Common::HaveApi::teamClashMaxTeams)
				{
					++counts[static_cast<std::size_t>(team)];
				}
			}
			return counts;
		}

		[[nodiscard]] std::int32_t teamsWithPlayers(std::span<const PartyPlayer> players, std::int32_t PartyPlayer::*field)
		{
			const auto counts = countTeams(players, field);
			return static_cast<std::int32_t>(std::ranges::count_if(counts, [](std::int32_t n) { return n > 0; }));
		}
	}

	TeamClashMode::TeamClashMode() : m_random{ std::random_device{}() } {}
	TeamClashMode::TeamClashMode(std::uint32_t seed) : m_random{ seed } {}

	std::int32_t TeamClashMode::teamCount(const MatchContext& match)
	{
		return std::clamp(match.settings().values[teamCountSetting], 3, Common::HaveApi::teamClashMaxTeams);
	}

	// Random and balanced: the players in random order take the teams in turn, the teams in random order too (so that
	// with fewer players than teams the empty ones are not always the last).
	void TeamClashMode::drawTeams(MatchContext& match)
	{
		std::vector<PartyPlayer*> order;
		for (PartyPlayer& player : match.players())
		{
			order.push_back(&player);
		}
		std::ranges::shuffle(order, m_random);
		std::vector<std::int32_t> teams(static_cast<std::size_t>(teamCount(match)));
		std::iota(teams.begin(), teams.end(), 1);
		std::ranges::shuffle(teams, m_random);
		for (std::size_t i = 0; i < order.size(); ++i)
		{
			order[i]->fallbackTeam = teams[i % teams.size()];
			order[i]->team = order[i]->fallbackTeam;
		}
		m_teamsDrawn = true;
	}

	void TeamClashMode::onMatchStart(MatchContext& match)
	{
		drawTeams(match);
	}

	void TeamClashMode::onRoundStart(MatchContext& match)
	{
		m_radar.reset(match);
		const bool keepTeams = match.settings().values[keepTeamsSetting] == settingOn;
		// keep teams Off: new teams every round. On: also new teams when the players who left the match leave a
		// single team (its rounds could only end at the time limit).
		const bool singleTeam = match.players().size() >= 2 && teamsWithPlayers(match.players(), &PartyPlayer::fallbackTeam) < 2;
		if (!m_teamsDrawn && (!keepTeams || singleTeam))
		{
			drawTeams(match);
		}
		m_teamsDrawn = false;
		reviveAll(match);
	}

	void TeamClashMode::onDeath(MatchContext& match, PartyPlayer& victim, PartyPlayer* killer)
	{
		const std::int32_t victimTeam = victim.fallbackTeam;
		eliminate(match, victim);
		if (killer != nullptr && killer != &victim && killer->fallbackTeam != victimTeam)
		{
			scoreKill(match, *killer);
		}
		const bool teamWipedOut = countTeams(match.players(), &PartyPlayer::team)[static_cast<std::size_t>(victimTeam)] == 0;
		if (teamWipedOut && !isRoundOver(match) && victimTeam >= 1 && victimTeam <= Common::HaveApi::teamClashMaxTeams)
		{
			match.announce(Common::HaveApi::teamEliminated(victimTeam)); // the round end announces the winner instead
		}
	}

	void TeamClashMode::onTick(MatchContext& match)
	{
		m_radar.tick(match, match.settings().values[radarPhaseSetting] == settingOn);
	}

	bool TeamClashMode::isRoundOver(MatchContext& match)
	{
		return teamsWithPlayers(match.players(), &PartyPlayer::fallbackTeam) >= 2 &&
			teamsWithPlayers(match.players(), &PartyPlayer::team) <= 1;
	}

	void TeamClashMode::onRoundEnd(MatchContext& match, RoundEndReason reason)
	{
		m_radar.reset(match);
		if (reason == RoundEndReason::TimeUp)
		{
			match.announce(Common::HaveApi::Announcement::TeamClashTimeUp);
			return;
		}
		const auto survivor = std::ranges::find_if(match.players(), &PartyPlayer::alive);
		if (survivor == match.players().end())
		{
			return;
		}
		const std::int32_t winner = survivor->fallbackTeam;
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

	void TeamClashMode::onMatchEnd(MatchContext& match)
	{
		if (match.settings().values[keepTeamsSetting] == settingOn)
		{
			std::array<std::int32_t, Common::HaveApi::teamClashMaxTeams + 1> points{};
			const auto members = countTeams(match.players(), &PartyPlayer::fallbackTeam);
			for (const PartyPlayer& player : match.players())
			{
				if (player.fallbackTeam >= 1 && player.fallbackTeam <= Common::HaveApi::teamClashMaxTeams)
				{
					points[static_cast<std::size_t>(player.fallbackTeam)] += player.points;
				}
			}
			std::int32_t best = 0;
			for (std::int32_t team = 1; team <= Common::HaveApi::teamClashMaxTeams; ++team)
			{
				const auto index = static_cast<std::size_t>(team);
				if (members[index] > 0 && (best == 0 || points[index] > points[static_cast<std::size_t>(best)]))
				{
					best = team;
				}
			}
			if (best != 0)
			{
				match.announce(Common::HaveApi::teamWon(best));
			}
			return;
		}
		if (const PartyPlayer* winner = bestByPoints(match.players()))
		{
			match.announce(Common::HaveApi::Announcement::ArenaWinner, winner->name); // "<name> HAS WON"
		}
	}

	std::optional<std::pair<Common::HaveApi::Announcement, std::string>> TeamClashMode::roundStartAnnouncement(
		const MatchContext& match) const
	{
		return std::pair{ Common::HaveApi::teamClashStart(teamCount(match)), std::string{} }; // "BATTLE START! X vs Y vs Z"
	}

	std::int32_t TeamClashMode::teamForLateJoiner(MatchContext& match)
	{
		const auto members = countTeams(match.players(), &PartyPlayer::fallbackTeam);
		std::int32_t smallest = 1;
		for (std::int32_t team = 2; team <= teamCount(match); ++team)
		{
			if (members[static_cast<std::size_t>(team)] < members[static_cast<std::size_t>(smallest)])
			{
				smallest = team;
			}
		}
		return smallest;
	}
}
