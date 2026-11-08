/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#ifndef COMMON_HAVE_API_TICK_RATE_H
#define COMMON_HAVE_API_TICK_RATE_H

#include <atomic>
#include <cstdint>

// Network tick rate: N updates per second.
//  - client: have_api message 0x1E with rerouting off, sent at login: the DLL only sets the game's tick rate
//    (send interval 1/N, interpolation window 0.8/N); nothing goes over UDP;
//  - CastServer: the position batches go out every 1000/N ms (told by MainServer at every match start, IPC M2C_tickRate).
// Stored in the database (ServerSettings 'TickRate', created once with the default); /settickrate changes it and
// restarts the servers.
namespace Common::HaveApi
{
	inline constexpr std::uint32_t defaultTickRate = 10; // the game's own value (interpolation window 0.08 = 0.8 / 10)
	inline constexpr std::uint32_t minTickRate = 5;
	inline constexpr std::uint32_t maxTickRate = 60;

	[[nodiscard]] constexpr bool isValidTickRate(std::uint32_t rate) noexcept
	{
		return rate >= minTickRate && rate <= maxTickRate;
	}

	[[nodiscard]] constexpr std::uint32_t tickMilliseconds(std::uint32_t rate) noexcept
	{
		return 1000 / (isValidTickRate(rate) ? rate : defaultTickRate);
	}

	// The process's current tick rate (MainServer: read from the database at the first login; CastServer: from MainServer).
	inline std::atomic<std::uint32_t> currentTickRate{ defaultTickRate };
}

#endif
