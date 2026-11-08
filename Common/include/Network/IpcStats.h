#ifndef COMMON_NETWORK_IPC_STATS_H
#define COMMON_NETWORK_IPC_STATS_H

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <asio.hpp>
#include "Utils/Logger.h"

namespace Common::Network
{
	// IPC packets received, by order (an echo loop between the Main and the Cast servers would otherwise go unnoticed).
	// Session::onPacket (the IPC path of the three servers) counts them; once a minute the server logs the orders received
	// in the last minute, an error for any order above loopWarningPerMinute.
	class IpcStats
	{
	public:
		static constexpr std::size_t orders = 64;                       // the last slot counts every larger order
		static constexpr std::uint64_t loopWarningPerMinute = 6000;    // 100 a second: no IPC order comes that often

		static void count(std::uint16_t order)
		{
			counters()[order < orders ? order : orders - 1].fetch_add(1, std::memory_order_relaxed);
		}

		// Every minute on `io` (a timer: no thread of its own)
		static void startReporter(asio::io_context& io)
		{
			auto timer = std::make_shared<asio::steady_timer>(io);
			schedule(timer);
		}

	private:
		static std::array<std::atomic<std::uint64_t>, orders>& counters()
		{
			static std::array<std::atomic<std::uint64_t>, orders> values{};
			return values;
		}

		static void schedule(std::shared_ptr<asio::steady_timer> timer)
		{
			timer->expires_after(std::chrono::minutes{ 1 });
			timer->async_wait([timer](const asio::error_code& error) {
				if (error)
				{
					return;
				}
				report();
				schedule(timer);
			});
		}

		static void report()
		{
			std::string line;
			std::string loops;
			for (std::size_t order = 0; order < orders; ++order)
			{
				const std::uint64_t n = counters()[order].exchange(0, std::memory_order_relaxed);
				if (n == 0)
				{
					continue;
				}
				const std::string entry = (order == orders - 1 ? ">=" : "") + std::to_string(order) + ":" + std::to_string(n);
				line += " " + entry;
				if (n > loopWarningPerMinute)
				{
					loops += " " + entry;
				}
			}
			if (!line.empty())
			{
				::Utils::Logger::log("[IpcStats] IPC packets received in the last minute (order:count):" + line, ::Utils::LogType::Info);
			}
			if (!loops.empty())
			{
				::Utils::Logger::log("[IpcStats] POSSIBLE IPC LOOP, more than " + std::to_string(loopWarningPerMinute) +
					" a minute:" + loops, ::Utils::LogType::Error);
			}
		}
	};
}

#endif
