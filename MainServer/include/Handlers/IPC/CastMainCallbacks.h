#ifndef CAST_MAIN_IPC_CALLBACKS_H
#define CAST_MAIN_IPC_CALLBACKS_H

#include "Network/Packet.h"
#include "Network/MainSessionManager.h"
#include "Network/Sessions/AuthSession.h"
#include "HaveApi/CastConnect.h"
#include <bit>

namespace Main
{
    namespace Handlers
    {
        inline void verifyCastConnect(const Common::Network::UnecryptedPacket& request, std::shared_ptr<Common::Network::Session> session,
            Main::Network::SessionsManager& sessionsManager)
        {
            Common::HaveApi::CastConnectAnswer answer{};
            if (const auto check = Common::HaveApi::decodeExact<Common::HaveApi::CastConnectCheck>(request.getData(), request.getDataSize()))
            {
                const auto target = sessionsManager.getSessionByAccountId(check->accountId);
                answer.verdict = target && target->is_open()
                    ? Common::HaveApi::verifyCastConnect(*check, target->getAccountInfo().accountKey, std::bit_cast<std::uint32_t>(target->getAccountInfo().uniqueId))
                    : Common::HaveApi::CastConnectVerdict::UnknownAccount;
                if (answer.verdict == Common::HaveApi::CastConnectVerdict::Accepted)
                {
                    answer.session = static_cast<std::uint16_t>(target->getId());
                }
                else
                {
                    ::Utils::Logger::log("[AntiCheat] cast connect of account " + std::to_string(check->accountId) + " refused (verdict " +
                        std::to_string(static_cast<int>(answer.verdict)) + ")", ::Utils::LogType::Warning, "Main::verifyCastConnect");
                }
            }

            Common::Network::UnecryptedPacket response;
            response.setTcpHeader(0);
            response.setCommand(Common::Constants::C2M_castConnect, 0, 0, request.getOption());
            response.setData(reinterpret_cast<const std::uint8_t*>(&answer), sizeof(answer));
            session->asyncWrite(response);
        }

        // No "pong": the CastServer has no use for it on its persistent link
        inline void getPlayerStateUpdate(const Common::Network::UnecryptedPacket& request, std::shared_ptr<Common::Network::Session>,
            Main::Network::SessionsManager& sessionsManager)
        {
            if (request.getDataSize() != sizeof(uint32_t))
            {
                return;
            }

            const std::uint32_t accountId = Main::Details::parseData<std::uint32_t>(request);
            const auto playerState = request.getOption(); 

            if (auto targetSession = sessionsManager.getSessionByAccountId(accountId); targetSession)
            {
                targetSession->setPlayerState(static_cast<Common::Enums::PlayerState>(playerState));
            }
        }
    }
}

#endif