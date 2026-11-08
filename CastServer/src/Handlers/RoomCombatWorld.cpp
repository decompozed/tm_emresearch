/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#include "Handlers/RoomCombatWorld.h"
#include <chrono>
#include <format>
#include <string>
#include <directxmath/DirectXPackedVector.h>
#include <Utils/Logger.h>
#include "Combat/Validation.h"
#include "Enums/RoomEnums.h"
#include "Handlers/PartyLogging.h"
#include "Network/IpcClient.h"
#include "Network/SessionsManager.h"

namespace Cast::Handlers
{
    namespace
    {
        std::string_view causeName(Combat::Cause cause)
        {
            return cause == Combat::Cause::Explosive ? "explosive / fall" : "weapon";
        }

        bool mustBroadcastDeath(std::uint32_t mode)
        {
            using namespace Common::Enums;
            return mode == Elimination || mode == CaptureTheBattery || mode == BombBattle || mode == Clan_BombBattle
                || mode == Clan_CaptureTheBattery || mode == Clan_Elimination || mode == ZombieMode;
        }
    }

    void reportPartyCombatEvent(ServerContext& ctx, std::uint32_t mode, const Common::HaveApi::CombatEvent& event)
    {
        const bool scrimmageDeath = mode == Common::Enums::Scrimmage && event.kind == Common::HaveApi::CombatEventKind::Death;
        if (mode == Common::Enums::SquareMode || scrimmageDeath)
        {
            ctx.ipc.sendPartyCombatEvent(event);
        }
    }

    void reportServerDeath(ServerContext& ctx, std::uint32_t victimAccountId)
    {
        Common::HaveApi::CombatEvent event{};
        event.victimAccountId = victimAccountId;
        event.kind = Common::HaveApi::CombatEventKind::Death;
        event.cause = Common::HaveApi::DeathCause::Server;
        ctx.ipc.sendPartyCombatEvent(event);
    }

    std::optional<Combat::PlayerState> RoomCombatWorld::player(std::uint32_t session) const
    {
        const auto player = m_room.findPlayer(session);
        if (!player)
        {
            return std::nullopt;
        }
        const bool stale = player->isDead() && std::chrono::steady_clock::now() - player->deadSince() >= Combat::staleDeathDelay;
        return Combat::PlayerState{ player->isInMatch(), player->getTeam() == Common::Enums::TEAM_OBSERVER, player->isDead(), stale };
    }

    bool RoomCombatWorld::sparesPlayers() const
    {
        return m_room.getMode() == Common::Enums::SquareMode && m_room.lava().protectsPlayers();
    }

    void RoomCombatWorld::rememberExplosive(std::uint32_t ammoId, std::uint32_t ownerSession)
    {
        if (m_room.getMode() == Common::Enums::SquareMode)
        {
            m_room.rememberExplosive(ammoId, ownerSession);
        }
    }

    Common::HaveApi::CombatEvent RoomCombatWorld::event(const Network::Session& target, std::optional<std::uint32_t> attacker,
        Combat::Cause cause, Common::HaveApi::CombatEventKind kind) const
    {
        Common::HaveApi::CombatEvent event{};
        event.victimAccountId = target.getAccountId();
        event.kind = kind;
        event.cause = cause == Combat::Cause::Explosive ? Common::HaveApi::DeathCause::Explosive : Common::HaveApi::DeathCause::Weapon;
        if (attacker && *attacker != target.getId())
        {
            if (const auto attackerSession = m_ctx.sessions.getSession(*attacker))
            {
                event.attackerAccountId = attackerSession->getAccountId();
            }
        }
        return event;
    }

    void RoomCombatWorld::hit(std::uint32_t target, std::optional<std::uint32_t> attacker, Combat::Cause cause, bool spared)
    {
        if (const auto player = m_room.findPlayer(target))
        {
            const auto kind = spared ? Common::HaveApi::CombatEventKind::SparedHit : Common::HaveApi::CombatEventKind::Hit;
            reportPartyCombatEvent(m_ctx, m_room.getMode(), event(*player, attacker, cause, kind));
            if (m_room.getMode() == Common::Enums::SquareMode)
            {
                m_room.partyLog().hits().add(attacker.value_or(0), target, cause == Combat::Cause::Explosive ?
                    Common::HaveApi::DeathCause::Explosive : Common::HaveApi::DeathCause::Weapon, spared);
            }
            if (spared)
            {
                logParty(m_room, std::format("cast spared hit ({}): {} left at 1 hit point, attacker {}", causeName(cause),
                    describePlayer(m_room, *player), attacker ? describeSession(m_room, *attacker) : std::string{ "unknown" }));
            }
        }
    }

