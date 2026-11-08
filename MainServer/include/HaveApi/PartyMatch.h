/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#ifndef MAIN_HAVE_API_PARTY_MATCH_H
#define MAIN_HAVE_API_PARTY_MATCH_H

#include <array>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>
#include <asio.hpp>
#include "HaveApi/CombatEvent.h"
#include "HaveApi/Messages.h"
#include "HaveApi/Storm.h"
#include "HaveApi/Zone.h"
#include "HaveApi/MatchLog.h"
#include "HaveApi/RoomState.h"
#include "HaveApi/Modes/PartyMode.h"
#include "Network/Packet.h"
#include "Structures/Match/EndScoreboard.h"

namespace Main { class MainServer; }
namespace Main::Classes { class Room; class RoomsManager; }
namespace Main::Network { class Session; }

namespace Main::HaveApi
{
	// A party battle in a SquareMode room, from the host's Start to the end of the match. The match owns the timing
	// (rounds, result screen, match end); the party mode (PartyMode) owns the rules.
	// Start sequence (evidence: DLL hooks #59 and the battle dialog):
	//  - the DLL creates its party-mode object on 0x22 only while its player is in the battle scene, destroys it
	//    while the player is back at state <= 9, and the battle dialog uses it without a null check;
	//  - so every player gets 0x22 every 500 ms until its client reports "loaded" (order 107, extra 41), then the
	//    battle state: roster 0x0E, round state 0x10 and the first-spawn one-shot 0x11.
	// End: SquareMode has no client match timer, so the host never ends the match by itself. Observed in game: when the
	// host client receives a match-end packet (order 254, mission 0) it answers with its own, with the real per-player
	// statistics, and the normal match end runs (Handlers::handleMatchEnding: echo to the room, rewards, Room::endMatch).
	// So the server only asks the host (requestHostMatchEnd); if the host does not answer (e.g. it disconnected) the
	// server ends the match itself with its own counters (endGameMatch).
	class PartyMatch final : public MatchContext, public std::enable_shared_from_this<PartyMatch>
	{
	public:
		PartyMatch(Main::MainServer& mainServer, Main::Classes::RoomsManager& roomsManager, std::uint16_t roomNumber,
			const Common::HaveApi::PartySettingsPayload& settings);

		void start(const Main::Classes::Room& room);
		void onPlayerJoined(const Main::Network::Session& session);  // late joiner (Start pressed while the match runs)
		void onPlayerLoaded(Main::Network::Session& session);        // order 107, extra 41
		void onDeath(const Main::Network::Session& victim, const Main::Network::Session* attacker, Common::HaveApi::DeathCause cause);
		void onRespawn(const Main::Network::Session& player, std::optional<SpawnPoint> at); // at: where the client respawned it
		void onSparedHit(const Main::Network::Session& victim); // Lava: left at 1 hit point -> full health again
		void onZoneOccupants(const Common::HaveApi::ZoneAnswer& occupants); // Hardpoint: CastServer's change
		void onStormOutside(const Common::HaveApi::StormAnswer& outside);
		void onStormLethal(const Main::Network::Session& sender, std::int32_t playerId); // 0x3C
		void onPropAction(const Main::Network::Session& player, const Common::HaveApi::PropActionPayload& action); // 0x2D
		void onHit(const Main::Network::Session& victim, const Main::Network::Session& attacker, Common::HaveApi::DeathCause cause);
		void onPlayerLeft(std::uint32_t sessionId, std::string_view how); // left the match or the room, or disconnected
		void finishMatch();                                          // winner, result screen, then the match end
		void stop();
		// Every player of the match loads `mapId` (no result screen, no room): the room start packet again
		// (order 107 extra 38, option = map) and CastServer told the map. The client's start handler (0xAE7BE0) sets the
		// map and puts each player back to loading (CMod +0x20 = 0xE0F1C0: state 10, CRoom +0x30 = 0x549600 ->
		// game state 23 ModLoading, which replaces the world when the map differs, 0x563840 step 2); then the usual
		// "loaded" (107 / 41) brings everybody back in.
		bool reloadMap(std::uint32_t mapId);
		[[nodiscard]] bool acceptsLateJoiners() const { return m_mode->acceptsLateJoiners(); }
		[[nodiscard]] bool isParticipant(const Main::Network::Session& session) const;
		[[nodiscard]] std::string_view phaseName() const noexcept;
		void log(std::string_view event) const { m_log.write(event); }

