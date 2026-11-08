/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#include "Handlers/PositionHandlers.h"
#include <array>
#include <format>
#include <string>
#include <directxmath/DirectXPackedVector.h>
#include <Utils/Logger.h>
#include "Enums/RoomEnums.h"
#include "Handlers/PartyLogging.h"
#include "Handlers/RoomCombatWorld.h"
#include "Managers/RoomsManager.h"
#include "Network/IpcClient.h"
#include "Position/PositionCodec.h"

namespace Cast::Handlers
{
    namespace
    {
        constexpr std::uint16_t playerPositionOrder = 281;
    }

    void PositionHandlers::registerRoutes(Network::PacketRouter<Network::Session>& router)
    {
        router.on(playerPositionOrder, [this](const Packet& request, SessionPtr session) { onPlayerPosition(request, session); });
    }

    void PositionHandlers::onPlayerPosition(const Packet& request, const SessionPtr& session)
    {
        const auto room = m_ctx.rooms.getRoom(session->getId());
        if (!room)
        {
            return;
        }

        Structures::PlayerPosition position{};
        if (const auto reason = Position::decode(request.getMission(), request.getExtra(), request.getOption(),
            { request.getData(), request.getDataSize() }, position); !reason.empty())
        {
            session->logDropped(request, reason);
            return;
        }

        session->setLastPosition(position.report.position);
        if (killedByPartyArea(*room, *session, position.report.position))
        {
            return;
        }

        std::array<std::uint8_t, Structures::maxPositionEntrySize> entry{};
        const auto size = Position::encode(entry, static_cast<std::uint32_t>(session->getId()), position, room->isInvisible() || session->isInvisible());
        room->enqueuePosition({ entry.data(), size });
    }

    bool PositionHandlers::killedByPartyArea(Domain::Room& room, Network::Session& session, const Structures::Half3& at)
    {
        if (room.getMode() != Common::Enums::SquareMode || !session.isInMatch() || session.getTeam() == Common::Enums::TEAM_OBSERVER)
        {
            return false;
        }
        const auto sessionId = static_cast<std::uint32_t>(session.getId());
        const float x = DirectX::PackedVector::XMConvertHalfToFloat(at.x);
        const float y = DirectX::PackedVector::XMConvertHalfToFloat(at.y);
        const float z = DirectX::PackedVector::XMConvertHalfToFloat(at.z);
        room.lava().setPosition(sessionId, x, y, z);
        updateZones(room, sessionId, x, y, z);
        updateStorm(room, sessionId, x, y);
        return killedByLava(room, session, x, y, z);
    }

    void PositionHandlers::updateZones(Domain::Room& room, std::uint32_t sessionId, float x, float y, float z)
    {
        for (auto& zone : room.zones())
        {
            if (Common::HaveApi::ZoneAnswer occupants{}; zone.updateOccupant(sessionId, x, y, z, occupants))
            {
                m_ctx.ipc.sendPartyZoneOccupants(sessionId, occupants);
                logParty(room, std::format("cast zone {} occupants now: {} (after the position ({:.1f}, {:.1f}, {:.1f}) of session {})", occupants.index,
                    describeSessions(room, listedSessions(occupants)), x, y, z, sessionId));
            }
        }
    }

    void PositionHandlers::updateStorm(Domain::Room& room, std::uint32_t sessionId, float x, float y)
    {
        if (Common::HaveApi::StormAnswer outside{}; room.storm().update(sessionId, x, y, outside))
        {
            m_ctx.ipc.sendPartyStormOutside(sessionId, outside);
            logParty(room, std::format("cast storm outside now: {} (after the position ({:.1f}, {:.1f}) of session {})",
                describeSessions(room, listedSessions(outside)), x, y, sessionId));
        }
    }

    bool PositionHandlers::killedByLava(Domain::Room& room, Network::Session& session, float x, float y, float z)
    {
        const auto sessionId = static_cast<std::uint32_t>(session.getId());
        const char* reason = nullptr;
        if (!room.lava().reaches(sessionId, x, y, z, reason) || !room.killForAll(sessionId))
        {
            return false;
        }
        const auto lava = room.lava().state();
        ::Utils::Logger::log("[HaveApi] lava kill (" + std::string{ reason } + "): " + session.nickname() + " at (" +
            std::to_string(x) + ", " + std::to_string(y) + ", " + std::to_string(z) + "), floor " + std::to_string(lava.floorZ) +
            ", walls x [" + std::to_string(lava.wallX1) + ", " + std::to_string(lava.wallX2) + "] y [" +
            std::to_string(lava.wallY1) + ", " + std::to_string(lava.wallY2) + "]");
        logParty(room, std::format("cast lava kill ({}): {} at ({:.1f}, {:.1f}, {:.1f}); lava {}", reason, describePlayer(room, session), x, y, z,
            describeLava(lava)));
        reportServerDeath(m_ctx, session.getAccountId());
        return true;
    }
}
