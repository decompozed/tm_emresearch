/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#ifndef MAIN_HAVE_API_MODES_PARTY_MODE_H
#define MAIN_HAVE_API_MODES_PARTY_MODE_H

#include <array>
#include <functional>
#include <map>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include "HaveApi/Announcements.h"
#include "HaveApi/Catalog.h"
#include "HaveApi/CombatEvent.h"
#include "HaveApi/Lava.h"
#include "HaveApi/Messages.h"

namespace Main::HaveApi
{
	// One player of a party match as the party rules see it.
	struct PartyPlayer
	{
		std::uint32_t sessionId{};
		std::int32_t playerId{};         // id the DLL knows the player by (unique id without the top bit)
		std::string name;
		std::int32_t team{};             // 0 = eliminated; otherwise the team or the role (mode dependent)
		std::int32_t fallbackTeam{};     // team shown while `team` is 0
		std::int32_t points = 0;
		std::int32_t kills = 0;
		std::int32_t deaths = 0;
		bool alive = true;
		bool loaded = false;             // the client finished loading (order 107, extra 41)
		std::int32_t placement = 0;
	};

	// Party rules the DLL does not fix: design decisions, configurable in [HaveApi] (doc/have_api_server_open_questions.md).
	struct PartyRules
	{
		std::uint32_t roundsPerMatch = 3;
		std::int32_t killPoints = 1;
		std::int32_t lastStandingPoints = 3;
		std::int32_t markedTargetPoints = 2;
		float lavaSpeedScale = 3.0f;
		float lavaWallCloseSeconds = 120.0f;
	};

	// Where a client respawned a player (x / y; the respawn itself is never changed).
	struct SpawnPoint
	{
		float x{};
		float y{};
	};

	inline constexpr float spawnPointSpacing = 300.0f;     // spawns closer than this are the same spawn point
	inline constexpr std::size_t maxSpawnPoints = 64;
	// Adds `spawn` unless a known one is within spawnPointSpacing; at maxSpawnPoints the oldest is dropped.
	void rememberSpawn(std::vector<SpawnPoint>& spawns, SpawnPoint spawn);

	// What a party mode may read and do during a match. Implemented by PartyMatch, faked by the unit tests.
	class MatchContext
	{
	public:
		virtual ~MatchContext() = default;

		[[nodiscard]] virtual std::span<PartyPlayer> players() = 0;
		[[nodiscard]] virtual const Common::HaveApi::PartySettingsPayload& settings() const = 0;
		[[nodiscard]] virtual const PartyRules& rules() const = 0;
		[[nodiscard]] virtual std::chrono::seconds roundElapsed() const = 0;
		[[nodiscard]] virtual std::uint32_t roundSeconds() const = 0;       // 0 = no time limit

