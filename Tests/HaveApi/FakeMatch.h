/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#ifndef TESTS_HAVE_API_FAKE_MATCH_H
#define TESTS_HAVE_API_FAKE_MATCH_H

#include <algorithm>
#include <array>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include "HaveApi/Modes/PartyMode.h"

namespace Tests
{
	// Records what a party mode asks the match to send.
	class FakeMatch final : public Main::HaveApi::MatchContext
	{
	public:
		FakeMatch(Common::HaveApi::PartyModeId mode, std::size_t playerCount, Common::HaveApi::SettingValues values)
		{
			m_settings = { static_cast<std::int32_t>(mode), values };
			for (std::size_t i = 0; i < playerCount; ++i)
			{
				Main::HaveApi::PartyPlayer player;
				player.playerId = static_cast<std::int32_t>(100 + i);
				player.name = "P" + std::to_string(i);
				player.loaded = true;
				m_players.push_back(player);
			}
		}

		std::span<Main::HaveApi::PartyPlayer> players() override { return m_players; }
		const Common::HaveApi::PartySettingsPayload& settings() const override { return m_settings; }
		const Main::HaveApi::PartyRules& rules() const override { return m_rules; }
		std::chrono::seconds roundElapsed() const override { return elapsed; }
		std::uint32_t roundSeconds() const override { return seconds; }
		void updatePlayer(const Main::HaveApi::PartyPlayer& player) override { updates.emplace_back(player.playerId, player.team); }
		void updatePoints(const Main::HaveApi::PartyPlayer& player) override { points.emplace_back(player.playerId, player.points); }
		void restoreHealth(const Main::HaveApi::PartyPlayer& player) override { healed.push_back(player.playerId); }
		void setRadar(bool active) override { radar.push_back(active); }
		void setMarked(std::int32_t first, std::int32_t second) override { marked.emplace_back(first, second); }
		void controlLava(const Common::HaveApi::LavaCommand& command, std::function<void(bool)> done = {}) override
		{
			lavaCommands.push_back(command);
			if (done)
			{
				done(lavaWorks); // the CastServer's answer, at once
			}
		}
		void setLavaPhase(bool hot, bool sleeping) override { lavaPhases.emplace_back(hot, sleeping); }
		std::uint32_t mapId() const override { return map; }
		void showZone(bool visible, float x, float y, std::int32_t state) override { zones.push_back({ visible, x, y, state }); }
		void setZoneScore(std::int32_t red, std::int32_t blue, std::int32_t limit) override { scores.push_back({ red, blue, limit }); }
		bool placeZone(bool active, float x, float y, float, float) override { placed.push_back({ active, x, y, 0 }); return true; }
		std::vector<std::int32_t> playersInZone() override { return inZone; }
		std::span<const Main::HaveApi::SpawnPoint> spawnPoints() const override { return spawns; }
		void setChaser(std::int32_t playerId, std::span<const std::int8_t> states) override
		{
			chaser = playerId;
			targetStates.assign(states.begin(), states.end());
		}
		void announce(Common::HaveApi::Announcement event, std::string_view text) override { announcements.emplace_back(event, std::string{ text }); }
		void announceToTeam(Common::HaveApi::Announcement event, std::int32_t team) override { teamAnnouncements.emplace_back(event, team); }
		void announceToPlayer(Common::HaveApi::Announcement event, const Main::HaveApi::PartyPlayer& player, std::string_view text = {}) override
		{
			playerAnnouncements.emplace_back(event, player.playerId);
			playerAnnouncementTexts.emplace_back(text);
		}
		std::vector<std::string> playerAnnouncementTexts;
		void requestPositions(std::vector<std::int32_t> playerIds, std::function<void(const Positions&)> done) override
		{
			Positions known;
			for (const std::int32_t id : playerIds)
			{
				if (const auto found = positions.find(id); found != positions.end())
				{
					known[id] = found->second;
				}
			}
			done(known); // the CastServer's answers, at once
		}
		void killPlayer(const Main::HaveApi::PartyPlayer& player) override { killed.push_back(player.playerId); }
		std::vector<std::pair<Common::HaveApi::Announcement, std::int32_t>> playerAnnouncements;
		std::map<std::int32_t, std::array<float, 3>> positions;
		std::vector<std::int32_t> killed;
		void showHqProgress(const Common::HaveApi::HqProgressPayload& value) override { progress.push_back(value); }
		void setRespawnBlockedTeam(std::int32_t team) override { blockedTeams.push_back(team); }
		void setModifiers(const Main::HaveApi::PartyPlayer& player, const Common::HaveApi::Modifiers& value, std::int32_t hp = 0) override { modifiers[player.playerId] = value; health[player.playerId] = hp; }
		std::map<std::int32_t, std::int32_t> health;
		void setPropState(std::int32_t playerId, std::int32_t model, bool locked, const Common::HaveApi::PropWall& = {}) override { propStates[playerId] = { model, locked }; }
		void propEvent(const Common::HaveApi::PropEventPayload& event) override { propEvents.push_back(event); }
		void sendPropPhase(const Main::HaveApi::PartyPlayer& player, const Common::HaveApi::PropPhasePayload& phase) override { propPhases[player.playerId] = phase; }
		void setTeamRounds(std::int32_t red, std::int32_t blue, std::int32_t rounds) override { teamRounds.push_back({ red, blue, rounds }); }
		bool placeSquareZone(std::size_t index, bool active, float x, float y, float halfSize, float zMin, float zMax, float) override
		{
			squares.push_back({ index, active, x, y, halfSize, zMin, zMax });
			return true;
		}
		std::vector<std::int32_t> playersInZone(std::size_t index) override { return index < inZones.size() ? inZones[index] : std::vector<std::int32_t>{}; }
		void showDominationZones(const Common::HaveApi::DominationZonesPayload& value) override { dominationZones.push_back(value); }
		void showTeamBases(const Common::HaveApi::TeamBasesPayload& value) override { teamBases.push_back(value); }
		bool changeMap(std::uint32_t mapId) override { changedMaps.push_back(mapId); map = mapId; return true; }
		void spectate(const Main::HaveApi::PartyPlayer& spectator, std::int32_t preferred) override { spectated.emplace_back(spectator.playerId, preferred); }
		void showPreparePanel(std::int32_t panelSeconds) override { preparePanels.push_back(panelSeconds); }
		std::int32_t maxHealth() const override { return roomHealth; }
		void setStorm(const Common::HaveApi::BattleZonePayload& zone) override { storms.push_back(zone); }
		void clearStorm() override { ++stormClears; }
		std::vector<std::int32_t> playersOutsideStorm() const override { return outside; }
		bool wasOutsideStorm(std::int32_t playerId, std::chrono::milliseconds) const override
		{
			return std::ranges::find(outside, playerId) != outside.end() || std::ranges::find(recentlyOutside, playerId) != recentlyOutside.end();
		}
		void sendStormDamage(std::span<const Common::HaveApi::StormHit> hits) override { stormDamage.emplace_back(hits.begin(), hits.end()); }
		void statusText(const Main::HaveApi::PartyPlayer& player, std::string_view text) override { texts.emplace_back(player.playerId, std::string{ text }); }
		void playSound(const Main::HaveApi::PartyPlayer& player, std::string_view sound) override { sounds.emplace_back(player.playerId, std::string{ sound }); }
		void showSpawnAreas(const Common::HaveApi::BattleSpawnsPayload& areas) override { spawnAreas.push_back(areas); }
		std::vector<Common::HaveApi::BattleSpawnsPayload> spawnAreas;
		std::uint32_t seconds = 120;
		std::int32_t roomHealth = 2000;
		std::vector<Common::HaveApi::BattleZonePayload> storms;
		std::int32_t stormClears = 0;
		std::vector<std::int32_t> outside;
		std::vector<std::int32_t> recentlyOutside;
		std::vector<std::vector<Common::HaveApi::StormHit>> stormDamage;
		std::vector<std::pair<std::int32_t, std::string>> texts;
		std::vector<std::pair<std::int32_t, std::string>> sounds;
		std::vector<std::pair<std::int32_t, std::int32_t>> spectated;
		std::vector<std::int32_t> preparePanels;
		struct Square { std::size_t index; bool active; float x, y, halfSize, zMin, zMax; };
		std::vector<Square> squares;
		std::array<std::vector<std::int32_t>, 3> inZones; // player ids CastServer reports in each Domination zone
		std::vector<Common::HaveApi::DominationZonesPayload> dominationZones;
		std::vector<Common::HaveApi::TeamBasesPayload> teamBases;
		std::vector<std::uint32_t> changedMaps;

