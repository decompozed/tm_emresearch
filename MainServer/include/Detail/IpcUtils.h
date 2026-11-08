#ifndef IPC_UTILS_MAIN_H
#define IPC_UTILS_MAIN_H

#include <Utils/Constants.h>
#include <Network/Packet.h>
#include "Structures/AccountInfo/MainAccountUniqueId.h"
#include "Structures/ClientData/Structures.h"
#include "HaveApi/Lava.h"
#include "HaveApi/PartyMatchInfo.h"
#include "HaveApi/Storm.h"
#include "HaveApi/Zone.h"
#include "HaveApi/RoomSync.h"
#include "Network/CastLink.h"
#include <cstring>
#include <functional>
#include <optional>
#include <string>
#include <vector>

// MainServer -> CastServer commands. All of them go through the CastServer's persistent link (Main::Ipc::CastLink)
// and never wait: the commands without an answer return whether the link took them (false = no CastServer link); the
// two with an answer (Lava, Zone) call their callback when it arrives, or with nothing after the timeout.
namespace Main
{
	namespace Ipc
	{
		inline bool M2C_sendMapId(std::uint32_t hostSessionId, std::uint32_t mapId, std::uint32_t modeId)
		{
			Common::Network::UnecryptedPacket packet;
			packet.setTcpHeader(hostSessionId);
			packet.setCommand(Common::Constants::M2C_mapId, 0, static_cast<std::uint8_t>(mapId), static_cast<std::uint8_t>(modeId));
			return CastLink::instance().send(packet);
		}

		inline bool M2C_sendRoomNumber(std::uint32_t hostSessionId, std::uint32_t roomNumber)
		{
			Common::Network::UnecryptedPacket packet;
			packet.setTcpHeader(hostSessionId);
			packet.setCommand(Common::Constants::M2C_roomNumber, 0, static_cast<std::uint8_t>(roomNumber), 0);
			return CastLink::instance().send(packet);
		}

		inline bool M2C_sendPlayerTeamInfoBatch(std::uint32_t hostSessionId, const std::vector<Main::ClientData::PlayerTeamInfo>& playerInfos)
		{
			Common::Network::UnecryptedPacket packet;
			packet.setTcpHeader(hostSessionId);
			packet.setCommand(Common::Constants::M2C_playerTeamInfoBatch, 0, 0, 0);
			packet.setData(reinterpret_cast<const std::uint8_t*>(playerInfos.data()),
				playerInfos.size() * sizeof(Main::ClientData::PlayerTeamInfo));
			return CastLink::instance().send(packet);
		}

		inline bool M2C_sendRoomSync(const Common::HaveApi::RoomSyncMessage& message)
		{
			Common::Network::UnecryptedPacket packet;
			packet.setTcpHeader(message.header.hostSession);
			packet.setCommand(Common::Constants::M2C_roomSync, 0, 0, 0);
			packet.setData(reinterpret_cast<const std::uint8_t*>(&message), static_cast<std::uint16_t>(message.byteSize()));
			return CastLink::instance().send(packet);
		}

		enum InvisibilityType { SELF_NOT_INVISIBLE = 0, SELF_INVISIBLE, ALL_INVISIBLE, NONE_INVISIBLE };

		// have_api party matches: CastServer sends the normal death packet (order 264) to this player only, so that its
		// client shows the game's death camera (eliminated late joiners). The other clients do not see the death.
		inline void M2C_sendPartyKillPlayer(std::uint32_t sessionId)
		{
			Common::Network::UnecryptedPacket packet;
			packet.setTcpHeader(sessionId);
			packet.setCommand(Common::Constants::M2C_partyKillPlayer, 0, 0, 0);
			CastLink::instance().send(packet);
		}

		// have_api party matches: the game and round of the room of `sessionId`, for the CastServer's mode log lines.
		inline void M2C_sendPartyMatchInfo(std::uint32_t sessionId, const Common::HaveApi::PartyMatchInfo& info)
		{
			Common::Network::UnecryptedPacket packet;
			packet.setTcpHeader(sessionId);
			packet.setCommand(Common::Constants::M2C_partyMatchInfo, 0, 0, 0);
			packet.setData(reinterpret_cast<const std::uint8_t*>(&info), sizeof(info));
			CastLink::instance().send(packet);
		}