		virtual void updatePlayer(const PartyPlayer& player) = 0;        // 0x0D: team / eliminated
		virtual void updatePoints(const PartyPlayer& player) = 0;        // 0x13
		virtual void restoreHealth(const PartyPlayer& player) = 0;       // 0x14 with restore full health
		virtual void setRadar(bool active) = 0;                          // 0x1B
		virtual void setMarked(std::int32_t first, std::int32_t second) = 0; // 0x17 (battle dialog +0x8B6 / +0x8BA)
		virtual void setChaser(std::int32_t playerId, std::span<const std::int8_t> targetStates) = 0; // 0x1A + 0x19
		// Lava Escape: CastServer places / moves / stops the lava, the clients get 0x23 / 0x24; `done` (optional) learns
		// whether it was done, when the CastServer's answer arrives (never waits)
		virtual void controlLava(const Common::HaveApi::LavaCommand& command, std::function<void(bool)> done = {}) = 0;
		virtual void setLavaPhase(bool hot, bool sleeping) = 0;          // 0x25
		// Hardpoint
		[[nodiscard]] virtual std::uint32_t mapId() const = 0;
		virtual void showZone(bool visible, float x, float y, std::int32_t state) = 0; // 0x27 (+ kept for players loading in)
		virtual void setZoneScore(std::int32_t red, std::int32_t blue, std::int32_t limit) = 0; // 0x28
		// CastServer: the zone (occupants; a respawn within keepOut gets the spawn grace); inactive = cleared
		virtual bool placeZone(bool active, float x, float y, float radius, float keepOut) = 0;
		[[nodiscard]] virtual std::vector<std::int32_t> playersInZone() = 0; // player ids
		// the spawn points the clients used in this match (from the Respawn combat events)
		[[nodiscard]] virtual std::span<const SpawnPoint> spawnPoints() const = 0;
		virtual void announce(Common::HaveApi::Announcement event, std::string_view text = {}) = 0; // 0x16
		virtual void announceToTeam(Common::HaveApi::Announcement event, std::int32_t team) = 0; // 0x16 to one team
		virtual void announceToPlayer(Common::HaveApi::Announcement event, const PartyPlayer& player, std::string_view text = {}) = 0; // 0x16 to one player
		// where CastServer last saw these players (its position packets), when its answers arrive; a player it has not
		// seen yet is missing from the map
		using Positions = std::map<std::int32_t, std::array<float, 3>>; // player id -> position
		virtual void requestPositions(std::vector<std::int32_t> playerIds, std::function<void(const Positions&)> done) = 0;
		virtual void killPlayer(const PartyPlayer& player) = 0; // dies for everybody (CastServer, as a lava death)
		// Headquarters
		virtual void showHqProgress(const Common::HaveApi::HqProgressPayload& progress) = 0; // 0x29 (+ kept for players loading in)
		// the players of `team` (fallback team) do not respawn (0x0E per player); 0 = everybody respawns again, the
		// blocked team's dead at once (0x11)
		virtual void setRespawnBlockedTeam(std::int32_t team) = 0;
		// Prop Hunt
		// health != 0: the player's hit points set once, on every client at the same moment (not kept for late loaders)
		virtual void setModifiers(const PartyPlayer& player, const Common::HaveApi::Modifiers& modifiers, std::int32_t health = 0) = 0; // 0x14 (kept)
		virtual void setPropState(std::int32_t playerId, std::int32_t model, bool locked, const Common::HaveApi::PropWall& wall = {}) = 0;            // 0x2A (kept)
		virtual void propEvent(const Common::HaveApi::PropEventPayload& event) = 0;                      // 0x2B
		virtual void sendPropPhase(const PartyPlayer& player, const Common::HaveApi::PropPhasePayload& phase) = 0; // 0x2C (kept)
		// Elimination Shuffle: the rounds each team won and the match's rounds (0x36 with the round, kept for players
		// loading in)
		virtual void setTeamRounds(std::int32_t /*red*/, std::int32_t /*blue*/, std::int32_t /*rounds*/) {}
		// Domination: zone `index` (0..2) at CastServer, a square of half side `halfSize` counting the players between zMin
		// and zMax (a respawn within keepOut gets the spawn grace); its occupants; the zones (0x37, kept for players
		// loading in); a player's spawns (0x38, kept per player); every player to another map (PartyMatch::reloadMap)
		virtual bool placeSquareZone(std::size_t /*index*/, bool /*active*/, float /*x*/, float /*y*/, float /*halfSize*/,
			float /*zMin*/, float /*zMax*/, float /*keepOut*/) { return false; }
		[[nodiscard]] virtual std::vector<std::int32_t> playersInZone(std::size_t /*index*/) { return {}; }
		virtual void showDominationZones(const Common::HaveApi::DominationZonesPayload& /*zones*/) {}
		virtual void showTeamBases(const Common::HaveApi::TeamBasesPayload& /*bases*/) {} // 0x38 to everybody (+ kept for players loading in)
		virtual bool changeMap(std::uint32_t /*mapId*/) { return false; }
		// 0x12: spectate
		virtual void spectate(const PartyPlayer& /*spectator*/, std::int32_t /*preferredPlayerId*/) {}
		virtual void showPreparePanel(std::int32_t /*seconds*/) {} // 0x15
		// Battle Royale: 0x3A (kept for players loading in), 0x3B, 0x02 / 0x04 to one player
		[[nodiscard]] virtual std::chrono::milliseconds roundElapsedMs() const { return roundElapsed(); }
		[[nodiscard]] virtual std::int32_t maxHealth() const { return 2000; }
		virtual void setStorm(const Common::HaveApi::BattleZonePayload& /*zone*/) {}
		virtual void clearStorm() {}
		[[nodiscard]] virtual std::vector<std::int32_t> playersOutsideStorm() const { return {}; }
		[[nodiscard]] virtual bool wasOutsideStorm(std::int32_t /*playerId*/, std::chrono::milliseconds /*within*/) const { return false; }
		virtual void sendStormDamage(std::span<const Common::HaveApi::StormHit> /*hits*/) {}
		virtual void statusText(const PartyPlayer& /*player*/, std::string_view /*text*/) {}
		virtual void playSound(const PartyPlayer& /*player*/, std::string_view /*sound*/) {}
		virtual void showSpawnAreas(const Common::HaveApi::BattleSpawnsPayload& /*areas*/) {} // 0x3D (+ kept for players loading in)
	};

