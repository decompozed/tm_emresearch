/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#include "Handlers/UnitHandlers.h"
#include "Combat/Rules.h"

namespace Cast::Handlers
{
    void UnitHandlers::registerRoutes(Network::PacketRouter<Network::Session>& router)
    {
        m_rules.route(Combat::respawnRequest, router, { 166 });
        m_rules.route(Combat::playerRespawn, router, { 276 });
        m_rules.route(Combat::botMove, router, { 285 });
        m_rules.route(Combat::botRespawn, router, { 286 });
        m_rules.route(Combat::npcMoves, router, { 282 });
        m_rules.route(Combat::npcProjectile, router, { 326 });
        m_rules.route(Combat::monsterSpawn, router, { 328 });
        m_rules.route(Combat::bossSchedule, router, { 331 });
    }
}
