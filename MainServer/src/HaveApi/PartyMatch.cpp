/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#include "HaveApi/PartyMatch.h"
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstring>
#include <format>
#include <random>
#include "ConstantDatabase/CdbSingleton.h"
#include "ConstantDatabase/Structures/CdbMapInfo.h"
#include "Handlers/Room/EliminationNextRoundHandler.h"
#include "HaveApi/BattleMessages.h"
#include "HaveApi/Catalog.h"
#include "HaveApi/Zone.h"
#include "HaveApi/RoomState.h"
#include "HaveApi/MessageWriter.h"
#include "HaveApi/ModeLog.h"
#include "HaveApi/PacketLog.h"
#include "HaveApi/Sender.h"
#include "MainServer.h"
#include "Managers/RoomsManager.h"
#include "Network/Sessions/MainSession.h"
#include "Rooms/Room.h"
#include "Structures/ClientData/Structures.h"
#include "Structures/Match/EndScoreboard.h"
#include "Utils/Logger.h"
#include "Utils/SetupParser.h"
#include "Detail/IpcUtils.h" // after the headers above: it needs their enums

namespace Main::HaveApi
{
	namespace CH = Common::HaveApi;

	namespace
	{
		constexpr auto modeCreationRetry = std::chrono::milliseconds{ 500 };
		constexpr auto tickInterval = std::chrono::seconds{ 1 };
		constexpr auto resultScreenTime = std::chrono::seconds{ 5 };   // design: result banner of a round / the match
		constexpr auto hostAnswerTimeout = std::chrono::seconds{ 5 };
		constexpr auto prepareTime = std::chrono::seconds{ 5 };        // PREPARE panel before the next round
		constexpr auto respawnOnceLifetime = std::chrono::seconds{ 3 }; // then an unused one-shot respawn is cleared
		constexpr auto deathCameraDelay = std::chrono::seconds{ 2 };    // eliminated late joiner: loaded -> death packet
		constexpr auto followDelay = std::chrono::seconds{ 1 };         // death packet -> 0x12 follow a living player
		constexpr auto spectateDelay = std::chrono::seconds{ 2 };
		constexpr std::int32_t announcementSeconds = 5;
		constexpr auto roundStartAnnouncementWindow = std::chrono::seconds{ 10 }; // players loading in still see it
		constexpr std::uint32_t secondsPerRoundTimeUnit = 6;
		constexpr auto mapLoadTimeout = std::chrono::seconds{ 40 };    // Elimination Shuffle: the slowest client's loading

		// Elimination Shuffle's maps: the maps of the official Elimination that the party room's SquareMode accepts too
		// (the client checks the map against the room's mode when it loads it, 0xAE7BE0), shuffled; `current` left out
		[[nodiscard]] std::vector<std::uint32_t> shuffledMapPool(std::uint32_t current)
		{
			std::vector<std::uint32_t> pool;
			for (const auto& [id, map] : Common::ConstantDatabase::CdbSingleton<Common::ConstantDatabase::CdbMapInfo>::getInstance().getEntries())
			{
				if (map.mi_mod_sab && map.mi_mod_nod && map.mi_id != current)
				{
					pool.push_back(map.mi_id);
				}
			}
			std::ranges::sort(pool);
			std::ranges::shuffle(pool, std::mt19937{ std::random_device{}() });
			return pool;
		}           // round time values are tenths of a minute (DLL labels)
		constexpr std::uint32_t observerTeam = 4;
		constexpr std::uint16_t matchEndOrder = 254;
		// The emulator's anti-farm rule of the normal match end (Handlers::isFarmingMatch): no rewards under 80 s.
		constexpr std::uint64_t minimumRewardedMatchMs = 80 * 1000;

		[[nodiscard]] std::shared_ptr<Main::Network::Session> sessionOf(const Main::Classes::Room& room, std::uint32_t sessionId)
		{
			for (const auto& [info, session] : room.getAllPlayersWithSessions())
			{
				if (session->getId() == sessionId)
				{
					return session;
				}
			}
			return nullptr;
		}

		[[nodiscard]] PartyRules configuredRules()
		{
			const auto& setup = Common::Utils::SetupParser::getInstance().getHaveApiSetup();
			return { setup.roundsPerMatch, setup.killPoints, setup.lastStandingPoints, setup.markedTargetPoints,
				setup.lavaSpeedScale, setup.lavaWallCloseSeconds };
		}

		[[nodiscard]] std::uint8_t counter(std::int32_t value)
		{
			return static_cast<std::uint8_t>(std::clamp(value, 0, 255));
		}
	}

	PartyMatch::PartyMatch(Main::MainServer& mainServer, Main::Classes::RoomsManager& roomsManager, std::uint16_t roomNumber,
		const CH::PartySettingsPayload& settings)
		: m_mainServer{ mainServer }
		, m_modeTimer{ mainServer.getIoContext() }
		, m_tickTimer{ mainServer.getIoContext() }
		, m_phaseTimer{ mainServer.getIoContext() }
		, m_roomsManager{ roomsManager }
		, m_roomNumber{ roomNumber }
		, m_settings{ settings }
		, m_rules{ configuredRules() }
		, m_mode{ createPartyMode(static_cast<CH::PartyModeId>(settings.modeId)) }
		, m_log{ roomNumber, settings.modeId }
	{
	}

	std::string_view PartyMatch::phaseName() const noexcept
	{
		switch (m_phase)
		{
		case Phase::Running: return "running";
		case Phase::RoundResult: return "round result";
		case Phase::MatchResult: return "match result";
		case Phase::Ended: return "ended";
		}
		return "unknown";
	}

	void PartyMatch::sendMatchInfo(bool running) const
	{
		const auto anyPlayer = std::ranges::find_if(m_players, &PartyPlayer::loaded);
		const auto target = anyPlayer != m_players.end() ? anyPlayer : m_players.begin();
		if (target == m_players.end())
		{
			return;
		}
		CH::PartyMatchInfo info{};
		info.modeId = m_settings.modeId;
		info.round = m_round;
		info.roomNumber = m_roomNumber;
		info.running = running ? 1 : 0;
		Main::Ipc::M2C_sendPartyMatchInfo(target->sessionId, info);
	}

	Main::Classes::Room* PartyMatch::room() const
	{
		return m_roomsManager.getRoomByNumber(m_roomNumber);
	}

	PartyPlayer* PartyMatch::playerOf(const Main::Network::Session& session)
	{
		const auto found = std::ranges::find(m_players, static_cast<std::uint32_t>(session.getId()), &PartyPlayer::sessionId);
		return found != m_players.end() ? &*found : nullptr;
	}

	bool PartyMatch::isParticipant(const Main::Network::Session& session) const
	{
		return std::ranges::find(m_players, static_cast<std::uint32_t>(session.getId()), &PartyPlayer::sessionId) != m_players.end();
	}

	// ---- start ----

	void PartyMatch::start(const Main::Classes::Room& room)
	{
		for (const auto& [info, session] : room.getAllPlayersWithSessions())
		{
			if (info.state == Common::Enums::STATE_NORMAL)
			{
				onPlayerJoined(*session); // the host and the ready players (Room::startMatch); the others may join later
			}
		}
		m_mode->onMatchStart(*this);
		if (m_mode->changesMapEveryRound())
		{
			Main::Classes::Room* current = this->room();
			m_mapPool = shuffledMapPool(current != nullptr ? current->getActualMap() : 0);
		}
		Utils::Logger::log("[HaveApi] party match started in room " + std::to_string(m_roomNumber) + ", mode " +
			std::to_string(m_settings.modeId) + ", " + std::to_string(m_players.size()) + " players");
		std::string host = "unknown";
		for (const auto& [info, session] : room.getAllPlayersWithSessions())
		{
			if (room.isHost(info.uniqueId))
			{
				host = std::format("{} (session {})", playerName(*session), session->getId());
			}
		}
		const auto& options = room.haveApi().options();
		m_log.write(std::format("match start: game {}, map {}, settings [{}, {}, {}, {}, {}, {}], round time {} s, rounds {}, host {}, "
			"room options (max health {}, weapon balance {}, option {}), rules (kill {}, last standing {}, marked {}, lava speed scale {:.2f}, "
			"lava walls {:.1f} s)", m_settings.modeId, mapId(), m_settings.values[0], m_settings.values[1], m_settings.values[2],
			m_settings.values[3], m_settings.values[4], m_settings.values[5], roundSeconds(), m_mode->roundsPerMatch(m_rules.roundsPerMatch), host,
			options.maxHealth, options.weaponBalance, options.optionEnabled, m_rules.killPoints, m_rules.lastStandingPoints, m_rules.markedTargetPoints,
			m_rules.lavaSpeedScale, m_rules.lavaWallCloseSeconds));
		m_log.write("match start players: " + MatchLog::describe(m_players));
		beginRound();
		m_started = true;
		sendModeCreation();
		scheduleModeCreation();
		scheduleTick();
	}

	void PartyMatch::onPlayerJoined(const Main::Network::Session& session)
	{
		const Main::Classes::Room* current = room();
		const auto team = current != nullptr ? current->getTeamForSession(static_cast<std::uint32_t>(session.getId())) : std::nullopt;
		if (!team || *team == observerTeam || playerOf(session) != nullptr)
		{
			return; // observers are not party players
		}
		if (m_started && !m_mode->acceptsLateJoiners())
		{
			m_log.write(std::format("late join refused (the game takes no late joiners): {} (session {})", playerName(session), session.getId()));
			return;
		}
		const auto& nickname = session.getAccountInfo().nickname;
		PartyPlayer player;
		player.sessionId = static_cast<std::uint32_t>(session.getId());
		player.playerId = CH::playerIdFromUniqueId(std::bit_cast<std::uint32_t>(session.getAccountInfo().uniqueId));
		player.name.assign(nickname, strnlen(nickname, sizeof(nickname)));
		if (m_started)
		{
			// late joiner: the mode picks its team; in modes without respawn it waits for the next round
			player.fallbackTeam = m_mode->teamForLateJoiner(*this);
			player.alive = m_mode->respawnAllowed();
			player.team = player.alive ? player.fallbackTeam : 0;
		}
		m_players.push_back(player);
		if (m_started)
		{
			// the zones placed so far know only the players of their placement: the late joiner counts in them too
			for (std::size_t index = 0; index < CH::maxZones; ++index)
			{
				if (m_zonePlaced[index])
				{
					CH::ZoneCommand command{};
					command.action = CH::ZoneAction::AddMembers;
					command.index = static_cast<std::uint8_t>(index);
					command.count = 1;
					command.members[0] = { player.sessionId, player.fallbackTeam };
					Main::Ipc::M2C_partyZone(m_players.front().sessionId, command, [](std::optional<CH::ZoneAnswer>) {});
				}
			}
			// a late joiner loads while the others play: it needs the same 0x22 repetition as everybody at the start,
			// otherwise its battle dialog opens without a party-mode object and the client crashes
			sendToLoaded(CH::encodePlayer(CH::MessageId::AddPlayer, player.playerId, player.team, player.fallbackTeam));
			m_log.write(std::format("late join ({}): {}", phaseName(), MatchLog::describe(player)));
			sendModeCreation();
			scheduleModeCreation();
		}
	}

