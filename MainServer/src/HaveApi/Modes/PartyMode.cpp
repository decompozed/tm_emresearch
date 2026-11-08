/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#include "HaveApi/Modes/PartyMode.h"
#include <algorithm>
#include <cmath>
#include "HaveApi/Modes/ArenaMode.h"
#include "HaveApi/Modes/BattleRoyaleMode.h"
#include "HaveApi/Modes/LavaMode.h"
#include "HaveApi/Modes/ChaserMode.h"
#include "HaveApi/Modes/ChickenRunMode.h"
#include "HaveApi/Modes/HardpointMode.h"
#include "HaveApi/Modes/HeadquartersMode.h"
#include "HaveApi/Modes/PropHuntMode.h"
#include "HaveApi/Modes/DominationMode.h"
#include "HaveApi/Modes/EliminationShuffleMode.h"
#include "HaveApi/Modes/TeamClashMode.h"

namespace Main::HaveApi
{
	namespace
	{
		// Party games whose rules are not written yet: everyone for themselves, respawn on, a kill is worth
		// KillPoints, the rounds end at the time limit.
		class UnrulyMode final : public PartyMode
		{
		public:
			void onMatchStart(MatchContext& match) override
			{
				std::int32_t team = 1;
				for (PartyPlayer& player : match.players())
				{
					player.team = team;
					player.fallbackTeam = team;
					++team;
				}
			}

			void onRoundStart(MatchContext&) override {}

			void onDeath(MatchContext& match, PartyPlayer& victim, PartyPlayer* killer) override
			{
				if (killer == nullptr || killer == &victim)
				{
					return;
				}
				killer->points += match.rules().killPoints;
				match.updatePoints(*killer);
			}

			[[nodiscard]] bool isRoundOver(MatchContext&) override { return false; }
			void onRoundEnd(MatchContext&, RoundEndReason) override {}
			void onMatchEnd(MatchContext&) override {}
			[[nodiscard]] bool respawnAllowed() const override { return true; }
		};
	}

	std::unique_ptr<PartyMode> createPartyMode(Common::HaveApi::PartyModeId id)
	{
		switch (id)
		{
		case Common::HaveApi::PartyModeId::Arena:
			return std::make_unique<ArenaMode>();
		case Common::HaveApi::PartyModeId::TeamClash:
			return std::make_unique<TeamClashMode>();
		case Common::HaveApi::PartyModeId::ChickenRun:
			return std::make_unique<ChickenRunMode>();
		case Common::HaveApi::PartyModeId::Chaser:
			return std::make_unique<ChaserMode>();
		case Common::HaveApi::PartyModeId::LavaEscape:
			return std::make_unique<LavaMode>();
		case Common::HaveApi::PartyModeId::Hardpoint:
			return std::make_unique<HardpointMode>();
		case Common::HaveApi::PartyModeId::Headquarters:
			return std::make_unique<HeadquartersMode>();
		case Common::HaveApi::PartyModeId::PropHunt:
			return std::make_unique<PropHuntMode>();
		case Common::HaveApi::PartyModeId::EliminationShuffle:
			return std::make_unique<EliminationShuffleMode>();
		case Common::HaveApi::PartyModeId::Domination:
			return std::make_unique<DominationMode>();
		case Common::HaveApi::PartyModeId::BattleRoyale:
			return std::make_unique<BattleRoyaleMode>();
		default:
			return std::make_unique<UnrulyMode>();
		}
	}

	void rememberSpawn(std::vector<SpawnPoint>& spawns, SpawnPoint spawn)
	{
		if (std::ranges::any_of(spawns, [&](const SpawnPoint& s) { return std::hypot(s.x - spawn.x, s.y - spawn.y) < spawnPointSpacing; }))
		{
			return;
		}
		if (spawns.size() >= maxSpawnPoints)
		{
			spawns.erase(spawns.begin());
		}
		spawns.push_back(spawn);
	}

	std::size_t aliveCount(std::span<const PartyPlayer> players)
	{
		return static_cast<std::size_t>(std::ranges::count_if(players, &PartyPlayer::alive));
	}

	PartyPlayer* bestByPoints(std::span<PartyPlayer> players)
	{
		const auto best = std::ranges::max_element(players, {}, &PartyPlayer::points);
		return best != players.end() ? &*best : nullptr;
	}

	std::int32_t PartyMode::teamForLateJoiner(MatchContext& match)
	{
		std::int32_t highest = 0;
		for (const PartyPlayer& player : match.players())
		{
			highest = (std::max)(highest, player.fallbackTeam);
		}
		return highest + 1;
	}

	void reviveAll(MatchContext& match)
	{
		for (PartyPlayer& player : match.players())
		{
			player.alive = true;
			player.team = player.fallbackTeam;
			match.updatePlayer(player);
		}
	}

	void eliminate(MatchContext& match, PartyPlayer& victim)
	{
		victim.alive = false;
		victim.team = 0;
		match.updatePlayer(victim);
	}

	void scoreKill(MatchContext& match, PartyPlayer& killer)
	{
		killer.points += match.rules().killPoints;
		match.updatePoints(killer);
	}

	void RadarPhase::reset(MatchContext& match)
	{
		m_active = false;
		match.setRadar(false);
	}

	void RadarPhase::tick(MatchContext& match, bool enabled)
	{
		if (m_active || !enabled || match.roundSeconds() == 0)
		{
			return;
		}
		if (match.roundElapsed() >= std::chrono::seconds{ match.roundSeconds() / 2 })
		{
			m_active = true;
			match.setRadar(true);
		}
	}
}
