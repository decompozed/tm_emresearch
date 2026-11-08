#ifndef CAST_ROOMS_ROOM_H
#define CAST_ROOMS_ROOM_H

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>
#include "Network/Sessions/CastSession.h"
#include "Rooms/PartyLava.h"
#include "Rooms/PartyLog.h"
#include "Rooms/PartyStorm.h"
#include "Rooms/PartyZone.h"
#include "Rooms/PlayerRoster.h"
#include "Rooms/PositionBatcher.h"
#include "Rooms/RoomMembership.h"
#include "Structures/ClientData/Structures.h"

namespace Cast::Domain
{
	class Room
	{
	public:
		using Packet = Common::Network::UnecryptedPacket;
		using SessionPtr = std::shared_ptr<Network::Session>;

		explicit Room(std::uint32_t roomNumber);

		[[nodiscard]] std::uint32_t getRoomNumber() const noexcept { return m_roomNumber; }
		[[nodiscard]] std::uint32_t getMap() const noexcept { return m_map; }
		void setMap(std::uint32_t map) noexcept { m_map = map; }
		[[nodiscard]] std::uint32_t getMode() const noexcept { return m_mode; }
		[[nodiscard]] bool isModeKnown() const noexcept { return m_modeKnown; }
		void setMode(std::uint32_t mode) noexcept;
		[[nodiscard]] bool isInvisible() const noexcept { return m_invisible; }
		void setInvisible(bool invisible) noexcept { m_invisible = invisible; }

		RoomMembership& membership() noexcept { return m_membership; }
		const RoomMembership& membership() const noexcept { return m_membership; }
		[[nodiscard]] bool isHost(std::uint32_t session) const noexcept { return m_membership.isHost(session); }
		[[nodiscard]] std::optional<std::uint32_t> host() const noexcept { return m_membership.host(); }
		[[nodiscard]] bool isAuthorizedMember(std::uint32_t session) const noexcept { return m_membership.isAuthorizedMember(session); }
		[[nodiscard]] bool takeSyncRequestSlot(std::chrono::steady_clock::time_point now);
		[[nodiscard]] std::uint32_t syncEpoch() const noexcept { return m_syncEpoch; }
		void setSyncEpoch(std::uint32_t epoch) noexcept { m_syncEpoch = epoch; }

		void addPlayer(const SessionPtr& session);
		void removePlayer(std::uint64_t sessionId);
		[[nodiscard]] SessionPtr findPlayer(std::uint32_t sessionId) const { return m_roster.find(sessionId); }
		void setPlayerTeams(const std::vector<Main::ClientData::PlayerTeamInfo>& players);
		void detachAll();
		template<class Fn>
		void detachUndeclared(Fn&& onDetached)
		{
			m_roster.removeIf(
				[this](const SessionPtr& session) { return !m_membership.isAuthorizedMember(static_cast<std::uint32_t>(session->getId())); },
				[&](const SessionPtr& session) {
					session->setInMatch(false);
					onDetached(session->getId());
				});
		}

		void broadcastToMatch(Packet& packet);
		void broadcastToMatchExceptSelf(Packet& packet, std::uint32_t selfId);
		bool forwardToHost(std::uint32_t senderSessionId, Packet& packet);
		bool forwardToPlayer(std::uint32_t senderSessionId, std::uint32_t receiverSessionId, Packet& packet);

		void endMatch();
		bool killOnlyForSelf(const SessionPtr& player);
		bool killForAll(std::uint32_t sessionId);
		void rememberExplosive(std::uint32_t ammoId, std::uint32_t ownerSession);
		[[nodiscard]] std::optional<std::uint32_t> explosiveOwner(std::uint32_t ammoId) const;

		PartyLava& lava() noexcept { return m_lava; }
		PartyZone& zone(std::size_t index) noexcept { return m_zones[(std::min)(index, m_zones.size() - 1)]; }
		std::array<PartyZone, Common::HaveApi::maxZones>& zones() noexcept { return m_zones; }
		PartyStorm& storm() noexcept { return m_storm; }
		PartyLog& partyLog() noexcept { return m_partyLog; }

		void enqueuePosition(std::span<const std::uint8_t> entry);
		void flushPendingPositions();

	private:
		[[nodiscard]] static bool canDie(const Network::Session& player) noexcept;
		[[nodiscard]] Packet fallDeathPacket(const Network::Session& victim) const;
		void broadcastToRoom(Packet& packet);

		std::uint32_t m_roomNumber{};
		std::uint32_t m_serverId{};
		std::uint32_t m_map{};
		std::uint32_t m_mode{};
		bool m_modeKnown{};
		bool m_invisible{};
		std::chrono::steady_clock::time_point m_clockStart{ std::chrono::steady_clock::now() };

		PlayerRoster m_roster;
		RoomMembership m_membership;
		std::optional<std::chrono::steady_clock::time_point> m_lastSyncRequest;
		std::uint32_t m_syncEpoch{};

		PartyLava m_lava;
		std::array<PartyZone, Common::HaveApi::maxZones> m_zones{ PartyZone{ 0 }, PartyZone{ 1 }, PartyZone{ 2 } };
		PartyStorm m_storm;
		PartyLog m_partyLog;
		std::array<std::pair<std::uint32_t, std::uint32_t>, 256> m_explosiveOwners{};
		std::size_t m_nextExplosive{};

		PositionBatcher m_batcher;
		Packet m_positionBatch{ Common::Constants::maxPacketBytes, 322, 0 };
	};
}

#endif
