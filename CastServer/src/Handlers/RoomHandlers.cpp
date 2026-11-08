/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#include "Handlers/RoomHandlers.h"
#include "Managers/RoomsManager.h"

namespace Cast::Handlers
{
    namespace
    {
        constexpr std::uint16_t leaveRoomOrder = 279;
    }

    void RoomHandlers::registerRoutes(Network::PacketRouter<Network::Session>& router)
    {
        router.on({ Domain::RoomMembership::createOrder, Domain::RoomMembership::joinOrder },
            [this](const Packet& request, SessionPtr session) { onAttach(request, session); });
        router.on(leaveRoomOrder, [this](const Packet&, SessionPtr session) { onLeave(session); });
    }

    void RoomHandlers::onAttach(const Packet& request, const SessionPtr& session)
    {
        m_ctx.rooms.attach(session, static_cast<std::uint16_t>(request.getOrder()), static_cast<std::uint32_t>(request.getSession()));
    }

    void RoomHandlers::onLeave(const SessionPtr& session)
    {
        m_ctx.rooms.removePlayer(session->getId());
    }
}
