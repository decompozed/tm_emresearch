/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#include <cstdlib>
#include <filesystem>
#include <memory>
#include "Utils/SecurityEvent.h"
#include "Utils/DailyLogFiles.h"
#include "Utils/Logger.h"

namespace Common::Utils::SecurityEvent
{
	namespace
	{
		struct Sink
		{
			std::unique_ptr<::Utils::DailyLogFiles> files;
			std::string salt;
		};

		Sink openSink()
		{
			Sink sink;
			const char* dir = std::getenv("TB_LOG_DIR");
			if (dir != nullptr && *dir != '\0')
			{
				sink.files = std::make_unique<::Utils::DailyLogFiles>(std::filesystem::path{ dir } / "security", 90);
			}
			if (const char* salt = std::getenv("TB_LOG_SALT"); salt != nullptr)
			{
				sink.salt = salt;
			}
			return sink;
		}
	}

	void emit(std::string_view event, std::string_view severity, std::uint32_t accountId, std::string_view ip,
		std::string_view result, std::string_view reason, std::string_view corrId) noexcept
	{
		try
		{
			static const Sink sink = openSink();
			if (!sink.files)
			{
				return;
			}
			const std::string server = ::Utils::Logger::processName();
			const std::string src = hashSource(ip, sink.salt);
			const std::string line = formatLine(Event{ event, severity, accountId, src, result, reason, corrId }, server, std::chrono::system_clock::now());
			sink.files->write(server.empty() ? "server" : server, line);
		}
		catch (...)
		{
			// a failing security log must not take the server down
		}
	}
}
