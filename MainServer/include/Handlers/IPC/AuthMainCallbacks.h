#ifndef AUTH_MAIN_IPC_CALLBACKS_H
#define AUTH_MAIN_IPC_CALLBACKS_H

#include "Detail/IpcUtils.h"
#include "Network/Packet.h"
#include "Network/MainSessionManager.h"
#include "Network/Sessions/AuthSession.h"
#include <string>

namespace Main
{
	namespace Handlers
	{
        inline void disconnectIfOnline(const Common::Network::UnecryptedPacket& request, std::shared_ptr<Common::Network::Session> session, 
            Main::Network::SessionsManager& sessionsManager)
        {
            auto response = request;

            const std::uint32_t accountId = Main::Details::parseData<std::uint32_t>(request); 

            if (auto targetSession = sessionsManager.getSessionByAccountId(accountId); targetSession)
            {
                Common::Network::Packet response;
                response.setTcpHeader(targetSession->getId(), Common::Enums::NO_ENCRYPTION);
                sessionsManager.removeSession(targetSession->getId());
                response.setOrder(73);
                response.setExtra(5);
                targetSession->asyncWrite(response);
                response.setExtra(0);
            }
            else
            { // player not found on this server, proceed to check whether they're on other servers
                response.setExtra(1);
            }
            session->asyncWrite(response); // "pong" the auth server
        }

        inline void handlePlayerCountRequest(const Common::Network::UnecryptedPacket& request, std::shared_ptr<Common::Network::Session> session,
            Main::Network::SessionsManager& sessionsManager)
        {
            auto response = request;
            const std::uint32_t totalPlayers = sessionsManager.getTotalSessions();
            response.setData(reinterpret_cast<const std::uint8_t*>(&totalPlayers), sizeof(totalPlayers));
            session->asyncWrite(response); 
        }
        
        // on the CastServer's link
        inline bool sendIpToCast(const std::string& ip)
        {
            return Main::Ipc::M2C_passIp(ip);
        }

        inline void handleIpReq(const Common::Network::UnecryptedPacket& request, std::shared_ptr<Common::Network::Session> session)
        {
            if (Main::Ipc::CastLink::instance().isLink(session))
            { // an IP comes from the AuthServer; from the Cast's link it is an echo
                ::Utils::Logger::log("[handleIpReq] passIp on the CastServer's link ignored (an echo)", ::Utils::LogType::Warning);
                return;
            }
            auto response = request;
            const std::string ip = reinterpret_cast<const char*>(request.getData());
            sendIpToCast(ip);
            if (session->s_loggedIps.find(ip) == session->s_loggedIps.end())
            {
                if (session->s_loggedIps.size() >= (Common::Constants::maxSessionsPerServer * 4))
                {
                    const std::string& oldestIp = session->ipQueue.front();
                    session->s_loggedIps.erase(oldestIp);
                    session->ipQueue.pop_front();
                }

                session->s_loggedIps.insert(ip);
                session->ipQueue.push_back(ip);
            }
        }
	}
}

#endif