	enum class RoundEndReason { LastStanding, TimeUp };

	// Rules of one party game. The match (PartyMatch) owns the timing: rounds, result screen, match end.
	class PartyMode
	{
	public:
		virtual ~PartyMode() = default;

		virtual void onMatchStart(MatchContext& match) = 0;               // teams / roles of the first round
		virtual void onRoundStart(MatchContext& match) = 0;
		virtual void onDeath(MatchContext& match, PartyPlayer& victim, PartyPlayer* killer) = 0;
		virtual void onRespawn(MatchContext&, PartyPlayer&) {}
		// A player loading in while a round runs (late joiner) of a mode with respawn: true = it does not appear but
		// waits dead (death camera, seen by nobody else) until its team may respawn again.
		[[nodiscard]] virtual bool keepsLateJoinerDead(const PartyPlayer&) const { return false; }
		// attacker damaged victim (weapon or explosive), also for the hit that kills (before onDeath)
		virtual void onHit(MatchContext&, PartyPlayer& /*victim*/, PartyPlayer& /*attacker*/, Common::HaveApi::DeathCause) {}
		virtual void onPlayerLeft(MatchContext&, PartyPlayer&) {}
		virtual void onTick(MatchContext&) {}                             // once a second while a round runs
		// a player's ability key (DLL message 0x2D, Prop Hunt)
		virtual void onPropAction(MatchContext&, PartyPlayer&, const Common::HaveApi::PropActionPayload&) {}
		// 0x3C from the host's DLL
		virtual void onStormLethal(MatchContext&, PartyPlayer& /*victim*/) {}
		[[nodiscard]] virtual bool isRoundOver(MatchContext& match) = 0;
		virtual void onRoundEnd(MatchContext& match, RoundEndReason reason) = 0;
		virtual void onMatchEnd(MatchContext& match) = 0;                 // winner announcement
		[[nodiscard]] virtual bool respawnAllowed() const = 0;            // during a round (0x0E flag)
		// whether the players alive at the end of a round get full health for the next one (the others respawn)
		[[nodiscard]] virtual bool healSurvivorsAtRoundStart(const MatchContext&) const { return true; }
		// announcement shown when a round starts (sent by the match to the loaded players and to those loading in)
		[[nodiscard]] virtual std::optional<std::pair<Common::HaveApi::Announcement, std::string>> roundStartAnnouncement(
			const MatchContext&) const { return std::nullopt; }
		// team of a player joining a running match; default: a team of its own
		[[nodiscard]] virtual std::int32_t teamForLateJoiner(MatchContext& match);
		// rounds of a match ([HaveApi] RoundsPerMatch unless the game has its own rule)
		[[nodiscard]] virtual std::uint32_t roundsPerMatch(std::uint32_t configured) const { return configured; }
		// "Round N" / "Round Result" banner (0x10); games without rounds show none
		[[nodiscard]] virtual bool showsRounds() const { return true; }
		// every round on another map (PartyMatch::reloadMap between the rounds; Elimination Shuffle)
		[[nodiscard]] virtual bool changesMapEveryRound() const { return false; }
		// the match is over before its last round (a team cannot be caught any more)
		[[nodiscard]] virtual bool isMatchDecided(const MatchContext&) const { return false; }
		[[nodiscard]] virtual bool acceptsLateJoiners() const { return true; }
	};

	[[nodiscard]] std::unique_ptr<PartyMode> createPartyMode(Common::HaveApi::PartyModeId id);

	// Helpers shared by the modes.
	[[nodiscard]] std::size_t aliveCount(std::span<const PartyPlayer> players);
	[[nodiscard]] PartyPlayer* bestByPoints(std::span<PartyPlayer> players);

	// Elimination rounds (Arena, Team Clash): the dead wait for the next round.
	void reviveAll(MatchContext& match);                         // round start: everybody alive in its team again
	void eliminate(MatchContext& match, PartyPlayer& victim);     // team 0: the DLL counts the living by team != 0
	void scoreKill(MatchContext& match, PartyPlayer& killer);     // + KillPoints

	// Radar phase setting (Arena, Team Clash): from the second half of the round every living player's overhead
	// marker is shown (0x1B).
	class RadarPhase
	{
	public:
		void reset(MatchContext& match);
		void tick(MatchContext& match, bool enabled);

	private:
		bool m_active = false;
	};
}

#endif
