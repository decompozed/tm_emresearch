/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#include "Handlers/ItemHandlers.h"
#include "Combat/Rules.h"

namespace Cast::Handlers
{
    void ItemHandlers::registerRoutes(Network::PacketRouter<Network::Session>& router)
    {
        m_rules.route(Combat::itemPickupRequest, router, { 96 });
        m_rules.route(Combat::itemPickup, router, { 262 });
        m_rules.route(Combat::itemUseRequest, router, { 102 });
        m_rules.route(Combat::itemUse, router, { 263 });
        m_rules.route(Combat::mapItemSpawn, router, { 260 });
        m_rules.route(Combat::itemGrant, router, { 261 });
        m_rules.route(Combat::bombActionRequest, router, { 155 });
        m_rules.route(Combat::bombAction, router, { 271 });
        m_rules.route(Combat::bombDropRequest, router, { 156 });
        m_rules.route(Combat::mapItemSnapshot, router, { 90 });
        m_rules.route(Combat::inventorySnapshot, router, { 94 });
        m_rules.route(Combat::effectSnapshot, router, { 78 });
    }
}