    void RoomCombatWorld::death(std::uint32_t victim, std::optional<std::uint32_t> killer, Combat::Cause cause)
    {
        const auto player = m_room.findPlayer(victim);
        if (!player)
        {
            return;
        }
        const bool wasDead = player->isDead();
        player->setDead(true);
        reportPartyCombatEvent(m_ctx, m_room.getMode(), event(*player, killer, cause, Common::HaveApi::CombatEventKind::Death));
        logParty(m_room, std::format("cast death ({}{}): {}, killer {}", causeName(cause), wasDead ? ", was already dead for the CastServer" : "",
            describePlayer(m_room, *player), killer && *killer != victim ? describeSession(m_room, *killer) : std::string{ "none" }));
        if (mustBroadcastDeath(m_room.getMode()))
        {
            m_ctx.ipc.sendPlayerStateUpdate(player->getAccountId(), true);
        }
    }

    void RoomCombatWorld::respawn(std::uint32_t host, std::uint32_t player, const Structures::Half3& position)
    {
        if (const auto hostSession = m_room.findPlayer(host))
        {
            hostSession->setInMatch(true);
        }
        const auto target = m_room.findPlayer(player);
        if (!target)
        {
            return;
        }

        Common::HaveApi::CombatEvent event{};
        if (m_room.getMode() == Common::Enums::SquareMode && Combat::isFinite(position.x) && Combat::isFinite(position.y))
        {
            event.hasPosition = 1;
            event.x = DirectX::PackedVector::XMConvertHalfToFloat(position.x);
            event.y = DirectX::PackedVector::XMConvertHalfToFloat(position.y);
            for (auto& zone : m_room.zones())
            {
                zone.onRespawn(player, event.x, event.y);
            }
        }
        const bool wasDead = target->isDead();
        target->setDead(false);
        logParty(m_room, std::format("cast respawn of {} at ({:.1f}, {:.1f}){}", describePlayer(m_room, *target),
            event.hasPosition ? event.x : 0.0f, event.hasPosition ? event.y : 0.0f, wasDead ? "" : ", was alive for the CastServer"));
        if (target->getTeam() == Common::Enums::TEAM_OBSERVER)
        {
            return;
        }

        target->setInMatch(true);
        event.victimAccountId = target->getAccountId();
        event.kind = Common::HaveApi::CombatEventKind::Respawn;
        reportPartyCombatEvent(m_ctx, m_room.getMode(), event);
        if (mustBroadcastDeath(m_room.getMode()))
        {
            m_ctx.ipc.sendPlayerStateUpdate(target->getAccountId(), false);
        }
    }

    void RoomCombatWorld::revive(std::uint32_t player)
    {
        const auto target = m_room.findPlayer(player);
        if (!target || !target->isDead())
        {
            return;
        }
        target->setDead(false);
        ::Utils::Logger::log("[AntiCheat] " + target->nickname() + " (session " + std::to_string(player) + ") acts " +
            std::to_string(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - target->deadSince()).count()) +
            " s after its recorded death: alive again for the CastServer", ::Utils::LogType::Warning, "Cast::RoomCombatWorld");
        logParty(m_room, "cast revive: " + describePlayer(m_room, *target) + " acts after its recorded death, alive again for the CastServer");
        if (mustBroadcastDeath(m_room.getMode()))
        {
            m_ctx.ipc.sendPlayerStateUpdate(target->getAccountId(), false);
        }
    }

    std::string_view RoomCombatWorld::nickname(std::uint32_t session) const
    {
        const auto player = m_room.findPlayer(session);
        return player ? std::string_view{ player->nickname() } : std::string_view{};
    }

    bool RoomCombatWorld::send(const Combat::Outgoing& outgoing)
    {
        Common::Network::UnecryptedPacket packet;
        packet.setCommand(outgoing.order, outgoing.mission, outgoing.extra, outgoing.option);
        packet.setData(outgoing.payload.data(), static_cast<std::uint16_t>(outgoing.payload.size()));
        switch (outgoing.route)
        {
        case Combat::Route::Match:
            m_room.broadcastToMatch(packet);
            return true;
        case Combat::Route::MatchExceptSender:
            m_room.broadcastToMatchExceptSelf(packet, outgoing.sender);
            return true;
        case Combat::Route::Recipient:
            return m_room.forwardToPlayer(outgoing.recipient, outgoing.recipient, packet);
        default:
            return false;
        }
    }

    void RoomCombatWorld::leaveMatch(std::uint32_t session)
    {
        if (const auto player = m_room.findPlayer(session))
        {
            player->setInMatch(false);
            logParty(m_room, "cast match leave: " + describePlayer(m_room, *player));
        }
    }

    void RoomCombatWorld::finishLoading(std::uint32_t session)
    {
        const auto player = m_room.findPlayer(session);
        if (!player)
        {
            return;
        }
        if (mustBroadcastDeath(m_room.getMode()))
        {
            m_ctx.ipc.sendPlayerStateUpdate(player->getAccountId(), true);
        }
        player->setDead(true);
        player->setInMatch(true);
        logParty(m_room, "cast loaded (dead until its first spawn): " + describePlayer(m_room, *player));
    }
}
