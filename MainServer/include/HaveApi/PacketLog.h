/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#ifndef MAIN_HAVE_API_PACKET_LOG_H
#define MAIN_HAVE_API_PACKET_LOG_H

#include <chrono>
#include <cstdint>
#include <format>
#include <fstream>
#include <mutex>
#include <span>
#include <string>
#include <string_view>

namespace Main::HaveApi
{
	enum class Direction { ServerToClient, ClientToServer };

	// Appends every have_api message, in both directions, to have_api_packets.log (working directory):
	// time, direction, session, player, message id, size and the raw bytes after the length prefix.
	class PacketLog
	{
	public:
		static void write(Direction direction, std::size_t sessionId, std::string_view player, std::uint16_t messageId,
			std::span<const std::uint8_t> message)
		{
			std::string line = std::format("[{:%F %T}] {} session={} player=\"{}\" id=0x{:02X} size={}\n  ",
				std::chrono::zoned_time{ std::chrono::current_zone(), std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()) },
				direction == Direction::ServerToClient ? "S->C" : "C->S", sessionId, player, messageId, message.size());
			for (const std::uint8_t byte : message)
			{
				line += std::format("{:02X} ", byte);
			}
			line += '\n';

			const std::lock_guard lock{ mutex() };
			stream() << line << std::flush;
		}

		// A game packet of the normal match flow (orders 107, 254, 258, 415...), captured to reproduce it for party
		// matches: order, mission, extra, option and the data bytes.
		static void writeGamePacket(std::string_view label, std::size_t sessionId, std::string_view player, std::uint32_t order,
			std::uint32_t mission, std::uint32_t extra, std::uint32_t option, std::span<const std::uint8_t> data)
		{
			std::string line = std::format("[{:%F %T}] GAME {} session={} player=\"{}\" order={} mission={} extra={} option={} size={}\n  ",
				std::chrono::zoned_time{ std::chrono::current_zone(), std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()) },
				label, sessionId, player, order, mission, extra, option, data.size());
			for (const std::uint8_t byte : data)
			{
				line += std::format("{:02X} ", byte);
			}
			line += '\n';

			const std::lock_guard lock{ mutex() };
			stream() << line << std::flush;
		}

	private:
		static std::mutex& mutex()
		{
			static std::mutex instance;
			return instance;
		}

		static std::ofstream& stream()
		{
			static std::ofstream file{ "have_api_packets.log", std::ios::app };
			return file;
		}
	};
}

#endif