		// MatchContext
		[[nodiscard]] std::span<PartyPlayer> players() override { return m_players; }
		[[nodiscard]] const Common::HaveApi::PartySettingsPayload& settings() const override { return m_settings; }
		[[nodiscard]] const PartyRules& rules() const override { return m_rules; }
		[[nodiscard]] std::chrono::seconds roundElapsed() const override;
		[[nodiscard]] std::uint32_t roundSeconds() const override;
		void updatePlayer(const PartyPlayer& player) override;
		void updatePoints(const PartyPlayer& player) override;
		void restoreHealth(const PartyPlayer& player) override;
		void setRadar(bool active) override;
		void setMarked(std::int32_t first, std::int32_t second) override;
		void setChaser(std::int32_t playerId, std::span<const std::int8_t> targetStates) override;
		void controlLava(const Common::HaveApi::LavaCommand& command, std::function<void(bool)> done = {}) override;
		void setLavaPhase(bool hot, bool sleeping) override;
		[[nodiscard]] std::uint32_t mapId() const override;
		void showZone(bool visible, float x, float y, std::int32_t state) override;
		void setZoneScore(std::int32_t red, std::int32_t blue, std::int32_t limit) override;
		bool placeZone(bool active, float x, float y, float radius, float keepOut) override; // zone 0, a circle
		[[nodiscard]] std::vector<std::int32_t> playersInZone() override;
		[[nodiscard]] std::span<const SpawnPoint> spawnPoints() const override { return m_spawnPoints; }
		void announce(Common::HaveApi::Announcement event, std::string_view text = {}) override;
		void announceToTeam(Common::HaveApi::Announcement event, std::int32_t team) override;
		void showHqProgress(const Common::HaveApi::HqProgressPayload& progress) override;
		void setRespawnBlockedTeam(std::int32_t team) override;
		void announceToPlayer(Common::HaveApi::Announcement event, const PartyPlayer& player, std::string_view text = {}) override;
		void requestPositions(std::vector<std::int32_t> playerIds, std::function<void(const Positions&)> done) override;
		void killPlayer(const PartyPlayer& player) override;
		void setModifiers(const PartyPlayer& player, const Common::HaveApi::Modifiers& modifiers, std::int32_t health = 0) override;
		void setPropState(std::int32_t playerId, std::int32_t model, bool locked, const Common::HaveApi::PropWall& wall = {}) override;
		void propEvent(const Common::HaveApi::PropEventPayload& event) override;
		void sendPropPhase(const PartyPlayer& player, const Common::HaveApi::PropPhasePayload& phase) override;
		void setTeamRounds(std::int32_t red, std::int32_t blue, std::int32_t rounds) override;
		bool placeSquareZone(std::size_t index, bool active, float x, float y, float halfSize, float zMin, float zMax,
			float keepOut) override;
		[[nodiscard]] std::vector<std::int32_t> playersInZone(std::size_t index) override;
		void showDominationZones(const Common::HaveApi::DominationZonesPayload& zones) override;
		void showTeamBases(const Common::HaveApi::TeamBasesPayload& bases) override;
		bool changeMap(std::uint32_t mapId) override { return reloadMap(mapId); }
		void spectate(const PartyPlayer& spectator, std::int32_t preferredPlayerId) override;
		void showPreparePanel(std::int32_t seconds) override;
		[[nodiscard]] std::chrono::milliseconds roundElapsedMs() const override;
		[[nodiscard]] std::int32_t maxHealth() const override;
		void setStorm(const Common::HaveApi::BattleZonePayload& zone) override;
		void clearStorm() override;
		[[nodiscard]] std::vector<std::int32_t> playersOutsideStorm() const override { return m_stormOutside; }
		[[nodiscard]] bool wasOutsideStorm(std::int32_t playerId, std::chrono::milliseconds within) const override;
		void sendStormDamage(std::span<const Common::HaveApi::StormHit> hits) override;
		void statusText(const PartyPlayer& player, std::string_view text) override;
		void playSound(const PartyPlayer& player, std::string_view sound) override;
		void showSpawnAreas(const Common::HaveApi::BattleSpawnsPayload& areas) override;