		// have_api Lava Escape: a command for the lava of the room of `sessionId`; `done` gets the lava the CastServer
		// answered with (nothing: no link / no answer within 3 s).
		inline void M2C_partyLava(std::uint32_t sessionId, const Common::HaveApi::LavaCommand& command,
			std::function<void(std::optional<Common::HaveApi::LavaState>)> done = {})
		{
			Common::Network::UnecryptedPacket packet;
			packet.setCommand(Common::Constants::M2C_partyLava, 0, 0, 0);
			packet.setData(reinterpret_cast<const std::uint8_t*>(&command), sizeof(command));
			packet.setTcpHeader(sessionId);
			CastLink::instance().request(packet, [done = std::move(done)](const Common::Network::UnecryptedPacket* answer) {
				if (!done)
				{
					return;
				}
				if (answer == nullptr || answer->getDataSize() != sizeof(Common::HaveApi::LavaState))
				{
					::Utils::Logger::log("no lava answer from Cast", ::Utils::LogType::Warning, "Main::M2C_partyLava");
					done(std::nullopt);
					return;
				}
				Common::HaveApi::LavaState state{};
				std::memcpy(&state, answer->getData(), sizeof(state));
				done(state);
			});
		}

		// have_api Hardpoint: a command for the zone of the room of `sessionId`; `done` gets who is in the zone.
		inline void M2C_partyZone(std::uint32_t sessionId, const Common::HaveApi::ZoneCommand& command,
			std::function<void(std::optional<Common::HaveApi::ZoneAnswer>)> done = {})
		{
			Common::Network::UnecryptedPacket packet;
			packet.setCommand(Common::Constants::M2C_partyZone, 0, 0, 0);
			packet.setData(reinterpret_cast<const std::uint8_t*>(&command), sizeof(command));
			packet.setTcpHeader(sessionId);
			CastLink::instance().request(packet, [done = std::move(done)](const Common::Network::UnecryptedPacket* answer) {
				if (!done)
				{
					return;
				}
				if (answer == nullptr || answer->getDataSize() != sizeof(Common::HaveApi::ZoneAnswer))
				{
					::Utils::Logger::log("no zone answer from Cast", ::Utils::LogType::Warning, "Main::M2C_partyZone");
					done(std::nullopt);
					return;
				}
				Common::HaveApi::ZoneAnswer zone{};
				std::memcpy(&zone, answer->getData(), sizeof(zone));
				done(zone);
			});
		}

		inline void M2C_partyStorm(std::uint32_t sessionId, const Common::HaveApi::StormCommand& command,
			std::function<void(std::optional<Common::HaveApi::StormAnswer>)> done = {})
		{
			Common::Network::UnecryptedPacket packet;
			packet.setCommand(Common::Constants::M2C_partyStorm, 0, 0, 0);
			packet.setData(reinterpret_cast<const std::uint8_t*>(&command), sizeof(command));
			packet.setTcpHeader(sessionId);
			CastLink::instance().request(packet, [done = std::move(done)](const Common::Network::UnecryptedPacket* answer) {
				if (!done)
				{
					return;
				}
				if (answer == nullptr || answer->getDataSize() != sizeof(Common::HaveApi::StormAnswer))
				{
					::Utils::Logger::log("no storm answer from Cast", ::Utils::LogType::Warning, "Main::M2C_partyStorm");
					done(std::nullopt);
					return;
				}
				Common::HaveApi::StormAnswer storm{};
				std::memcpy(&storm, answer->getData(), sizeof(storm));
				done(storm);
			});
		}

		// Fire and forget: tick rate (option) or restart, to the Cast server.
		inline void M2C_sendServerCommand(std::uint32_t order, std::uint32_t sessionId, std::uint8_t option)
		{
			Common::Network::UnecryptedPacket packet;
			packet.setTcpHeader(sessionId);
			packet.setCommand(static_cast<std::uint16_t>(order), 0, 0, option);
			CastLink::instance().send(packet);
		}

		inline void M2C_sendInvisibilityCommand(std::uint32_t sessionId, InvisibilityType type)
		{
			Common::Network::UnecryptedPacket packet;
			packet.setTcpHeader(sessionId);
			packet.setCommand(Common::Constants::M2C_Invisibility, 0, static_cast<std::uint8_t>(type), 0);
			CastLink::instance().send(packet);
		}

		// The client's IP (AuthServer -> MainServer A2M_passIp), passed on to the Cast server.
		inline bool M2C_passIp(const std::string& ip)
		{
			Common::Network::UnecryptedPacket packet;
			packet.setTcpHeader(0);
			packet.setCommand(Common::Constants::A2M_passIp, 0, 0, 0);
			packet.setData(reinterpret_cast<const std::uint8_t*>(ip.c_str()), ip.size() + 1);
			return CastLink::instance().send(packet);
		}
	}
}
#endif
