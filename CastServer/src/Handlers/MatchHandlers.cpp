/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#include "Handlers/MatchHandlers.h"
#include "Match/Rules.h"

namespace Cast::Handlers
{
    void MatchHandlers::registerRoutes(Network::PacketRouter<Network::Session>& router)
    {
        m_rules.route(Match::matchEnd, router, { 254 });
        m_rules.route(Match::guestReady, router, { 255 });
        m_rules.route(Match::matchLeave, router, { 256 });
        m_rules.route(Match::loadingProgress, router, { 257 });
        m_rules.route(Match::matchStart, router, { 258 });
        m_rules.route(Match::matchStop, router, { 259 });

        m_rules.route(Match::joinStateRequest, router, { 79 });
        m_rules.route(Match::hostClock, router, { 408 });
        m_rules.route(Match::joinStats, router, { 306 });
        m_rules.route(Match::unitStates, router, { 305 });
        m_rules.route(Match::modeState, router, { 309 });
        m_rules.route(Match::modeSync, router, { 304 });

        m_rules.route(Match::voiceMessage, router, { 280 });
        m_rules.route(Match::nameHeartbeat, router, { 284 });
        m_rules.route(Match::skillUse, router, { 429 });
    }
}
