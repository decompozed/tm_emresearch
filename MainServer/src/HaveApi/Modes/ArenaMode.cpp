/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#include "HaveApi/Modes/ArenaMode.h"
#include <algorithm>

namespace Main::HaveApi
{
	namespace
	{
		constexpr std::size_t healWinnerSetting = 1;
		constexpr std::size_t radarPhaseSetting = 2;
		constexpr std::int32_t settingOn = 1;
	}

	void ArenaMode::onMatchStart(MatchContext& match)
	{
		std::int32_t team = 1;
		for (PartyPlayer& player : match.players())
		{
			player.team = team;           // everyone versus everyone: nobody shares a team
			player.fallbackTeam = team;
			++team;
		}
	}

	void ArenaMode::onRoundStart(MatchContext& match)
	{
		m_radar.reset(match);
		reviveAll(match);
	}

	void ArenaMode::onDeath(MatchContext& match, PartyPlayer& victim, PartyPlayer* killer)
	{
		eliminate(match, victim);
		if (killer != nullptr && killer != &victim)
		{
			scoreKill(match, *killer);
		}
	}

	bool ArenaMode::healSurvivorsAtRoundStart(const MatchContext& match) const
	{
		return match.settings().values[healWinnerSetting] == settingOn;
	}

	void ArenaMode::onTick(MatchContext& match)
	{
		m_radar.tick(match, match.settings().values[radarPhaseSetting] == settingOn);
	}

	bool ArenaMode::isRoundOver(MatchContext& match)
	{
		return match.players().size() >= 2 && aliveCount(match.players()) <= 1;
	}

	void ArenaMode::onRoundEnd(MatchContext& match, RoundEndReason reason)
	{
		m_radar.reset(match);
		if (reason == RoundEndReason::TimeUp)
		{
			match.announce(Common::HaveApi::Announcement::ArenaTimeUp);
			return;
		}
		const auto survivor = std::ranges::find_if(match.players(), &PartyPlayer::alive);
		if (survivor == match.players().end())
		{
			return;
		}
		survivor->points += match.rules().lastStandingPoints;
		match.updatePoints(*survivor);
		match.announce(Common::HaveApi::Announcement::ArenaWinner, survivor->name);
	}

	void ArenaMode::onMatchEnd(MatchContext& match)
	{
		if (const PartyPlayer* winner = bestByPoints(match.players()))
		{
			match.announce(Common::HaveApi::Announcement::ArenaWinner, winner->name);
		}
	}
}