	private:
		enum class Phase { Running, RoundResult, MatchResult, Ended };

		[[nodiscard]] PartyPlayer* playerOf(const Main::Network::Session& session);
		[[nodiscard]] Main::Classes::Room* room() const;
		void beginRound();
		void endRound(RoundEndReason reason);
		void scheduleTick();
		void tick();
		void after(std::chrono::seconds delay, void (PartyMatch::*next)());
		void prepareNextRoundOrFinish();
		void loadNextMap();
		bool applyLava(const Common::HaveApi::LavaCommand& command, const std::optional<Common::HaveApi::LavaState>& state); // Elimination Shuffle: every player to the next map of the pool
		void sendTeamRounds(std::int32_t round);  // 0x36 (kept for players loading in)
		bool sendZoneCommand(const Common::HaveApi::ZoneCommand& command); // CastServer zone command (its index)
		void startNextRound();
		void requestHostMatchEnd();
		void endGameMatch();
		[[nodiscard]] Common::Network::Packet matchEndPacket(const Main::Classes::Room& room, std::vector<Main::Structures::ClientEndingMatch>& scores) const;
		void scheduleModeCreation();
		void sendModeCreation();
		void sendBattleState(Main::Network::Session& session);
		void sendRoundState(bool revive, bool fullHealth = false) const;
		[[nodiscard]] std::vector<std::uint8_t> roundStateMessage(bool revive, bool fullHealth = false) const;
		void sendToLoaded(const std::vector<std::uint8_t>& message) const;
		void sendToPartyPlayer(const PartyPlayer& player, const std::vector<std::uint8_t>& message) const;
		void sendStormCommand(Common::HaveApi::StormAction action);
		[[nodiscard]] Common::HaveApi::BattleZonePayload stormNow() const;
		void sendRespawnOnce(std::uint32_t sessionId);
		void sendMatchInfo(bool running) const; // CastServer: the game and round of its mode log lines
		void logLavaAnswer(const Common::HaveApi::LavaCommand& command, const std::optional<Common::HaveApi::LavaState>& state) const;
		void runDueActions();
		void followLivingPlayer(const PartyPlayer& spectator, std::int32_t preferredPlayerId = 0);
		void recordPlacements();
		[[nodiscard]] std::vector<Common::HaveApi::RosterEntry> roster() const;
		[[nodiscard]] bool respawnAllowed() const;
		[[nodiscard]] bool respawnAllowedFor(const PartyPlayer& player) const; // + the blocked team (Headquarters)
		[[nodiscard]] std::vector<std::uint8_t> rosterFor(const PartyPlayer& player) const; // 0x0E with its respawn flag
		[[nodiscard]] std::string banner() const;

