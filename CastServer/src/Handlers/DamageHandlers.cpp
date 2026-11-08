/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#include "Handlers/DamageHandlers.h"
#include "Combat/Rules.h"

namespace Cast::Handlers
{
    void DamageHandlers::registerRoutes(Network::PacketRouter<Network::Session>& router)
    {
        m_rules.route(Combat::explosionDamage, router, { 264 });
        m_rules.route(Combat::effectOutcome, router, { 265 });
        m_rules.route(Combat::multiHit, router, { 266, 269 });
        m_rules.route(Combat::singleHit, router, { 267, 268, 270 });
        m_rules.route(Combat::projectileLaunch, router, { 272 });
        m_rules.route(Combat::reload, router, { 275 });

        m_rules.route(Combat::itemDamageRequest, router, { 146 });
        m_rules.route(Combat::multiHitRequest, router, { 147, 153 });
        m_rules.route(Combat::singleHitRequest, router, { 149, 152, 154 });
        m_rules.route(Combat::projectileRequest, router, { 151 });
        m_rules.route(Combat::reloadRequest, router, { 165 });
    }
}
