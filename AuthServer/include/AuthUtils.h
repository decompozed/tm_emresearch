
#ifndef AUTH_UTILITIES_H
#define AUTH_UTILITIES_H

#include <unordered_map>
#include <cstdint>
#include <chrono>
#include <vector>
#include <asio.hpp>
#include <Utils/SetupParser.h>
#include <Network/Packet.h>
#include <Utils/Constants.h>
#include <Utils/Logger.h>
#include "MainIpcExchange.h"

namespace Auth
{
	namespace Utils
	{
        // this also disconnects the player if they're already online through a different session
        inline std::unordered_map<std::uint32_t, std::uint32_t> getPlayersPerServer(std::uint32_t aid, const std::string& ip)
        {
            std::unordered_map<std::uint32_t, std::uint32_t> playerCounts;

            try
            {
                for (const auto& server : Common::Utils::SetupParser::getInstance().getMainServersInfo())
                {
                    try
                    {
                        std::vector<std::uint8_t> request;
                        Common::Network::UnecryptedPacket packet;

                        // pass IP
                        packet.setTcpHeader(0);
                        packet.setCommand(Common::Constants::A2M_passIp, 0, 0, 0);
                        packet.setData(reinterpret_cast<const std::uint8_t*>(ip.c_str()), ip.size() + 1);
                        packet.appendOutgoingPacket(request);

                        // Request players per server
                        packet.setTcpHeader(0);
                        packet.setCommand(Common::Constants::A2M_getPlayersPerServer, 0, 0, 0);
                        packet.setData(nullptr, 0);
                        packet.appendOutgoingPacket(request);

                        // Disconnect request
                        packet.setTcpHeader(0);
                        packet.setCommand(Common::Constants::A2M_disconnectOnlinePlayer, 0, 0, 0);
                        packet.setData(reinterpret_cast<const std::uint8_t*>(&aid), sizeof(aid));
                        packet.appendOutgoingPacket(request);

                        auto answer = exchangeWithMain(server, request, std::chrono::milliseconds(2000));
                        if (!answer)
                        {
                            continue;
                        }

                        Common::Network::UnecryptedPacket responsePacket;
                        if (!responsePacket.processIncomingPacket(answer->data(), static_cast<std::uint16_t>(answer->size())))
                            continue;

                        if (responsePacket.getOrder() != Common::Constants::A2M_getPlayersPerServer)
                            continue;

                        if (answer->size() >= sizeof(std::uint32_t))
                        {
                            const std::uint32_t playerCount = *reinterpret_cast<const std::uint32_t*>(responsePacket.getData());
                            playerCounts[server.serverNumber] = playerCount;
                        }
                    }
                    catch (const std::exception& e)
                    {
                        ::Utils::Logger::log("Exception for server " + server.ip + ": " + std::string(e.what()), ::Utils::LogType::Warning, "Auth::getPlayerCountFromMain");
                    }
                }
            }
            catch (const std::exception& e)
            {
                ::Utils::Logger::log("Exception: " + std::string(e.what()), ::Utils::LogType::Error, "Auth::getPlayerCountFromMain");
            }

            return playerCounts;
        }

	}
}
#endif