		Main::MainServer& m_mainServer;
		asio::steady_timer m_modeTimer;
		asio::steady_timer m_tickTimer;
		asio::steady_timer m_phaseTimer;
		Main::Classes::RoomsManager& m_roomsManager;
		std::uint16_t m_roomNumber;
		Common::HaveApi::PartySettingsPayload m_settings;
		PartyRules m_rules;
		std::unique_ptr<PartyMode> m_mode;
		std::vector<PartyPlayer> m_players;
		// Delayed messages to one player, run by the 1 s tick.
		struct PendingAction
		{
			enum class Kind { ClearRespawnOnce, DeathCamera, FollowLivingPlayer };
			Kind kind{};
			std::uint32_t sessionId{};
			std::chrono::steady_clock::time_point at{};
			std::int32_t preferredPlayerId{};
		};
		std::vector<PendingAction> m_pendingActions;
		bool m_radarActive = false;
		std::pair<std::int32_t, std::int32_t> m_marked{};
		std::vector<std::vector<std::uint8_t>> m_chaserState; // last 0x1A and 0x19, for players loading in
		std::optional<Common::HaveApi::LavaState> m_lava;     // last lava sent to the clients (while shown)
		std::chrono::steady_clock::time_point m_lavaAt{};
		std::vector<std::uint8_t> m_lavaPhase;
		bool m_playersProtected = false;
		std::map<std::int32_t, bool> m_disarmed; // player id -> CastServer drops its damage (Prop Hunt, 0x14 canDamageOthers 0)
		std::vector<std::uint8_t> m_zoneMessage;  // last 0x27 / 0x28, for players loading in
		std::vector<std::uint8_t> m_scoreMessage;
		std::vector<std::uint8_t> m_hqProgressMessage; // last 0x29
		std::map<std::int32_t, std::vector<std::uint8_t>> m_modifierMessages; // player id -> last 0x14 (Prop Hunt)
		std::map<std::int32_t, std::vector<std::uint8_t>> m_propStates;       // player id -> last 0x2A
		std::map<std::uint32_t, std::vector<std::uint8_t>> m_propPhases;      // session -> last 0x2C
		std::int32_t m_respawnBlockedTeam = 0;
		std::array<bool, Common::HaveApi::maxZones> m_zonePlaced{};
		std::array<std::vector<std::int32_t>, Common::HaveApi::maxZones> m_zoneOccupants; // player ids, from CastServer
		std::vector<std::uint8_t> m_dominationZonesMessage;                     // last 0x37, for players loading in
		std::vector<std::uint8_t> m_teamBasesMessage;                           // last 0x38, for players loading in
		std::vector<std::uint8_t> m_spawnAreasMessage;                          // last 0x3D, for players loading in
		std::vector<SpawnPoint> m_spawnPoints;     // where the clients respawned players in this match
		std::optional<Common::HaveApi::BattleZonePayload> m_storm; // last 0x3A, for players loading in
		std::chrono::steady_clock::time_point m_stormAt{};
		bool m_stormPlaced = false;
		std::vector<std::int32_t> m_stormOutside; // player ids, from CastServer
		std::map<std::int32_t, std::chrono::steady_clock::time_point> m_lastOutside;
		std::unordered_map<std::uint32_t, RoomState::PartyResult> m_results;
		std::uint32_t m_round = 1;
		// Elimination Shuffle: the maps not played yet (Elimination and SquareMode maps, shuffled), the round's map
		// loading (the next round starts when every player loaded it, at the latest at m_loadDeadline)
		std::vector<std::uint32_t> m_mapPool;
		bool m_waitingForLoad = false;
		std::chrono::steady_clock::time_point m_loadDeadline{};
		std::array<std::int32_t, 3> m_teamRounds{}; // red wins, blue wins, rounds (last setTeamRounds)
		std::vector<std::uint8_t> m_teamRoundsMessage;
		MatchLog m_log;
		std::chrono::steady_clock::time_point m_roundStart{};
		Phase m_phase = Phase::Running;
		bool m_started = false;
		bool m_stopped = false;
	};

	// Called when the host starts the match: a SquareMode room with a party game starts its party match.
	void startPartyMatch(Main::MainServer& mainServer, Main::Classes::RoomsManager& roomsManager, Main::Classes::Room& room);

	// A match-end packet (order 254) from a client of a party room, in the mode log (with the party match's phase, if any)
	void logClientMatchEnd(const Main::Classes::Room& room, const Main::Network::Session& sender, std::string_view what);
}

#endif
