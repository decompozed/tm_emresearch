/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#include "Rooms/Room.h"
#include <Utils/SetupParser.h>
#include "Structures/Combat/CombatPackets.h"

namespace Cast::Domain
{
	namespace
	{
		constexpr std::chrono::seconds syncRequestInterval{ 1 };
		constexpr std::uint16_t explosionDamageOrder = 264;
	}

	Room::Room(std::uint32_t roomNumber)
		: m_roomNumber{ roomNumber }
		, m_serverId{ Common::Utils::SetupParser::getInstance().getSelfCastServerInfo().serverNumber }
	{
	}

	void Room::setMode(std::uint32_t mode) noexcept
	{
		m_mode = mode;
		m_modeKnown = true;
	}

	bool Room::takeSyncRequestSlot(std::chrono::steady_clock::time_point now)
	{
		if (m_lastSyncRequest && now - *m_lastSyncRequest < syncRequestInterval)
		{
			return false;
		}
		m_lastSyncRequest = now;
		return true;
	}

	void Room::addPlayer(const SessionPtr& session)
	{
		m_roster.add(session);
	}

	void Room::removePlayer(std::uint64_t sessionId)
	{
		if (const auto session = m_roster.find(sessionId))
		{
			session->setInMatch(false);
		}
		m_roster.remove(sessionId);
	}

	void Room::setPlayerTeams(const std::vector<Main::ClientData::PlayerTeamInfo>& players)
	{
		m_roster.forEachLive([&](const SessionPtr& session) {
			const auto info = std::ranges::find_if(players, [&](const Main::ClientData::PlayerTeamInfo& player) { return player.uid.session == session->getId(); });
			if (info != players.end())
			{
				session->setTeam(static_cast<Common::Enums::Team>(info->team));
				const auto& nickname = info->nickname;
				session->setNickname(std::string{ std::begin(nickname), std::ranges::find(nickname, '\0') });
			}
		});
	}

	void Room::detachAll()
	{
		m_roster.removeIf([](const SessionPtr&) { return true; }, [](const SessionPtr& session) { session->setInMatch(false); });
	}

	void Room::broadcastToRoom(Packet& packet)
	{
		m_roster.forEachLive([&](const SessionPtr& player) {
			packet.setTcpHeader(player->getId());
			player->asyncWrite(packet);
		});
	}

	void Room::broadcastToMatch(Packet& packet)
	{
		m_roster.forEachLive([&](const SessionPtr& player) {
			if (player->isInMatch())
			{
				packet.setTcpHeader(player->getId());
				player->asyncWrite(packet);
			}
		});
	}

	void Room::broadcastToMatchExceptSelf(Packet& packet, std::uint32_t selfId)
	{
		m_roster.forEachLive([&](const SessionPtr& player) {
			if (player->isInMatch() && player->getId() != selfId)
			{
				packet.setTcpHeader(player->getId());
				player->asyncWrite(packet);
			}
		});
	}

	bool Room::forwardToHost(std::uint32_t senderSessionId, Packet& packet)
	{
		const auto hostId = m_membership.host();
		return hostId && forwardToPlayer(senderSessionId, *hostId, packet);
	}

	bool Room::forwardToPlayer(std::uint32_t senderSessionId, std::uint32_t receiverSessionId, Packet& packet)
	{
		const auto receiver = m_roster.find(receiverSessionId);
		if (!receiver)
		{
			return false;
		}
		packet.setTcpHeader(senderSessionId);
		receiver->asyncWrite(packet);
		return true;
	}

	void Room::endMatch()
	{
		m_clockStart = std::chrono::steady_clock::now();
		m_storm.clear();
		m_lava.forgetPositions();
		m_roster.forEachLive([](const SessionPtr& session) { session->setInMatch(false); });
	}

	bool Room::canDie(const Network::Session& player) noexcept
	{
		return player.isInMatch() && !player.isDead() && player.getTeam() != Common::Enums::TEAM_OBSERVER;
	}

	Room::Packet Room::fallDeathPacket(const Network::Session& victim) const
	{
		const auto death = Structures::makeFallDeath({ static_cast<std::uint32_t>(victim.getId()), m_serverId, 0 }, victim.lastPosition());
		Packet packet;
		packet.setCommand(explosionDamageOrder, 0, 0, 1);
		packet.setData(reinterpret_cast<const std::uint8_t*>(&death), sizeof(death));
		return packet;
	}

	bool Room::killOnlyForSelf(const SessionPtr& player)
	{
		if (!player || !canDie(*player))
		{
			return false;
		}
		auto packet = fallDeathPacket(*player);
		packet.setTcpHeader(player->getId());
		player->setDead(true);
		player->asyncWrite(packet);
		return true;
	}

	bool Room::killForAll(std::uint32_t sessionId)
	{
		const auto victim = m_roster.find(sessionId);
		if (!victim || !canDie(*victim))
		{
			return false;
		}
		victim->setDead(true);
		auto packet = fallDeathPacket(*victim);
		broadcastToMatch(packet);
		return true;
	}

	void Room::rememberExplosive(std::uint32_t ammoId, std::uint32_t ownerSession)
	{
		m_explosiveOwners[m_nextExplosive] = { ammoId, ownerSession };
		m_nextExplosive = (m_nextExplosive + 1) % m_explosiveOwners.size();
	}

	std::optional<std::uint32_t> Room::explosiveOwner(std::uint32_t ammoId) const
	{
		if (ammoId == 0)
		{
			return std::nullopt;
		}
		for (std::size_t age = 1; age <= m_explosiveOwners.size(); ++age)
		{
			const auto& [id, owner] = m_explosiveOwners[(m_nextExplosive + m_explosiveOwners.size() - age) % m_explosiveOwners.size()];
			if (id == ammoId)
			{
				return owner;
			}
		}
		return std::nullopt;
	}

	void Room::enqueuePosition(std::span<const std::uint8_t> entry)
	{
		m_batcher.enqueue(entry);
	}

	void Room::flushPendingPositions()
	{
		const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - m_clockStart);
		m_batcher.flush(static_cast<std::uint32_t>(elapsed.count() / 10), [this](std::uint8_t count, std::span<const std::uint8_t> payload) {
			m_positionBatch.setOption(count);
			m_positionBatch.setData(payload.data(), static_cast<std::uint16_t>(payload.size()));
			broadcastToRoom(m_positionBatch);
		});
	}
}