	void PartyMatch::onPlayerLoaded(Main::Network::Session& session)
	{
		PartyPlayer* player = playerOf(session);
		if (player == nullptr || player->loaded || m_phase == Phase::Ended)
		{
			return;
		}
		player->loaded = true;
		m_log.write(std::format("loaded ({}, round {} s in): {}", phaseName(), roundElapsed().count(), MatchLog::describe(*player)));
		sendMatchInfo(true);
		sendToPlayer(session, CH::encodeEmpty(CH::MessageId::CreatePartyMode));
		sendBattleState(session);
	}

	bool PartyMatch::reloadMap(std::uint32_t mapId)
	{
		Main::Classes::Room* current = room();
		if (current == nullptr || m_phase == Phase::Ended)
		{
			return false;
		}
		std::shared_ptr<Main::Network::Session> host;
		for (const auto& [info, session] : current->getAllPlayersWithSessions())
		{
			if (current->isHost(info.uniqueId))
			{
				host = session;
			}
		}
		if (!host)
		{
			m_log.write(std::format("map change to {} failed: no host", mapId));
			return false;
		}
		const Main::Structures::UniqueId hostId = host->getAccountInfo().uniqueId;
		if (!Main::Ipc::M2C_sendMapId(hostId.session, mapId, current->getRoomSettings().mode))
		{
			m_log.write(std::format("map change to {} failed: no CastServer link", mapId));
			return false;
		}
		m_log.write(std::format("map change from {} to {} (host {}), every player loads again", current->getActualMap(), mapId, playerName(*host)));
		current->setMatchMap(mapId);
		for (PartyPlayer& player : m_players)
		{
			player.loaded = false;
		}
		m_spawnPoints.clear();
		Common::Network::Packet start;
		start.setTcpHeader(host->getId(), Common::Enums::NO_ENCRYPTION);
		start.setCommand(107, 0, 38, mapId);
		start.setData(reinterpret_cast<const std::uint8_t*>(&hostId), sizeof(hostId));
		current->broadcastToRoom(start);
		Utils::Logger::log("[HaveApi] room " + std::to_string(m_roomNumber) + ": map reload to " + std::to_string(mapId));
		sendModeCreation();
		scheduleModeCreation();
		return true;
	}

	void PartyMatch::sendModeCreation()
	{
		const Main::Classes::Room* current = room();
		if (current == nullptr)
		{
			stop();
			return;
		}
		for (const PartyPlayer& player : m_players)
		{
			if (player.loaded)
			{
				continue;
			}
			if (const auto session = sessionOf(*current, player.sessionId))
			{
				sendToPlayer(*session, CH::encodeEmpty(CH::MessageId::CreatePartyMode));
				if (!m_spawnAreasMessage.empty())
				{
					sendToPlayer(*session, m_spawnAreasMessage);
				}
			}
		}
	}

	void PartyMatch::scheduleModeCreation()
	{
		if (m_stopped || std::ranges::all_of(m_players, &PartyPlayer::loaded))
		{
			return;
		}
		m_modeTimer.expires_after(modeCreationRetry);
		m_modeTimer.async_wait([weak = weak_from_this()](const asio::error_code& error) {
			const auto self = weak.lock();
			if (error || !self || self->m_stopped)
			{
				return;
			}
			self->sendModeCreation();
			self->scheduleModeCreation();
		});
	}

	// ---- rounds ----

	std::uint32_t PartyMatch::roundSeconds() const
	{
		if (m_settings.modeId == static_cast<std::int32_t>(CH::PartyModeId::Hardpoint) ||
			m_settings.modeId == static_cast<std::int32_t>(CH::PartyModeId::Headquarters))
		{
			// time limit setting (Hardpoint 3rd, Headquarters 4th): 1 = Off (the points limit ends the match), else minutes
			const std::int32_t limit = m_settings.values[m_settings.modeId == static_cast<std::int32_t>(CH::PartyModeId::Hardpoint) ? 2 : 3];
			return limit <= 1 ? 0 : static_cast<std::uint32_t>(limit) * 60;
		}
		if (m_settings.modeId == static_cast<std::int32_t>(CH::PartyModeId::Domination))
		{
			// time limit setting (2nd), as Headquarters: 1 = Off (the points limit ends the match), else minutes
			const std::int32_t limit = m_settings.values[1];
			return limit <= 1 ? 0 : static_cast<std::uint32_t>(limit) * 60;
		}
		if (m_settings.modeId == static_cast<std::int32_t>(CH::PartyModeId::PropHunt))
		{
			return static_cast<std::uint32_t>(m_settings.values[1]) * 60; // round time setting, minutes (hide time included)
		}
		if (m_settings.modeId == static_cast<std::int32_t>(CH::PartyModeId::BattleRoyale))
		{
			return static_cast<std::uint32_t>(m_settings.values[0]) * 60;
		}
		if (m_settings.modeId == static_cast<std::int32_t>(CH::PartyModeId::LavaEscape))
		{
			// no round time setting: the lava ends the round; a time limit in case it cannot (design, configurable)
			return Common::Utils::SetupParser::getInstance().getHaveApiSetup().lavaRoundSeconds;
		}
		return static_cast<std::uint32_t>(m_settings.values[0]) * secondsPerRoundTimeUnit;
	}

