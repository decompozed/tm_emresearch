/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#ifndef MAIN_HAVE_API_MODES_TEAM_BASE_SPAWNS_H
#define MAIN_HAVE_API_MODES_TEAM_BASE_SPAWNS_H

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>
#include "HaveApi/Modes/PartyMode.h"
#include "HaveApi/PartySpawns.h"

namespace Main::HaveApi
{
	// Hardpoint / Headquarters / Domination respawns: each team at its official base, opposite teams never in the same
	// places. The DLL loads the map's TDM (else SAB) REDTEAM / BLUETEAM spawns into the red / blue lists; the host's client
	// picks every player's respawn (HOST_USER 0xACC110) in the list of the base that player's team uses. This sends
	// everybody (0x38) both bases' points of Common/include/HaveApi/PartySpawns.h (the nodes with a floor under them, none
	// near the other team's) and which base each team uses; again when that changes (Domination).
	class TeamBaseSpawns
	{
	public:
		void reset(std::uint32_t mapId);
		void update(MatchContext& match); // each team at its own base
		// baseOf[team]: the team whose base the players of `team` respawn at
		void update(MatchContext& match, const std::array<std::int32_t, 3>& baseOf);

		[[nodiscard]] std::vector<std::array<float, 2>> pointsOf(std::int32_t team) const;

	private:
		std::span<const Common::HaveApi::PartySpawn> m_spawns;
		std::optional<std::array<std::int32_t, 2>> m_sentBaseOf; // the last 0x38's
	};
}

#endif
