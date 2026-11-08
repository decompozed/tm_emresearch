/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#include "HaveApi/Modes/TeamBaseSpawns.h"
#include <algorithm>

namespace Main::HaveApi
{
	namespace
	{
		namespace CH = Common::HaveApi;

		constexpr std::int32_t redTeam = 1;
		constexpr std::int32_t blueTeam = 2;
	}

	void TeamBaseSpawns::reset(std::uint32_t mapId)
	{
		m_spawns = CH::findPartySpawns(mapId);
		m_sentBaseOf.reset();
	}

	std::vector<std::array<float, 2>> TeamBaseSpawns::pointsOf(std::int32_t team) const
	{
		std::vector<std::array<float, 2>> points;
		for (const CH::PartySpawn& spawn : m_spawns)
		{
			if (spawn.team == team)
			{
				points.push_back({ spawn.x, spawn.y });
			}
		}
		return points;
	}

	void TeamBaseSpawns::update(MatchContext& match)
	{
		update(match, { 0, redTeam, blueTeam });
	}

	void TeamBaseSpawns::update(MatchContext& match, const std::array<std::int32_t, 3>& baseOf)
	{
		const std::array<std::int32_t, 2> current{ baseOf[redTeam], baseOf[blueTeam] };
		if (m_sentBaseOf == current)
		{
			return;
		}
		m_sentBaseOf = current;
		CH::TeamBasesPayload payload{};
		payload.baseOf = current;
		for (const std::int32_t base : { redTeam, blueTeam })
		{
			const auto points = pointsOf(base);
			const auto index = static_cast<std::size_t>(base - 1);
			payload.counts[index] = static_cast<std::int32_t>((std::min)(points.size(), payload.points[index].size()));
			for (std::size_t i = 0; i < static_cast<std::size_t>(payload.counts[index]); ++i)
			{
				payload.points[index][i] = points[i];
			}
		}
		match.showTeamBases(payload);
	}
}
