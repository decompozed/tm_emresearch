/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#include "Handlers/RuleDispatcher.h"
#include <array>
#include <format>
#include "Combat/PacketUnits.h"
#include "Combat/Validation.h"
#include "Enums/RoomEnums.h"
#include "Handlers/PartyLogging.h"
#include "Handlers/RoomCombatWorld.h"
#include "Managers/RoomsManager.h"
#include "Structures/Combat/CombatPackets.h"

namespace Cast::Handlers
{
    namespace
    {
        void applyVerdict(Common::Network::UnecryptedPacket& packet, const Combat::Verdict& verdict)
        {
            packet.setOption(verdict.option);
            if (verdict.size != packet.getDataSize())
            {
                std::array<std::uint8_t, Structures::maxPayloadSize> data{};
                std::copy_n(packet.getData(), verdict.size, data.begin());
                packet.setData(data.data(), static_cast<std::uint16_t>(verdict.size));
            }
        }

        std::string describeUnit(Domain::Room& room, std::uint16_t serverId, const Structures::UniqueId& id)
        {
            if (id.server == serverId && room.findPlayer(id.session))
            {
                return describeSession(room, id.session);
            }
            return std::format("unit {} (not a player of this room: npc or unknown)", Combat::rawId(id));
        }

        std::string describeUnits(Domain::Room& room, std::uint16_t serverId, const Combat::Packet& packet)
        {
            const auto units = Combat::packetUnits(packet);
            std::string attacker = "the sender";
            if (units.attacker)
            {
                attacker = describeUnit(room, serverId, *units.attacker);
            }
            else if (units.ammoId)
            {
                const auto owner = room.explosiveOwner(*units.ammoId);
                attacker = owner ? "owner of explosive " + std::to_string(*units.ammoId) + ": " + describeSession(room, *owner)
                    : "explosive " + std::to_string(*units.ammoId) + " of an unknown owner";
            }
            return "attacker " + attacker + (units.target ? ", target " + describeUnit(room, serverId, *units.target) : std::string{});
        }
    }

    void RuleDispatcher::route(Rule rule, Network::PacketRouter<Network::Session>& router, std::initializer_list<std::uint16_t> orders)
    {
        router.on(orders, [this, rule](const Packet& request, std::shared_ptr<Network::Session> session) { handle(rule, const_cast<Packet&>(request), *session); });
    }

    void RuleDispatcher::handle(Rule rule, Packet& request, Network::Session& session)
    {
        const auto room = m_ctx.rooms.getRoom(session.getId());
        if (!room)
        {
            logRejected(session, request, "sender is not in a room");
            return;
        }

        RoomCombatWorld world{ m_ctx, *room };
        const Combat::Packet packet{ static_cast<std::uint32_t>(session.getId()), request.getOrder(), request.getMission(), request.getExtra(),
            request.getOption(), { request.getMutableData(), request.getDataSize() }, request.getSession() };
        const auto verdict = rule(world, packet);
        switch (verdict.route)
        {
        case Combat::Route::Drop:
            logRejected(session, request, verdict.reason);
            logPartyDrop(*room, session, packet, verdict.reason);
            return;
        case Combat::Route::Match:
            applyVerdict(request, verdict);
            room->broadcastToMatch(request);
            return;
        case Combat::Route::MatchExceptSender:
            applyVerdict(request, verdict);
            room->broadcastToMatchExceptSelf(request, packet.sender);
            return;
        case Combat::Route::Host:
            if (!room->forwardToHost(packet.sender, request))
            {
                logRejected(session, request, "room host unknown");
                logPartyDrop(*room, session, packet, "room host unknown");
            }
            return;
        case Combat::Route::Recipient:
            applyVerdict(request, verdict);
            if (!room->forwardToPlayer(packet.sender, packet.target, request))
            {
                logRejected(session, request, "recipient not in the room");
                logPartyDrop(*room, session, packet, "recipient not in the room");
            }
            return;
        case Combat::Route::Handled:
            return;
        }
    }

    void RuleDispatcher::logRejected(Network::Session& session, const Packet& request, std::string_view reason)
    {
        session.logDropped(request, reason);
    }

    void RuleDispatcher::logPartyDrop(Domain::Room& room, Network::Session& sender, const Combat::Packet& packet, std::string_view reason)
    {
        if (room.getMode() != Common::Enums::SquareMode)
        {
            return;
        }
        const auto units = Combat::packetUnits(packet);
        const std::string key = std::format("{}|{}|{}|{}", reason, packet.sender, units.attacker ? Combat::rawId(*units.attacker) : 0,
            units.ammoId.value_or(0));
        std::uint32_t suppressed = 0;
        if (!room.partyLog().allowDrop(key, Domain::PartyLog::Clock::now(), suppressed))
        {
            return;
        }
        logParty(room, std::format("cast dropped order {} (mission {}, extra {}, option {}, {} bytes): {}; sender {}; {}{}", packet.order,
            packet.mission, packet.extra, packet.option, packet.payload.size(), reason, describePlayer(room, sender),
            describeUnits(room, m_ctx.serverId, packet), suppressed ? std::format(" ({} more like it not logged)", suppressed) : std::string{}));
    }
}
