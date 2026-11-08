#ifndef MAIN_CONTROL_ANNOUNCE_BROADCAST_H
#define MAIN_CONTROL_ANNOUNCE_BROADCAST_H

#include "Control/AnnounceParser.h"
#include "Network/Packet.h"
#include "Enums/MiscellaneousEnums.h"

namespace Main
{
	namespace Control
	{
		inline Common::Network::Packet buildAnnouncePacket(AnnounceKind kind, const std::string& message, std::uint32_t sessionId = 0)
		{
			Common::Network::Packet packet;
			packet.setTcpHeader(sessionId, Common::Enums::NO_ENCRYPTION);
			packet.setCommand(402, 0, kind == AnnounceKind::Tip ? 0xC : 0xA, 0);
			packet.setData(reinterpret_cast<std::uint8_t*>(const_cast<char*>(message.c_str())), static_cast<std::uint16_t>(message.size()));
			return packet;
		}
	}
}

#endif