	std::chrono::seconds PartyMatch::roundElapsed() const
	{
		return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - m_roundStart);
	}

	bool PartyMatch::respawnAllowed() const
	{
		return m_phase == Phase::Running ? m_mode->respawnAllowed() : true;
	}

	bool PartyMatch::respawnAllowedFor(const PartyPlayer& player) const
	{
		if (m_phase == Phase::Running && m_respawnBlockedTeam != 0 && player.fallbackTeam == m_respawnBlockedTeam)
		{
			return false;
		}
		return respawnAllowed();
	}

	std::vector<std::uint8_t> PartyMatch::rosterFor(const PartyPlayer& player) const
	{
		const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - m_roundStart);
		return CH::encodeRoster(respawnAllowedFor(player), static_cast<std::int32_t>(roundSeconds()),
			m_phase == Phase::Running ? static_cast<std::int32_t>(elapsed.count()) : 0, roster());
	}

	std::string PartyMatch::banner() const
	{
		if (!m_mode->showsRounds())
		{
			return {};
		}
		return m_phase == Phase::Running ? "Round " + std::to_string(m_round) : "Round Result";
	}

	void PartyMatch::beginRound()
	{
		m_phase = Phase::Running;
		m_roundStart = std::chrono::steady_clock::now();
		m_log.setRound(m_round);
		sendMatchInfo(true);
		m_log.write(std::format("round {} start on map {}, {} s", m_round, mapId(), roundSeconds()));
		m_mode->onRoundStart(*this);
		m_log.write("round start players (after the game's roles): " + MatchLog::describe(m_players));
	}

	void PartyMatch::scheduleTick()
	{
		m_tickTimer.expires_after(tickInterval);
		m_tickTimer.async_wait([weak = weak_from_this()](const asio::error_code& error) {
			if (const auto self = weak.lock(); self && !error && !self->m_stopped)
			{
				self->tick();
				self->scheduleTick();
			}
		});
	}

	void PartyMatch::tick()
	{
		runDueActions();
		if (m_waitingForLoad &&
			(std::ranges::all_of(m_players, &PartyPlayer::loaded) || std::chrono::steady_clock::now() >= m_loadDeadline))
		{
			startNextRound(); // every player is on the next map (or the slowest one gave up)
			return;
		}
		if (m_phase != Phase::Running)
		{
			return;
		}
		if (m_stormPlaced)
		{
			sendStormCommand(CH::StormAction::Query);
		}
		m_mode->onTick(*this);
		if (m_mode->isRoundOver(*this))
		{
			endRound(RoundEndReason::LastStanding);
		}
		else if (roundSeconds() != 0 && roundElapsed() >= std::chrono::seconds{ roundSeconds() })
		{
			endRound(RoundEndReason::TimeUp);
		}
	}

	void PartyMatch::after(std::chrono::seconds delay, void (PartyMatch::*next)())
	{
		m_phaseTimer.expires_after(delay);
		m_phaseTimer.async_wait([weak = weak_from_this(), next](const asio::error_code& error) {
			if (const auto self = weak.lock(); self && !error && !self->m_stopped)
			{
				((*self).*next)();
			}
		});
	}

	void PartyMatch::endRound(RoundEndReason reason)
	{
		m_phase = Phase::RoundResult;
		Utils::Logger::log("[HaveApi] room " + std::to_string(m_roomNumber) + ": round " + std::to_string(m_round) + " over (" +
			(reason == RoundEndReason::TimeUp ? "time up" : "last standing") + ")");
		m_log.write(std::format("round {} over ({}) after {} s, before the game's result: {}", m_round,
			reason == RoundEndReason::TimeUp ? "time up" : "last standing", roundElapsed().count(), MatchLog::describe(m_players)));
		m_mode->onRoundEnd(*this, reason);
		recordPlacements();
		m_log.write("round result: " + MatchLog::describe(m_players));
		m_log.writeHits(m_players);
		sendRoundState(false);
		after(resultScreenTime, &PartyMatch::prepareNextRoundOrFinish);
	}

	// After the round result: the last round ends the match; otherwise the players see the "PREPARE" panel (0x15)
	// for a few seconds before the next round starts.
	void PartyMatch::prepareNextRoundOrFinish()
	{
		if (m_round >= m_mode->roundsPerMatch(m_rules.roundsPerMatch) || m_mode->isMatchDecided(*this))
		{
			finishMatch();
			return;
		}
		if (m_mode->changesMapEveryRound())
		{
			loadNextMap(); // no PREPARE panel: the loading screen of the next map shows the round and the scores
			return;
		}
		sendToLoaded(CH::encodeValues(CH::MessageId::KillNotice, static_cast<std::int32_t>(prepareTime.count())));
		after(prepareTime, &PartyMatch::startNextRound);
	}

	void PartyMatch::loadNextMap()
	{
		Main::Classes::Room* current = room();
		if (m_mapPool.empty())
		{
			m_mapPool = shuffledMapPool(current != nullptr ? current->getActualMap() : 0); // more rounds than maps
		}
		const std::uint32_t next = m_mapPool.empty() ? 0 : m_mapPool.back();
		if (!m_mapPool.empty())
		{
			m_mapPool.pop_back();
		}
		sendTeamRounds(static_cast<std::int32_t>(m_round + 1)); // the loading screen shows the next round
		if (next == 0 || !reloadMap(next))
		{
			Utils::Logger::log("[HaveApi] room " + std::to_string(m_roomNumber) + ": map change failed, the round stays on this map");
			m_log.write(std::format("map change to {} failed, the next round stays on map {}", next, mapId()));
			after(prepareTime, &PartyMatch::startNextRound);
			return;
		}
		m_waitingForLoad = true;
		m_loadDeadline = std::chrono::steady_clock::now() + mapLoadTimeout;
	}

	void PartyMatch::sendTeamRounds(std::int32_t round)
	{
		CH::TeamRoundsPayload rounds{};
		rounds.round = round;
		rounds.redWins = m_teamRounds[0];
		rounds.blueWins = m_teamRounds[1];
		rounds.rounds = m_teamRounds[2];
		m_teamRoundsMessage = CH::encode(CH::MessageId::TeamRounds, rounds);
		sendToLoaded(m_teamRoundsMessage);
	}

	void PartyMatch::setTeamRounds(std::int32_t red, std::int32_t blue, std::int32_t rounds)
	{
		m_teamRounds = { red, blue, rounds };
		m_log.write(std::format("team rounds: red {} blue {} of {}", red, blue, rounds));
		sendTeamRounds(static_cast<std::int32_t>(m_round));
	}

	void PartyMatch::startNextRound()
	{
		if (m_waitingForLoad && !std::ranges::all_of(m_players, &PartyPlayer::loaded))
		{
			m_log.write("map load deadline reached, the next round starts without: " + MatchLog::describe(m_players));
		}
		m_waitingForLoad = false;
		std::vector<std::uint32_t> eliminated;
		for (const PartyPlayer& player : m_players)
		{
			if (!player.alive && player.loaded)
			{
				eliminated.push_back(player.sessionId);
			}
		}
		++m_round;
		beginRound();
		if (const Main::Classes::Room* current = room())
		{
			for (const PartyPlayer& player : m_players)
			{
				const auto session = player.loaded ? sessionOf(*current, player.sessionId) : nullptr;
				if (session)
				{
					sendToPlayer(*session, rosterFor(player));
				}
			}
		}
		sendRoundState(true, m_mode->healSurvivorsAtRoundStart(*this));
		if (const auto announcement = m_mode->roundStartAnnouncement(*this))
		{
			announce(announcement->first, announcement->second);
		}
		for (const std::uint32_t sessionId : eliminated)
		{
			sendRespawnOnce(sessionId); // the eliminated players come back; the survivors must not get a spare one
		}
	}

	// 0x11 sets a one-shot respawn in the DLL that stays pending until the player dies. A player who does not use it
	// at once (alive already) would respawn at its next death in a mode without respawn, so a roster (0x0E, which
	// clears the one-shot) follows a few seconds later.
	void PartyMatch::sendRespawnOnce(std::uint32_t sessionId)
	{
		const Main::Classes::Room* current = room();
		const auto session = current != nullptr ? sessionOf(*current, sessionId) : nullptr;
		const auto player = std::ranges::find(m_players, sessionId, &PartyPlayer::sessionId);
		if (!session)
		{
			m_log.write(std::format("one-shot respawn (0x11) not sent: session {} not in the room", sessionId));
			return;
		}
		sendToPlayer(*session, CH::encodeEmpty(CH::MessageId::RespawnOnce));
		m_log.write(std::format("one-shot respawn (0x11) sent ({}), cleared in {} s: {}", phaseName(), respawnOnceLifetime.count(),
			player != m_players.end() ? MatchLog::describe(*player) : std::format("session {} (not a party player)", sessionId)));
		m_pendingActions.push_back({ PendingAction::Kind::ClearRespawnOnce, sessionId, std::chrono::steady_clock::now() + respawnOnceLifetime });
	}

	void PartyMatch::runDueActions()
	{
		const auto now = std::chrono::steady_clock::now();
		std::vector<PendingAction> due;
		std::erase_if(m_pendingActions, [&](const PendingAction& action) {
			if (action.at > now)
			{
				return false;
			}
			due.push_back(action);
			return true;
		});
		const Main::Classes::Room* current = room();
		for (const PendingAction& action : due)
		{
			const auto session = current != nullptr ? sessionOf(*current, action.sessionId) : nullptr;
			const auto player = std::ranges::find(m_players, action.sessionId, &PartyPlayer::sessionId);
			if (!session || player == m_players.end())
			{
				m_log.write(std::format("delayed action {} for session {} dropped: no longer in the room or the match", static_cast<int>(action.kind),
					action.sessionId));
				continue;
			}
			switch (action.kind)
			{
			case PendingAction::Kind::ClearRespawnOnce:
				sendToPlayer(*session, rosterFor(*player));
				m_log.write(std::format("one-shot respawn cleared (roster 0x0E, respawn {}): {}", respawnAllowedFor(*player) ? "allowed" : "blocked",
					MatchLog::describe(*player)));
				break;
			case PendingAction::Kind::DeathCamera:
				// An eliminated late joiner stays where its client placed it at the end of loading and its camera follows
				// nobody. CastServer sends the normal death packet (order 264) to that client only: the other clients and
				// the host's statistics never see this death, and onDeath ignores it (the player is already eliminated).
				if (m_phase == Phase::Running && !player->alive && respawnAllowed() && !m_mode->keepsLateJoinerDead(*player))
				{
					m_log.write("death camera skipped, its team may respawn again: " + MatchLog::describe(*player));
					sendRespawnOnce(action.sessionId); // kept dead, but its team may respawn again meanwhile
				}
				else if (m_phase == Phase::Running && !player->alive)
				{
					Utils::Logger::log("[HaveApi] room " + std::to_string(m_roomNumber) + ": death camera for " + player->name);
					m_log.write("death camera (death packet to the player only): " + MatchLog::describe(*player));
					Main::Ipc::M2C_sendPartyKillPlayer(action.sessionId);
					m_pendingActions.push_back({ PendingAction::Kind::FollowLivingPlayer, action.sessionId, now + followDelay });
				}
				break;
			case PendingAction::Kind::FollowLivingPlayer:
				if (m_phase == Phase::Running && !player->alive)
				{
					followLivingPlayer(*player, action.preferredPlayerId);
				}
				break;
			}
		}
	}

	// 0x12: the dead player's camera follows a living player. After the death packet alone the camera stays still; 0x12
	// to a player that never spawned respawned it, so it is sent only after the death packet.
	void PartyMatch::followLivingPlayer(const PartyPlayer& spectator, std::int32_t preferredPlayerId)
	{
		const Main::Classes::Room* current = room();
		const auto session = current != nullptr ? sessionOf(*current, spectator.sessionId) : nullptr;
		auto followed = std::ranges::find_if(m_players, [&](const PartyPlayer& p) { return p.alive && p.loaded && p.playerId == preferredPlayerId; });
		if (followed == m_players.end())
		{
			followed = std::ranges::find_if(m_players, [](const PartyPlayer& p) { return p.alive && p.loaded; });
		}
		if (!session || followed == m_players.end())
		{
			m_log.write(std::format("spectate: {} follows nobody ({})", spectator.name, session ? "nobody alive and loaded" : "not in the room"));
			return;
		}
		Utils::Logger::log("[HaveApi] room " + std::to_string(m_roomNumber) + ": " + spectator.name + " follows " + followed->name +
			" (player id " + std::to_string(followed->playerId) + ")");
		m_log.write(std::format("spectate (0x12): {} follows {} (preferred id {})", MatchLog::describe(spectator), MatchLog::describe(*followed),
			preferredPlayerId));
		sendToPlayer(*session, CH::encodeValues(CH::MessageId::Spectate, followed->playerId, 0));
	}

	void PartyMatch::finishMatch()
	{
		if (m_phase == Phase::MatchResult || m_phase == Phase::Ended || m_stopped)
		{
			return;
		}
		m_phase = Phase::MatchResult;
		Utils::Logger::log("[HaveApi] room " + std::to_string(m_roomNumber) + ": match result");
		m_mode->onMatchEnd(*this);
		recordPlacements();
		m_log.writeHits(m_players);
		m_log.write("match result: " + MatchLog::describe(m_players));
		std::int32_t entrants = 0;
		for (const auto& [sessionId, result] : m_results)
		{
			entrants = (std::max)(entrants, result.placement);
		}
		for (auto& [sessionId, result] : m_results)
		{
			result.entrants = entrants;
		}
		if (Main::Classes::Room* current = room())
		{
			current->haveApi().setPartyResults(m_results);
		}
		sendRoundState(false);
		sendToLoaded(CH::encodeRoster(true, 0, 0, roster()));
		sendToLoaded(CH::encodeEmpty(CH::MessageId::RespawnOnce)); // whoever is dead comes back for the result screen
		after(resultScreenTime, &PartyMatch::requestHostMatchEnd);
	}

	// ---- combat ----

	void PartyMatch::onDeath(const Main::Network::Session& victim, const Main::Network::Session* attacker, CH::DeathCause cause)
	{
		PartyPlayer* dead = playerOf(victim);
		const PartyPlayer* by = attacker != nullptr ? playerOf(*attacker) : nullptr;
		const std::string killerText = by != nullptr ? MatchLog::describe(*by) : attacker != nullptr ? std::string{ playerName(*attacker) } + " (not a party player)" : "none";
		if (dead == nullptr || !dead->alive)
		{
			m_log.write(std::format("death ignored ({}, {}): {} already dead for the server or not a party player, killer {}", MatchLog::causeName(cause),
				phaseName(), dead != nullptr ? MatchLog::describe(*dead) : std::string{ playerName(victim) }, killerText));
			return; // an eliminated player cannot die again (e.g. the death camera of a late joiner)
		}
		if (m_phase == Phase::RoundResult)
		{
			// killed during the result screen or the PREPARE panel: no statistics or points, but the next round must
			// bring the player back (startNextRound sends the one-shot respawn to the players not alive)
			dead->alive = false;
			m_log.write(std::format("death during the round result ({}), no points, back next round: {}, killer {}", MatchLog::causeName(cause),
				MatchLog::describe(*dead), killerText));
			return;
		}
		if (m_phase != Phase::Running)
		{
			m_log.write(std::format("death ignored ({}, {}): {}, killer {}", MatchLog::causeName(cause), phaseName(), MatchLog::describe(*dead), killerText));
			return;
		}
		++dead->deaths;
		PartyPlayer* killer = attacker != nullptr ? playerOf(*attacker) : nullptr;
		if (killer != nullptr && killer != dead)
		{
			++killer->kills;
		}
		Utils::Logger::log("[HaveApi] room " + std::to_string(m_roomNumber) + ": " + dead->name + " died (cause " +
			std::to_string(static_cast<int>(cause)) + ", killer " + (killer != nullptr ? killer->name : std::string{ "none" }) + ")");
		if (killer != nullptr && killer != dead)
		{
			m_mode->onHit(*this, *dead, *killer, cause); // the killing hit is a hit too
		}
		m_mode->onDeath(*this, *dead, killer);
		recordPlacements();
		m_log.write(std::format("death ({}) at {} s: {}, killer {}", MatchLog::causeName(cause), roundElapsed().count(), MatchLog::describe(*dead),
			killer != nullptr ? MatchLog::describe(*killer) : killerText));
	}

	// A player leaves the match or the room (or disconnects): it leaves the roster; the round and the match are
	// checked again. With a single player left the match ends and that player wins.
	void PartyMatch::onPlayerLeft(std::uint32_t sessionId, std::string_view how)
	{
		const auto found = std::ranges::find(m_players, sessionId, &PartyPlayer::sessionId);
		if (found == m_players.end() || m_phase == Phase::Ended)
		{
			return;
		}
		m_log.write(std::format("player {} ({}): {}", how, phaseName(), MatchLog::describe(*found)));
		m_mode->onPlayerLeft(*this, *found);
		recordPlacements();
		const std::int32_t playerId = found->playerId;
		Utils::Logger::log("[HaveApi] room " + std::to_string(m_roomNumber) + ": " + found->name + " left the party match");
		m_players.erase(found);
		sendToLoaded(CH::encodePlayer(CH::MessageId::RemovePlayer, playerId, 0, 0));

		if (m_players.empty())
		{
			m_log.write("no party player left: the match stops");
			stop();
			return;
		}
		if (m_players.size() == 1 && (m_phase == Phase::Running || m_phase == Phase::RoundResult))
		{
			m_log.write("one party player left: the match ends");
			m_phaseTimer.cancel();
			finishMatch();
			return;
		}
		if (m_phase == Phase::Running && m_mode->isRoundOver(*this))
		{
			endRound(RoundEndReason::LastStanding);
		}
	}

	void PartyMatch::onHit(const Main::Network::Session& victim, const Main::Network::Session& attacker, CH::DeathCause cause)
	{
		PartyPlayer* hit = playerOf(victim);
		PartyPlayer* by = playerOf(attacker);
		if (hit == nullptr || by == nullptr || hit == by || m_phase != Phase::Running)
		{
			return;
		}
		m_log.hit(*by, *hit, cause);
		m_mode->onHit(*this, *hit, *by, cause);
	}

	// A hit of another player that would have killed was left at 1 hit point by CastServer (Lava Escape). At 1 hit
	// point every later hit would change nothing, and the game only pushes a player that takes damage: full health
	// again, so the next hits hurt and push.
	void PartyMatch::onSparedHit(const Main::Network::Session& victim)
	{
		const PartyPlayer* player = playerOf(victim);
		const bool restored = player != nullptr && player->alive && m_phase == Phase::Running;
		m_log.write(std::format("spared hit (left at 1 hit point by the CastServer, {}): {}{}", phaseName(),
			player != nullptr ? MatchLog::describe(*player) : std::string{ playerName(victim) }, restored ? ", full health again" : ", health not restored"));
		if (restored)
		{
			restoreHealth(*player);
		}
	}

	void PartyMatch::onRespawn(const Main::Network::Session& session, std::optional<SpawnPoint> at)
	{
		PartyPlayer* player = playerOf(session);
		if (player != nullptr && at)
		{
			rememberSpawn(m_spawnPoints, *at); // Hardpoint / Headquarters: the next zone keeps away from these
		}
		const std::string where = at ? std::format("({:.1f}, {:.1f})", at->x, at->y) : std::string{ "unknown position" };
		if (player == nullptr || m_phase != Phase::Running)
		{
			m_log.write(std::format("respawn at {} ({}, not handled by the game): {}", where, phaseName(),
				player != nullptr ? MatchLog::describe(*player) : std::string{ playerName(session) } + " (not a party player)"));
			return;
		}
		m_log.write(std::format("respawn at {}{}: {}", where, player->alive ? "" : " while dead for the server", MatchLog::describe(*player)));
		m_mode->onRespawn(*this, *player);
	}

	// ---- messages ----

	void PartyMatch::sendToLoaded(const std::vector<std::uint8_t>& message) const
	{
		const Main::Classes::Room* current = room();
		if (current == nullptr)
		{
			return;
		}
		for (const PartyPlayer& player : m_players)
		{
			if (!player.loaded)
			{
				continue;
			}
			if (const auto session = sessionOf(*current, player.sessionId))
			{
				sendToPlayer(*session, message);
			}
		}
	}

	std::vector<CH::RosterEntry> PartyMatch::roster() const
	{
		std::vector<CH::RosterEntry> entries;
		for (const PartyPlayer& player : m_players)
		{
			entries.push_back({ player.playerId, player.team, player.points, player.fallbackTeam });
		}
		return entries;
	}

	std::vector<std::uint8_t> PartyMatch::roundStateMessage(bool revive, bool fullHealth) const
	{
		const std::string text = banner();
		CH::RoundState round;
		round.banner = text;
		round.lengthSeconds = m_phase == Phase::Running ? roundSeconds() : 0;
		round.elapsedMilliseconds = static_cast<std::uint64_t>(
			std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - m_roundStart).count());
		round.countdownSounds = m_phase == Phase::Running && roundSeconds() != 0;
		round.actions.revive = revive;
		round.actions.restoreFullHealth = fullHealth;
		return CH::encodeRoundState(round);
	}

	void PartyMatch::sendRoundState(bool revive, bool fullHealth) const
	{
		m_log.write(std::format("round state (0x10) to the loaded players: banner \"{}\", {}, revive {}, full health {}", banner(), phaseName(), revive,
			fullHealth));
		sendToLoaded(roundStateMessage(revive, fullHealth));
	}

	void PartyMatch::sendBattleState(Main::Network::Session& session)
	{
		const auto found = std::ranges::find(m_players, static_cast<std::uint32_t>(session.getId()), &PartyPlayer::sessionId);
		if (found != m_players.end())
		{
			sendToPlayer(session, rosterFor(*found));
		}
		sendToPlayer(session, roundStateMessage(false));
		if (const auto announcement = m_mode->roundStartAnnouncement(*this);
			announcement && m_phase == Phase::Running && roundElapsed() < roundStartAnnouncementWindow)
		{
			sendToPlayer(session, CH::encodeAnnouncement(announcement->first, announcementSeconds, announcement->second));
		}
		if (m_marked != std::pair<std::int32_t, std::int32_t>{})
		{
			sendToPlayer(session, CH::encodeValues(CH::MessageId::MarkedPlayers, m_marked.first, m_marked.second));
		}
		for (const auto& message : m_chaserState)
		{
			sendToPlayer(session, message);
		}
		if (m_lava)
		{
			// the lava where it is now (the client moves it from there)
			const float t = std::chrono::duration<float>(std::chrono::steady_clock::now() - m_lavaAt).count();
			const CH::LavaState& lava = *m_lava;
			sendToPlayer(session, CH::encodeLavaFloor(true, CH::lavaFloorNodeHeight(lava.floorZ + lava.floorSpeed * t), lava.floorSpeed));
			if (lava.wallsOn)
			{
				sendToPlayer(session, CH::encodeLavaWalls({ lava.wallX1 + lava.wallX1Speed * t, lava.wallX1Speed,
					lava.wallX2 + lava.wallX2Speed * t, lava.wallX2Speed, lava.wallY1 + lava.wallY1Speed * t, lava.wallY1Speed,
					lava.wallY2 + lava.wallY2Speed * t, lava.wallY2Speed }));
			}
		}
		if (!m_lavaPhase.empty())
		{
			sendToPlayer(session, m_lavaPhase);
		}
		if (m_storm)
		{
			sendToPlayer(session, CH::encode(CH::MessageId::BattleZone, stormNow()));
		}
		if (!m_zoneMessage.empty())
		{
			sendToPlayer(session, m_zoneMessage);
		}
		if (!m_scoreMessage.empty())
		{
			sendToPlayer(session, m_scoreMessage);
		}
		if (!m_hqProgressMessage.empty())
		{
			sendToPlayer(session, m_hqProgressMessage);
		}
		if (!m_dominationZonesMessage.empty())
		{
			sendToPlayer(session, m_dominationZonesMessage);
		}
		if (!m_teamBasesMessage.empty())
		{
			sendToPlayer(session, m_teamBasesMessage);
		}
		if (!m_spawnAreasMessage.empty())
		{
			sendToPlayer(session, m_spawnAreasMessage);
		}
		for (const auto& [playerId, message] : m_modifierMessages)
		{
			sendToPlayer(session, message);
		}
		for (const auto& [playerId, message] : m_propStates)
		{
			sendToPlayer(session, message);
		}
		if (const auto phase = m_propPhases.find(static_cast<std::uint32_t>(session.getId())); phase != m_propPhases.end())
		{
			sendToPlayer(session, phase->second);
		}
		if (m_radarActive)
		{
			sendToPlayer(session, CH::encode(CH::MessageId::Radar, CH::BytePayload{ 1 })); // joined after the radar phase began
		}
		if (!m_teamRoundsMessage.empty())
		{
			sendToPlayer(session, m_teamRoundsMessage);
		}
		// players enter dying: the one-shot spawns them. Not for a late joiner of a mode without respawn: it
		// waits for the next round like an eliminated player (the round start sends the one-shot to everybody); nor for
		// one the mode keeps dead (Headquarters: its team holds the HQ), until its team may respawn again.
		if (found != m_players.end() && m_phase == Phase::Running && m_mode->keepsLateJoinerDead(*found))
		{
			found->alive = false;
		}
		else if (found == m_players.end() || found->alive || respawnAllowed())
		{
			sendRespawnOnce(static_cast<std::uint32_t>(session.getId()));
			return;
		}
		m_pendingActions.push_back({ PendingAction::Kind::DeathCamera, found->sessionId, std::chrono::steady_clock::now() + deathCameraDelay });
	}

	void PartyMatch::updatePlayer(const PartyPlayer& player)
	{
		m_log.write("team / role (0x0D): " + MatchLog::describe(player));
		sendToLoaded(CH::encodePlayer(CH::MessageId::UpdatePlayer, player.playerId, player.team, player.fallbackTeam));
	}

	void PartyMatch::updatePoints(const PartyPlayer& player)
	{
		sendToLoaded(CH::encodePoints(player.playerId, player.points));
	}

	void PartyMatch::restoreHealth(const PartyPlayer& player)
	{
		CH::PlayerActions actions;
		actions.restoreFullHealth = true;
		m_log.write("full health (0x14, neutral modifiers): " + MatchLog::describe(player));
		sendToLoaded(CH::encodePlayerModifiers(player.playerId, CH::neutralModifiers(), actions));
	}

	void PartyMatch::setMarked(std::int32_t first, std::int32_t second)
	{
		if (m_log.changed("marked", std::format("{} {}", first, second)))
		{
			m_log.write(std::format("marked players (0x17): {} and {}", first, second));
		}
		m_marked = { first, second };
		sendToLoaded(CH::encodeValues(CH::MessageId::MarkedPlayers, first, second));
	}

	void PartyMatch::setChaser(std::int32_t playerId, std::span<const std::int8_t> targetStates)
	{
		std::string states;
		for (const std::int8_t state : targetStates)
		{
			states += std::to_string(state) + " ";
		}
		if (m_log.changed("chaser", std::format("{} {}", playerId, states)))
		{
			m_log.write(std::format("chaser (0x1A) {}, target states (0x19) {}", playerId, states));
		}
		m_chaserState = { CH::encodeValues(CH::MessageId::ChaserId, playerId), CH::encodeChaserTargets(targetStates) };
		for (const auto& message : m_chaserState)
		{
			sendToLoaded(message);
		}
	}

	void PartyMatch::controlLava(const CH::LavaCommand& command, std::function<void(bool)> done)
	{
		// CastServer finds the room from any of its players
		auto anyPlayer = std::ranges::find_if(m_players, &PartyPlayer::loaded);
		if (anyPlayer == m_players.end())
		{
			anyPlayer = m_players.begin();
		}
		if (anyPlayer == m_players.end())
		{
			m_log.write("lava command not sent (no party player): " + MatchLog::describe(command));
			if (done)
			{
				done(false);
			}
			return;
		}
		m_log.write(std::format("lava command to the CastServer (via session {}, {}): {}; players {}", anyPlayer->sessionId, phaseName(),
			MatchLog::describe(command), MatchLog::describe(m_players)));
		Main::Ipc::M2C_partyLava(anyPlayer->sessionId, command,
			[weak = weak_from_this(), command, done = std::move(done)](std::optional<CH::LavaState> state) {
				const auto self = weak.lock();
				if (!self || self->m_stopped)
				{
					return; // the match is over: a late answer
				}
				self->logLavaAnswer(command, state);
				const bool applied = self->applyLava(command, state);
				if (done)
				{
					done(applied);
				}
			});
	}

	void PartyMatch::logLavaAnswer(const CH::LavaCommand& command, const std::optional<CH::LavaState>& state) const
	{
		if (!state)
		{
			m_log.write("lava answer: none from the CastServer (no link or timeout) to " + MatchLog::describe(command));
			return;
		}
		const bool roomLevel = command.action != CH::LavaAction::Stop && command.action != CH::LavaAction::Start && command.action != CH::LavaAction::SetSpeed;
		m_log.write(std::format("lava answer to action {}: {}{}", static_cast<int>(command.action), MatchLog::describe(*state),
			roomLevel && !state->active ? " (room not found at the CastServer)" : ""));
	}

	// The CastServer's answer to a lava command: the lava sent to the clients
	bool PartyMatch::applyLava(const CH::LavaCommand& command, const std::optional<CH::LavaState>& state)
	{
		if (!state)
		{
			return false;
		}
		if (command.action == CH::LavaAction::ProtectPlayers || command.action == CH::LavaAction::UnprotectPlayers)
		{
			m_playersProtected = command.action == CH::LavaAction::ProtectPlayers && state->active != 0;
			return state->active != 0;
		}
		if (command.action == CH::LavaAction::Stop || !state->active)
		{
			const bool wasShown = m_lava.has_value();
			m_lava.reset();
			if (wasShown)
			{
				sendToLoaded(CH::encodeLavaFloor(false, 0.0f, 0.0f));
			}
			if (command.action != CH::LavaAction::Stop)
			{
				Utils::Logger::log("[HaveApi] room " + std::to_string(m_roomNumber) + ": the lava could not start (room not found or no position, "
					"spawn and bounds of the map)", Utils::LogType::Warning);
			}
			return command.action == CH::LavaAction::Stop;
		}
		m_lava = *state;
		m_lavaAt = std::chrono::steady_clock::now();
		sendToLoaded(CH::encodeLavaFloor(true, CH::lavaFloorNodeHeight(state->floorZ), state->floorSpeed));
		if (state->wallsOn)
		{
			sendToLoaded(CH::encodeLavaWalls({ state->wallX1, state->wallX1Speed, state->wallX2, state->wallX2Speed,
				state->wallY1, state->wallY1Speed, state->wallY2, state->wallY2Speed }));
		}
		Utils::Logger::log("[HaveApi] room " + std::to_string(m_roomNumber) + ": lava " + (state->fromMap ? "from the map bounds" : "from the players") +
			", surface " + std::to_string(state->floorZ) +
			" speed " + std::to_string(state->floorSpeed) + (state->wallsOn ? ", walls x [" + std::to_string(state->wallX1) + ", " +
				std::to_string(state->wallX2) + "] y [" + std::to_string(state->wallY1) + ", " + std::to_string(state->wallY2) + "] speed " +
				std::to_string(state->wallX1Speed) : std::string{}));
		return true;
	}

	void PartyMatch::setLavaPhase(bool hot, bool sleeping)
	{
		m_lavaPhase = CH::encodeLavaPhase(hot, sleeping);
		m_log.write(std::format("lava phase (0x25): hot {}, sleeping {}", hot, sleeping));
		sendToLoaded(m_lavaPhase);
	}

	std::uint32_t PartyMatch::mapId() const
	{
		Main::Classes::Room* current = room(); // getActualMap is not const
		return current != nullptr ? current->getActualMap() : 0;
	}

	void PartyMatch::showZone(bool visible, float x, float y, std::int32_t state)
	{
		if (m_log.changed("zone", std::format("{} {:.1f} {:.1f} {}", visible, x, y, state)))
		{
			m_log.write(std::format("zone shown (0x27): visible {}, ({:.1f}, {:.1f}), state {}", visible, x, y, state));
		}
		m_zoneMessage = CH::encodeHardpointZone(visible, x, y, state);
		sendToLoaded(m_zoneMessage);
	}

	void PartyMatch::setZoneScore(std::int32_t red, std::int32_t blue, std::int32_t limit)
	{
		if (m_log.changedAtMostEvery("score", std::format("{} {} {}", red, blue, limit), std::chrono::seconds{ 10 }))
		{
			m_log.write(std::format("team score (0x28, at most every 10 s): red {} blue {} limit {}", red, blue, limit));
		}
		m_scoreMessage = CH::encodeHardpointScore(red, blue, limit);
		sendToLoaded(m_scoreMessage);
	}

	namespace
	{
		[[nodiscard]] CH::ZoneCommand zoneCommand(std::span<const PartyPlayer> players, std::size_t index, bool active, CH::ZoneShape shape,
			float x, float y, float radius, float keepOut, float zMin = 0.0f, float zMax = 0.0f)
		{
			CH::ZoneCommand command{};
			command.action = active ? CH::ZoneAction::Set : CH::ZoneAction::Clear;
			command.index = static_cast<std::uint8_t>(index);
			command.shape = shape;
			command.x = x;
			command.y = y;
			command.radius = radius;
			command.keepOut = keepOut;
			command.zMin = zMin;
			command.zMax = zMax;
			for (const PartyPlayer& player : players)
			{
				if (command.count < command.members.size())
				{
					command.members[command.count++] = { player.sessionId, player.fallbackTeam };
				}
			}
			return command;
		}
	}

	bool PartyMatch::placeZone(bool active, float x, float y, float radius, float keepOut)
	{
		return sendZoneCommand(zoneCommand(m_players, 0, active, CH::ZoneShape::Circle, x, y, radius, keepOut));
	}

	bool PartyMatch::placeSquareZone(std::size_t index, bool active, float x, float y, float halfSize, float zMin, float zMax,
		float keepOut)
	{
		if (index >= CH::maxZones)
		{
			return false;
		}
		return sendZoneCommand(zoneCommand(m_players, index, active, CH::ZoneShape::Square, x, y, halfSize, keepOut, zMin, zMax));
	}

	bool PartyMatch::sendZoneCommand(const CH::ZoneCommand& command)
	{
		if (m_players.empty())
		{
			return false;
		}
		const std::size_t index = command.index;
		const bool active = command.action == CH::ZoneAction::Set;
		m_zoneOccupants[index].clear();
		m_log.write(std::format("zone command {} to the CastServer: action {}, {} at ({:.1f}, {:.1f}) size {:.1f}, keep out {:.1f}, z [{:.1f}, {:.1f}], "
			"{} members", index, static_cast<int>(command.action), command.shape == CH::ZoneShape::Square ? "square" : "circle", command.x, command.y,
			command.radius, command.keepOut, command.zMin, command.zMax, command.count));
		Main::Ipc::M2C_partyZone(m_players.front().sessionId, command,
			[weak = weak_from_this(), active, index](std::optional<CH::ZoneAnswer> answer) {
				const auto self = weak.lock();
				if (!self || self->m_stopped)
				{
					return;
				}
				self->m_log.write(answer ? std::format("zone {} answer: done {}, {} occupants", index, answer->done, answer->count)
					: std::format("zone {} answer: none from the CastServer (no link or timeout)", index));
				self->m_zonePlaced[index] = active && answer && answer->done;
				self->m_zoneOccupants[index].clear();
				if (self->m_zonePlaced[index])
				{
					auto occupants = *answer;
					occupants.index = static_cast<std::uint8_t>(index);
					self->onZoneOccupants(occupants);
				}
			});
		return true; // sent; the CastServer's answer sets the occupants (and it pushes their changes, onZoneOccupants)
	}

	void PartyMatch::onZoneOccupants(const CH::ZoneAnswer& occupants)
	{
		if (occupants.index >= CH::maxZones)
		{
			return;
		}
		auto& zone = m_zoneOccupants[occupants.index];
		zone.clear();
		std::string names;
		for (std::size_t i = 0; i < occupants.count && i < occupants.sessions.size(); ++i)
		{
			const auto found = std::ranges::find(m_players, occupants.sessions[i], &PartyPlayer::sessionId);
			if (found != m_players.end())
			{
				zone.push_back(found->playerId);
				names += std::format("{}{} (team {})", names.empty() ? "" : ", ", found->name, found->fallbackTeam);
			}
		}
		if (m_log.changed("occupants " + std::to_string(occupants.index), names))
		{
			m_log.write(std::format("zone {} occupants: {}", occupants.index, names.empty() ? std::string{ "nobody" } : names));
		}
	}

	// No IPC: CastServer pushes the occupants when they change (onZoneOccupants).
	std::vector<std::int32_t> PartyMatch::playersInZone()
	{
		return m_zoneOccupants[0];
	}

	std::vector<std::int32_t> PartyMatch::playersInZone(std::size_t index)
	{
		return index < CH::maxZones ? m_zoneOccupants[index] : std::vector<std::int32_t>{};
	}

	void PartyMatch::showDominationZones(const CH::DominationZonesPayload& zones)
	{
		std::string text = std::format("{} zones, half size {:.1f}", zones.count, zones.halfSize);
		for (std::size_t i = 0; i < zones.zones.size() && i < static_cast<std::size_t>((std::max)(zones.count, 0)); ++i)
		{
			const CH::DominationZone& z = zones.zones[i];
			text += std::format("; {} at ({:.1f}, {:.1f}, {:.1f}) owner {} capturer {} contested {} progress {}", static_cast<char>('A' + i), z.x, z.y,
				z.z, z.owner, z.capturer, z.contested, z.permille);
		}
		if (m_log.changedAtMostEvery("domination", text, std::chrono::seconds{ 2 }))
		{
			m_log.write("domination zones (0x37): " + text);
		}
		m_dominationZonesMessage = CH::encodeDominationZones(zones);
		sendToLoaded(m_dominationZonesMessage);
	}

	void PartyMatch::showSpawnAreas(const CH::BattleSpawnsPayload& areas)
	{
		m_log.write(std::format("spawn areas (0x3D): {} boxes, {} players", areas.boxCount, areas.playerCount));
		m_spawnAreasMessage = CH::encode(CH::MessageId::BattleSpawns, areas);
		sendToLoaded(m_spawnAreasMessage);
	}

	void PartyMatch::showTeamBases(const CH::TeamBasesPayload& bases)
	{
		m_log.write(std::format("team bases (0x38): red respawns at base {} ({} points), blue at base {} ({} points)", bases.baseOf[0], bases.counts[0],
			bases.baseOf[1], bases.counts[1]));
		m_teamBasesMessage = CH::encodeTeamBases(bases);
		sendToLoaded(m_teamBasesMessage);
	}

	void PartyMatch::setRadar(bool active)
	{
		if (active != m_radarActive)
		{
			m_log.write(std::format("radar (0x1B): {}", active ? "on" : "off"));
		}
		m_radarActive = active;
		sendToLoaded(CH::encode(CH::MessageId::Radar, CH::BytePayload{ static_cast<std::uint8_t>(active ? 1 : 0) }));
	}

	void PartyMatch::announce(CH::Announcement event, std::string_view text)
	{
		m_log.write(std::format("announcement (0x16) {} to everybody{}", static_cast<int>(event), text.empty() ? "" : ": " + std::string{ text }));
		sendToLoaded(CH::encodeAnnouncement(event, announcementSeconds, text));
	}

	void PartyMatch::announceToPlayer(CH::Announcement event, const PartyPlayer& player, std::string_view text)
	{
		const Main::Classes::Room* current = room();
		const auto session = current != nullptr && player.loaded ? sessionOf(*current, player.sessionId) : nullptr;
		m_log.write(std::format("announcement (0x16) {} to {}{}{}", static_cast<int>(event), player.name, text.empty() ? "" : ": " + std::string{ text },
			session ? "" : " (not sent: not loaded or not in the room)"));
		if (session)
		{
			sendToPlayer(*session, CH::encodeAnnouncement(event, announcementSeconds, text));
		}
	}

	// One position probe per player (Cast LavaAction::Probe); `done` once every answer (or timeout) is in.
	void PartyMatch::requestPositions(std::vector<std::int32_t> playerIds, std::function<void(const Positions&)> done)
	{
		struct Gathering
		{
			Positions positions;
			std::size_t waiting = 0;
			std::function<void(const Positions&)> done;
		};
		auto gathering = std::make_shared<Gathering>();
		gathering->done = std::move(done);
		std::vector<std::pair<std::int32_t, std::uint32_t>> probes; // player id, session
		for (const std::int32_t id : playerIds)
		{
			const auto found = std::ranges::find(m_players, id, &PartyPlayer::playerId);
			if (found != m_players.end())
			{
				probes.emplace_back(id, found->sessionId);
			}
		}
		gathering->waiting = probes.size();
		if (probes.empty())
		{
			m_log.write("position probe: no player to ask");
			gathering->done(gathering->positions);
			return;
		}
		for (const auto& [id, sessionId] : probes)
		{
			CH::LavaCommand command{};
			command.action = CH::LavaAction::Probe;
			Main::Ipc::M2C_partyLava(sessionId, command,
				[weak = weak_from_this(), gathering, id = id](std::optional<CH::LavaState> state) {
					if (state && state->active)
					{
						gathering->positions[id] = { state->wallX1, state->wallY1, state->floorZ };
					}
					const auto self = weak.lock();
					if (self && !self->m_stopped)
					{
						self->m_log.write(state && state->active ? std::format("position probe of player {}: ({:.1f}, {:.1f}, {:.1f})", id, state->wallX1,
							state->wallY1, state->floorZ) : std::format("position probe of player {}: {}", id, state ? "unknown" : "no answer"));
					}
					if (--gathering->waiting == 0 && self && !self->m_stopped)
					{
						gathering->done(gathering->positions);
					}
				});
		}
	}

	void PartyMatch::killPlayer(const PartyPlayer& player)
	{
		CH::LavaCommand command{};
		command.action = CH::LavaAction::KillPlayer;
		m_log.write("kill by the party rules (CastServer, for everybody): " + MatchLog::describe(player));
		Main::Ipc::M2C_partyLava(player.sessionId, command, [weak = weak_from_this(), name = player.name](std::optional<CH::LavaState> state) {
			if (const auto self = weak.lock(); self && !self->m_stopped)
			{
				self->m_log.write(std::format("kill of {}: {}", name, state ? (state->active ? "done by the CastServer" : "room not found at the CastServer")
					: "no answer from the CastServer"));
			}
		});
	}

	void PartyMatch::spectate(const PartyPlayer& spectator, std::int32_t preferredPlayerId)
	{
		m_log.write(std::format("spectate requested by the game for {} (preferred player {}), in {} s", spectator.name, preferredPlayerId,
			spectateDelay.count()));
		m_pendingActions.push_back({ PendingAction::Kind::FollowLivingPlayer, spectator.sessionId,
			std::chrono::steady_clock::now() + spectateDelay, preferredPlayerId });
	}

	void PartyMatch::showPreparePanel(std::int32_t seconds)
	{
		m_log.write(std::format("prepare panel (0x15): {} s", seconds));
		sendToLoaded(CH::encodeValues(CH::MessageId::KillNotice, seconds));
	}

	std::chrono::milliseconds PartyMatch::roundElapsedMs() const
	{
		return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - m_roundStart);
	}

	std::int32_t PartyMatch::maxHealth() const
	{
		const Main::Classes::Room* current = room();
		const std::int32_t health = current != nullptr ? current->haveApi().options().maxHealth : 0;
		return health > 0 ? health : 2000;
	}

	void PartyMatch::sendToPartyPlayer(const PartyPlayer& player, const std::vector<std::uint8_t>& message) const
	{
		const Main::Classes::Room* current = room();
		if (const auto session = current != nullptr && player.loaded ? sessionOf(*current, player.sessionId) : nullptr)
		{
			sendToPlayer(*session, message);
		}
	}

	void PartyMatch::statusText(const PartyPlayer& player, std::string_view text)
	{
		if (m_log.changed("status " + std::to_string(player.sessionId), std::string{ text }))
		{
			m_log.write(std::format("status text to {}: {}", player.name, text.empty() ? std::string_view{ "(none)" } : text));
		}
		sendToPartyPlayer(player, CH::encodeText(CH::MessageId::StatusText, text));
	}

	void PartyMatch::playSound(const PartyPlayer& player, std::string_view sound)
	{
		m_log.write(std::format("sound {} to {}", sound, player.name));
		sendToPartyPlayer(player, CH::MessageWriter{ CH::MessageId::PlaySound }.append(std::uint32_t{ 0 }).appendText(sound).finish());
	}

	CH::BattleZonePayload PartyMatch::stormNow() const
	{
		CH::BattleZonePayload zone = *m_storm;
		const float t = std::chrono::duration<float>(std::chrono::steady_clock::now() - m_stormAt).count();
		for (std::size_t i = 0; i < static_cast<std::size_t>(std::clamp(zone.boxCount, 0, static_cast<std::int32_t>(zone.boxes.size()))); ++i)
		{
			CH::BattleZoneBox& box = zone.boxes[i];
			const float moved = std::clamp(t, 0.0f, box.moveSeconds);
			box.x1 += box.x1Speed * moved;
			box.x2 += box.x2Speed * moved;
			box.y1 += box.y1Speed * moved;
			box.y2 += box.y2Speed * moved;
			box.moveSeconds -= moved;
		}
		zone.phaseEndsInMs = (std::max)(0, zone.phaseEndsInMs - static_cast<std::int32_t>(t * 1000.0f));
		return zone;
	}

	void PartyMatch::setStorm(const CH::BattleZonePayload& zone)
	{
		m_storm = zone;
		m_stormAt = std::chrono::steady_clock::now();
		sendToLoaded(CH::encode(CH::MessageId::BattleZone, zone));
		sendStormCommand(CH::StormAction::Set);
		Utils::Logger::log("[HaveApi] room " + std::to_string(m_roomNumber) + ": storm phase " + std::to_string(zone.phase) + " (" +
			std::string{ zone.label.data(), strnlen(zone.label.data(), zone.label.size()) } + "), " + std::to_string(zone.phaseEndsInMs / 1000) + " s left");
		std::string boxes;
		for (std::size_t i = 0; i < static_cast<std::size_t>(std::clamp(zone.boxCount, 0, static_cast<std::int32_t>(zone.boxes.size()))); ++i)
		{
			const CH::BattleZoneBox& b = zone.boxes[i];
			boxes += std::format(" [x {:.0f}..{:.0f} y {:.0f}..{:.0f} moving {:.1f} s]", b.x1, b.x2, b.y1, b.y2, b.moveSeconds);
		}
		m_log.write(std::format("storm (0x3A): active {}, phase {} ({}), {} ms left, {} damage/s, boxes{}", zone.active, zone.phase,
			std::string{ zone.label.data(), strnlen(zone.label.data(), zone.label.size()) }, zone.phaseEndsInMs, zone.damagePerSecond, boxes));
	}

	void PartyMatch::clearStorm()
	{
		if (!m_storm)
		{
			return;
		}
		m_log.write("storm cleared");
		m_storm.reset();
		m_stormOutside.clear();
		m_lastOutside.clear();
		sendToLoaded(CH::encode(CH::MessageId::BattleZone, CH::BattleZonePayload{}));
		sendStormCommand(CH::StormAction::Clear);
	}

	void PartyMatch::sendStormCommand(CH::StormAction action)
	{
		if (m_players.empty())
		{
			return;
		}
		CH::StormCommand command{};
		command.action = action;
		if (action == CH::StormAction::Set && m_storm)
		{
			const auto count = static_cast<std::size_t>(std::clamp(m_storm->boxCount, 0, static_cast<std::int32_t>(command.boxes.size())));
			command.boxCount = static_cast<std::uint8_t>(count);
			for (std::size_t i = 0; i < count; ++i)
			{
				const CH::BattleZoneBox& b = m_storm->boxes[i];
				command.boxes[i] = { b.x1, b.x1Speed, b.x2, b.x2Speed, b.y1, b.y1Speed, b.y2, b.y2Speed, b.moveSeconds };
			}
			for (const PartyPlayer& player : m_players)
			{
				if (command.memberCount < command.members.size())
				{
					command.members[command.memberCount++] = player.sessionId;
				}
			}
		}
		if (action == CH::StormAction::Clear)
		{
			m_stormPlaced = false;
		}
		auto anyPlayer = std::ranges::find_if(m_players, &PartyPlayer::loaded);
		const std::uint32_t session = anyPlayer != m_players.end() ? anyPlayer->sessionId : m_players.front().sessionId;
		Main::Ipc::M2C_partyStorm(session, command, [weak = weak_from_this(), action](std::optional<CH::StormAnswer> answer) {
			const auto self = weak.lock();
			if (self && !self->m_stopped && action != CH::StormAction::Query)
			{
				self->m_log.write(answer ? std::format("storm command {} answer: done {}, {} outside", static_cast<int>(action), answer->done, answer->count)
					: std::format("storm command {} answer: none from the CastServer (no link or timeout)", static_cast<int>(action)));
			}
			if (!self || self->m_stopped || !answer || action == CH::StormAction::Clear || !self->m_storm)
			{
				return;
			}
			if (action == CH::StormAction::Set)
			{
				self->m_stormPlaced = answer->done != 0;
			}
			self->onStormOutside(*answer);
		});
	}

	void PartyMatch::onStormOutside(const CH::StormAnswer& outside)
	{
		if (!m_storm)
		{
			return;
		}
		const auto now = std::chrono::steady_clock::now();
		m_stormOutside.clear();
		std::string names;
		for (std::size_t i = 0; i < outside.count && i < outside.sessions.size(); ++i)
		{
			const auto found = std::ranges::find(m_players, outside.sessions[i], &PartyPlayer::sessionId);
			if (found != m_players.end())
			{
				m_stormOutside.push_back(found->playerId);
				m_lastOutside[found->playerId] = now;
				names += (names.empty() ? "" : ", ") + found->name;
			}
		}
		if (m_log.changed("storm outside", names))
		{
			m_log.write("outside the storm: " + (names.empty() ? std::string{ "nobody" } : names));
		}
	}

	bool PartyMatch::wasOutsideStorm(std::int32_t playerId, std::chrono::milliseconds within) const
	{
		if (std::ranges::find(m_stormOutside, playerId) != m_stormOutside.end())
		{
			return true;
		}
		const auto found = m_lastOutside.find(playerId);
		return found != m_lastOutside.end() && std::chrono::steady_clock::now() - found->second <= within;
	}

	void PartyMatch::sendStormDamage(std::span<const CH::StormHit> hits)
	{
		CH::StormDamagePayload payload{};
		std::string text;
		for (const CH::StormHit& hit : hits.first((std::min)(hits.size(), payload.hits.size())))
		{
			payload.hits[static_cast<std::size_t>(payload.count++)] = hit;
			text += std::format(" {}:{}", hit.playerId, hit.damage);
		}
		m_log.write("storm damage (0x3B, player id:damage):" + text);
		sendToLoaded(CH::encode(CH::MessageId::StormDamage, payload));
	}

	void PartyMatch::onStormLethal(const Main::Network::Session& sender, std::int32_t playerId)
	{
		const Main::Classes::Room* current = room();
		const bool fromHost = current != nullptr && current->isHost(sender.getAccountInfo().uniqueId);
		const auto victim = std::ranges::find(m_players, playerId, &PartyPlayer::playerId);
		m_log.write(std::format("lethal storm hit (0x3C) on player {} from {} ({}, {}): {}", playerId, playerName(sender), fromHost ? "host" : "not the host",
			phaseName(), victim != m_players.end() ? MatchLog::describe(*victim) : std::string{ "not a party player" }));
		if (!fromHost || m_phase != Phase::Running)
		{
			return;
		}
		if (victim == m_players.end() || !victim->alive)
		{
			return;
		}
		Utils::Logger::log("[HaveApi] room " + std::to_string(m_roomNumber) + ": the host reports a lethal storm hit on " + victim->name);
		m_mode->onStormLethal(*this, *victim);
	}

	void PartyMatch::recordPlacements()
	{
		const std::uint64_t now = Main::Details::getUtcTimeMs();
		for (const PartyPlayer& player : m_players)
		{
			if (player.placement != 0)
			{
				m_results.try_emplace(player.sessionId, RoomState::PartyResult{ player.placement, now });
			}
		}
	}

	void PartyMatch::announceToTeam(CH::Announcement event, std::int32_t team)
	{
		const Main::Classes::Room* current = room();
		if (current == nullptr)
		{
			return;
		}
		const auto message = CH::encodeAnnouncement(event, announcementSeconds);
		m_log.write(std::format("announcement (0x16) {} to team {}", static_cast<int>(event), team));
		for (const PartyPlayer& player : m_players)
		{
			const auto session = player.loaded && player.fallbackTeam == team ? sessionOf(*current, player.sessionId) : nullptr;
			if (session)
			{
				sendToPlayer(*session, message);
			}
		}
	}

	void PartyMatch::showHqProgress(const CH::HqProgressPayload& progress)
	{
		auto message = CH::encodeHqProgress(progress);
		if (message == m_hqProgressMessage)
		{
			return; // nothing changed: the DLL animates the bar itself
		}
		m_hqProgressMessage = std::move(message);
		if (m_log.changedAtMostEvery("hq", std::format("{} {} {}", progress.kind, progress.team, progress.permillePerSecond), std::chrono::seconds{ 1 }))
		{
			m_log.write(std::format("HQ progress (0x29): kind {}, team {}, {} permille ({} /s), {} s left", progress.kind, progress.team,
				progress.permille, progress.permillePerSecond, progress.secondsLeft));
		}
		sendToLoaded(m_hqProgressMessage);
	}

	void PartyMatch::setModifiers(const PartyPlayer& player, const CH::Modifiers& modifiers, std::int32_t health)
	{
		m_log.write(std::format("modifiers (0x14) to {}: {}{}", MatchLog::describe(player), MatchLog::describe(modifiers),
			health != 0 ? std::format(", hit points set to {}", health) : std::string{}));
		auto& message = m_modifierMessages[player.playerId];
		message = CH::encodePlayerModifiers(player.playerId, modifiers, {}); // what a late loader gets: no health
		if (health != 0)
		{
			CH::PlayerActions actions{};
			actions.health = health;
			sendToLoaded(CH::encodePlayerModifiers(player.playerId, modifiers, actions));
		}
		else
		{
			sendToLoaded(message);
		}
		// the game still sends the damage of a player the DLL disarmed (melee): CastServer drops it
		const bool disarm = modifiers.head.canDamageOthers == 0;
		if (m_disarmed[player.playerId] != disarm && player.sessionId != 0)
		{
			CH::LavaCommand command{};
			command.action = disarm ? CH::LavaAction::DisarmPlayer : CH::LavaAction::ArmPlayer;
			m_log.write(std::format("{} {} at the CastServer (session {})", disarm ? "disarm" : "arm", player.name, player.sessionId));
			Main::Ipc::M2C_partyLava(player.sessionId, command,
				[weak = weak_from_this(), playerId = player.playerId, name = player.name, disarm](std::optional<CH::LavaState> state) {
					const auto self = weak.lock();
					if (!self)
					{
						return;
					}
					self->m_log.write(std::format("{} {}: {}", disarm ? "disarm" : "arm", name, state ? (state->active ? "applied by the CastServer"
						: "room not found at the CastServer") : "no answer from the CastServer"));
					if (state)
					{
						self->m_disarmed[playerId] = disarm;
					}
				});
		}
	}

	void PartyMatch::setPropState(std::int32_t playerId, std::int32_t model, bool locked, const CH::PropWall& wall)
	{
		const auto player = std::ranges::find(m_players, playerId, &PartyPlayer::playerId);
		m_log.write(std::format("prop state (0x2A) of {} (id {}): model {}, {}{}", player != m_players.end() ? player->name : std::string{ "?" }, playerId,
			model, locked ? "locked" : "unlocked", wall.attached ? std::format(", on the wall at ({:.1f}, {:.1f}, {:.1f}) yaw {:.2f} anchor ({:.1f}, {:.1f})",
				wall.x, wall.y, wall.z, wall.yaw, wall.anchorX, wall.anchorY) : std::string{}));
		auto& message = m_propStates[playerId];
		message = CH::encodePropState(playerId, model, locked, wall);
		sendToLoaded(message);
	}

	void PartyMatch::propEvent(const CH::PropEventPayload& event)
	{
		m_log.write(std::format("prop event (0x2B): kind {}, player {}, at ({:.1f}, {:.1f}, {:.1f}), model {}, radius {:.1f}", event.kind, event.playerId,
			event.x, event.y, event.z, event.model, event.radius));
		sendToLoaded(CH::encodePropEvent(event));
	}

	void PartyMatch::sendPropPhase(const PartyPlayer& player, const CH::PropPhasePayload& phase)
	{
		auto message = CH::encodePropPhase(phase);
		auto& last = m_propPhases[player.sessionId];
		if (message == last)
		{
			return;
		}
		if (m_log.changed("prop phase " + std::to_string(player.sessionId), std::format("{} {} {} {} {}", phase.phase, phase.map, phase.changesLeft,
			phase.decoysLeft, phase.flashesLeft)))
		{
			m_log.write(std::format("prop phase (0x2C) to {}: phase {}, {} s left, map {}, changes {}, decoys {}, flashes {}", player.name, phase.phase,
				phase.secondsLeft, phase.map, phase.changesLeft, phase.decoysLeft, phase.flashesLeft));
		}
		last = std::move(message);
		const Main::Classes::Room* current = room();
		const auto session = current != nullptr && player.loaded ? sessionOf(*current, player.sessionId) : nullptr;
		if (session)
		{
			sendToPlayer(*session, last);
		}
	}

	void PartyMatch::onPropAction(const Main::Network::Session& session, const CH::PropActionPayload& action)
	{
		PartyPlayer* player = playerOf(session);
		m_log.write(std::format("prop action (0x2D) {} from {} ({}) at ({:.1f}, {:.1f}, {:.1f}) yaw {:.2f} anchor ({:.1f}, {:.1f})", action.action,
			player != nullptr ? MatchLog::describe(*player) : std::string{ playerName(session) } + " (not a party player)", phaseName(), action.x,
			action.y, action.z, action.yaw, action.anchorX, action.anchorY));
		if (player == nullptr || m_phase != Phase::Running)
		{
			return;
		}
		m_mode->onPropAction(*this, *player, action);
	}

	// Headquarters: the owners of the HQ do not respawn while they hold it. The respawn flag of 0x0E is per player, so
	// each player gets its own roster; when the block ends the blocked team's dead get the one-shot respawn at once.
	void PartyMatch::setRespawnBlockedTeam(std::int32_t team)
	{
		if (team == m_respawnBlockedTeam)
		{
			return;
		}
		const std::int32_t previous = m_respawnBlockedTeam;
		m_respawnBlockedTeam = team;
		const Main::Classes::Room* current = room();
		if (current == nullptr)
		{
			return;
		}
		Utils::Logger::log("[HaveApi] room " + std::to_string(m_roomNumber) + ": respawn blocked for team " + std::to_string(team));
		m_log.write(std::format("respawn blocked for team {} (was {})", team, previous));
		for (const PartyPlayer& player : m_players)
		{
			const auto session = player.loaded ? sessionOf(*current, player.sessionId) : nullptr;
			if (!session)
			{
				continue;
			}
			sendToPlayer(*session, rosterFor(player));
			if (previous != 0 && player.fallbackTeam == previous && !player.alive)
			{
				sendRespawnOnce(player.sessionId);
			}
		}
	}

	void PartyMatch::stop()
	{
		if (!m_stopped)
		{
			m_log.writeHits(m_players);
			m_log.write(std::format("match stopped ({}): {}", phaseName(), MatchLog::describe(m_players)));
			sendMatchInfo(false);
		}
		if (m_lava && !m_stopped)
		{
			controlLava({ CH::LavaAction::Stop }); // CastServer must not keep killing after the match
		}
		for (std::size_t index = 0; index < CH::maxZones; ++index)
		{
			if (m_zonePlaced[index] && !m_stopped)
			{
				// no zone (occupants, spawn grace) in the room's next match
				sendZoneCommand(zoneCommand(m_players, index, false, CH::ZoneShape::Circle, 0, 0, 0, 0));
			}
		}
		if (m_stormPlaced && !m_stopped)
		{
			sendStormCommand(CH::StormAction::Clear);
		}
		const bool anyDisarmed = std::ranges::any_of(m_disarmed, [](const auto& entry) { return entry.second; });
		if ((m_playersProtected || anyDisarmed) && !m_stopped)
		{
			controlLava({ CH::LavaAction::UnprotectPlayers }); // normal damage in the room's next match
		}
		m_stopped = true;
		m_modeTimer.cancel();
		m_tickTimer.cancel();
		m_phaseTimer.cancel();
	}

	// ---- match end ----

	// The match-end packet in the host's format: header + one ClientEndingMatch per player (server counters).
	Common::Network::Packet PartyMatch::matchEndPacket(const Main::Classes::Room& current,
		std::vector<Main::Structures::ClientEndingMatch>& scores) const
	{
		std::vector<std::uint8_t> data(sizeof(Main::ClientData::ClientEndingMatchHeader), 0);
		std::uint32_t firstSessionId = 0;
		for (const PartyPlayer& player : m_players)
		{
			const auto session = sessionOf(current, player.sessionId);
			if (!session)
			{
				continue;
			}
			Main::Structures::ClientEndingMatch score{};
			score.totalKills = counter(player.kills);
			score.totalDeaths = counter(player.deaths);
			score.uniqueId = session->getAccountInfo().uniqueId;
			const auto bytes = std::bit_cast<std::array<std::uint8_t, sizeof(score)>>(score);
			data.insert(data.end(), bytes.begin(), bytes.end());
			scores.push_back(score);
			if (firstSessionId == 0)
			{
				firstSessionId = player.sessionId;
			}
		}
		Common::Network::Packet packet;
		packet.setCommand(matchEndOrder, 0, 0, static_cast<std::uint32_t>(scores.size()));
		packet.setData(data.data(), static_cast<std::uint16_t>(data.size()));
		packet.setTcpHeader(firstSessionId, Common::Enums::NO_ENCRYPTION);
		return packet;
	}

	// Asks the host client to end the match: it answers with its own match-end packet, handled as a normal match end.
	void PartyMatch::requestHostMatchEnd()
	{
		Main::Classes::Room* current = room();
		if (current == nullptr)
		{
			return;
		}
		std::vector<Main::Structures::ClientEndingMatch> scores;
		const Common::Network::Packet request = matchEndPacket(*current, scores);
		for (const auto& [info, session] : current->getAllPlayersWithSessions())
		{
			if (current->isHost(session->getAccountInfo().uniqueId))
			{
				m_log.write(std::format("match end asked from the host {} (session {}), server fallback in {} s", playerName(*session), session->getId(),
					hostAnswerTimeout.count()));
				session->asyncWrite(request);
				PacketLog::writeGamePacket("match-end S->host (server asks)", session->getId(), playerName(*session), matchEndOrder, 0, 0,
					request.getOption(), { request.getData(), request.getDataSize() });
				break;
			}
		}
		after(hostAnswerTimeout, &PartyMatch::endGameMatch); // cancelled by Room::endMatch when the host answers
	}

	// Fallback when the host does not answer: the server plays the host's part of the normal match end
	// (Handlers/Room/EliminationNextRoundHandler.h) with its own counters: the match-end packet to the whole room,
	// every player's scoreboard with the normal rewards (order 254, mission 3), then the room match ends.
	void PartyMatch::endGameMatch()
	{
		Main::Classes::Room* current = room();
		if (current == nullptr || m_phase == Phase::Ended)
		{
			return;
		}
		m_phase = Phase::Ended;
		m_log.write("no match end from the host in time: the server ends the match with its own counters");

		std::vector<Main::Structures::ClientEndingMatch> players;
		Common::Network::Packet matchEnd = matchEndPacket(*current, players);
		if (players.empty())
		{
			m_log.write("server match end: no player left, the room match ends without scoreboard");
			current->endMatch();
			return;
		}
		current->broadcastToRoom(matchEnd);
		PacketLog::writeGamePacket("match-end S->room (server fallback)", 0, "", matchEndOrder, 0, 0,
			matchEnd.getOption(), { matchEnd.getData(), matchEnd.getDataSize() });

		// Rewards as in a normal match: processEndMatchPlayer computes MP/EXP, level up, stored stats, and sends the
		// scoreboard (order 254, mission 3).
		const std::uint64_t now = Main::Details::getUtcTimeMs();
		const bool shortMatch = now > current->getMatchStartTime() && now - current->getMatchStartTime() < minimumRewardedMatchMs;
		const Main::ClientData::ClientEndingMatchHeader header{};
		Common::Network::Packet response = matchEnd;
		for (const auto& score : players)
		{
			Main::Handlers::processEndMatchPlayer(score, header, current, response, matchEndOrder, now, shortMatch,
				m_mainServer.getExpMpBonusInfo(), m_mainServer.getEventMissionInfo());
		}
		Utils::Logger::log("[HaveApi] party match of room " + std::to_string(m_roomNumber) + ": match end sent");
		current->endMatch(); // stops and releases this match: keep it last
	}

	void RoomState::stopMatch()
	{
		if (m_match)
		{
			m_match->stop();
			m_match.reset();
		}
	}

	void logClientMatchEnd(const Main::Classes::Room& room, const Main::Network::Session& sender, std::string_view what)
	{
		if (room.getRoomSettings().mode != CH::partyRoomMode)
		{
			return;
		}
		const PartyMatch* match = room.haveApi().match();
		const std::string text = std::format("match end packet (order 254) from {} (session {}, {}): {}; party match {}", playerName(sender), sender.getId(),
			room.isHost(sender.getAccountInfo().uniqueId) ? "host" : "not the host", what,
			match != nullptr ? "still running (" + std::string{ match->phaseName() } + ")" : std::string{ "not running" });
		if (match != nullptr)
		{
			match->log(text);
			return;
		}
		CH::writeModeLog(room.haveApi().partySettings().modeId, room.getRoomNumber(), 0, text);
	}

	void startPartyMatch(Main::MainServer& mainServer, Main::Classes::RoomsManager& roomsManager, Main::Classes::Room& room)
	{
		if (room.getRoomSettings().mode != CH::partyRoomMode || !room.haveApi().hasPartyGame())
		{
			return;
		}
		room.haveApi().stopMatch();
		auto match = std::make_shared<PartyMatch>(mainServer, roomsManager, room.getRoomNumber(), room.haveApi().partySettings());
		room.haveApi().setMatch(match);
		match->start(room);
	}
}