		Main::HaveApi::PartyPlayer& player(std::size_t index) { return m_players[index]; }
		void addPlayer(Main::HaveApi::PartyPlayer player) { m_players.push_back(std::move(player)); }

		std::chrono::seconds elapsed{ 0 };
		std::vector<std::pair<std::int32_t, std::int32_t>> updates;
		std::vector<std::pair<std::int32_t, std::int32_t>> points;
		std::vector<std::int32_t> healed;
		std::vector<bool> radar;
		std::vector<std::pair<std::int32_t, std::int32_t>> marked;
		std::int32_t chaser = 0;
		std::vector<Common::HaveApi::LavaCommand> lavaCommands;
		std::vector<std::pair<bool, bool>> lavaPhases;
		bool lavaWorks = true;
		struct Zone { bool visible; float x, y; std::int32_t state; };
		struct Score { std::int32_t red, blue, limit; };
		std::uint32_t map = 6;
		std::vector<Zone> zones;
		std::vector<Zone> placed;
		std::vector<Score> scores;
		std::vector<Score> teamRounds; // red wins, blue wins, rounds
		std::vector<std::int32_t> inZone; // player ids CastServer reports in the zone
		std::vector<Main::HaveApi::SpawnPoint> spawns; // where the clients respawned players
		std::vector<std::int8_t> targetStates;
		std::vector<std::pair<Common::HaveApi::Announcement, std::string>> announcements;
		std::vector<std::pair<Common::HaveApi::Announcement, std::int32_t>> teamAnnouncements;
		std::vector<Common::HaveApi::HqProgressPayload> progress;
		std::vector<std::int32_t> blockedTeams;
		std::map<std::int32_t, Common::HaveApi::Modifiers> modifiers;
		std::map<std::int32_t, std::pair<std::int32_t, bool>> propStates;
		std::vector<Common::HaveApi::PropEventPayload> propEvents;
		std::map<std::int32_t, Common::HaveApi::PropPhasePayload> propPhases;

	private:
		std::vector<Main::HaveApi::PartyPlayer> m_players;
		Common::HaveApi::PartySettingsPayload m_settings{};
		Main::HaveApi::PartyRules m_rules{};
	};
}

#endif
