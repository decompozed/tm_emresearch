/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#include "Handlers/ConnectionHandlers.h"
#include "Connection/ConnectionRules.h"
#include "Managers/RoomsManager.h"
#include "Network/SessionsManager.h"
#include "Network/IpcClient.h"
#include <Utils/Logger.h>
#include <string>

namespace Cast::Handlers
{
    namespace
    {
        Connection::Command commandOf(const Common::Network::UnecryptedPacket& packet)
        {
            return { static_cast<std::uint16_t>(packet.getOrder()), static_cast<std::uint8_t>(packet.getMission()),
                static_cast<std::uint8_t>(packet.getExtra()), static_cast<std::uint8_t>(packet.getOption()),
                { packet.getData(), packet.getDataSize() } };
        }

        std::string describe(const std::optional<Common::HaveApi::CastConnectAnswer>& answer)
        {
            return answer ? "verdict " + std::to_string(static_cast<int>(answer->verdict)) + ", session " + std::to_string(answer->session)
                          : std::string{ "no answer" };
        }
    }

    void ConnectionHandlers::registerRoutes(Network::PacketRouter<Network::Session>& router)
    {
        router.on(Connection::connectOrder, [this](const Packet& req, SessionPtr s) { onConnect(req, s); });
        router.on(Connection::pingOrder, [this](const Packet& req, SessionPtr s) { onPing(req, s); });
        router.on(Connection::crashOrder, [this](const Packet& req, SessionPtr s) { onCrash(req, s); });
    }

    void ConnectionHandlers::onConnect(const Packet& request, SessionPtr session)
    {
        const auto connect = Connection::parseConnect(commandOf(request));
        if (!connect)
        {
            session->logDropped(request, "malformed 252");
            return;
        }
        session->setStage(Connection::Stage::Verifying);
        m_ctx.ipc.verifyConnect(Connection::toCheck(*connect),
            [this, weak = std::weak_ptr<Network::Session>(session), connect = *connect](std::optional<Common::HaveApi::CastConnectAnswer> answer) {
                onVerified(weak.lock(), connect, answer);
            });
    }

    void ConnectionHandlers::onVerified(SessionPtr session, const Structures::ConnectRequest& request,
        const std::optional<Common::HaveApi::CastConnectAnswer>& answer)
    {
        const auto verified = Connection::verifiedSession(answer, request);
        const auto holder = verified ? m_ctx.sessions.getSession(*verified) : nullptr;
        const bool open = session && session->is_open();
        switch (Connection::connectOutcome(open, open ? session->stage() : Connection::Stage::AwaitingConnect, verified, holder && holder != session))
        {
        case Connection::Outcome::Ignore:
            return;
        case Connection::Outcome::Reject:
            ::Utils::Logger::log("[AntiCheat] 252 of account " + std::to_string(request.accountId) + " from session " + std::to_string(session->getId()) +
                " refused (" + describe(answer) + ")", ::Utils::LogType::Warning, "Cast::onConnect");
            session->closeSocket();
            return;
        case Connection::Outcome::ReplaceHolder:
            ::Utils::Logger::log("Session " + std::to_string(*verified) + " connected again: previous socket closed", ::Utils::LogType::Info, "Cast::onConnect");
            holder->closeSocket();
            m_ctx.sessions.removeSession(*verified);
            authenticate(session, *verified, request.accountId);
            return;
        case Connection::Outcome::Register:
            authenticate(session, *verified, request.accountId);
            return;
        }
    }

    void ConnectionHandlers::authenticate(const SessionPtr& session, std::uint16_t sessionId, std::uint32_t accountId)
    {
        session->setAccountId(accountId);
        m_ctx.sessions.addSession(session, sessionId);
        session->setStage(Connection::Stage::Authenticated);
        session->writeCommand(Structures::castConnected);
    }

    void ConnectionHandlers::onPing(const Packet& request, SessionPtr session)
    {
        if (!Connection::isPing(commandOf(request)))
        {
            session->logDropped(request, "malformed 71");
            return;
        }
        session->writeCommand(Structures::pong);
    }

    void ConnectionHandlers::onCrash(const Packet& request, SessionPtr session)
    {
        if (!Connection::isCrashReport(commandOf(request)))
        {
            session->logDropped(request, "malformed 253");
            return;
        }
        session->setInMatch(false);
        m_ctx.rooms.removePlayer(session->getId());
    }
}
