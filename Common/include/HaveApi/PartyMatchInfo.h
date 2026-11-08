/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#ifndef COMMON_HAVE_API_PARTY_MATCH_INFO_H
#define COMMON_HAVE_API_PARTY_MATCH_INFO_H

#include <cstdint>
#include "Utils/Macros.h"

// MainServer -> CastServer (Common::Constants::M2C_partyMatchInfo, no answer): the party game and round of the room of
// the IPC session, so that CastServer writes its party events to the same mode log (HaveApi/ModeLog.h).
namespace Common::HaveApi
{
PACK_PUSH(1)
	struct PartyMatchInfo
	{
		std::int32_t modeId{};    // PartyModeId
		std::uint32_t round{};
		std::uint32_t roomNumber{};
		std::uint8_t running{};   // 0: the match is over
		std::uint8_t reserved[3]{};
	};
PACK_POP()

	static_assert(sizeof(PartyMatchInfo) == 16);
}

#endif
