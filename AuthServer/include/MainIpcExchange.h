#ifndef AUTH_MAIN_IPC_EXCHANGE_H
#define AUTH_MAIN_IPC_EXCHANGE_H

#include <chrono>
#include <cstdint>
#include <optional>
#include <vector>
#include <Utils/SetupParser.h>

namespace Auth::Utils
{
	std::optional<std::vector<std::uint8_t>> exchangeWithMain(const Common::Utils::MainSetup& server, const std::vector<std::uint8_t>& request,
		std::chrono::milliseconds timeout);
}

#endif
