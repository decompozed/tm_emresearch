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
#include <memory>
#include "HaveApi/ModeLog.h"
#include "Utils/Logger.h"

namespace Common::HaveApi
{
	namespace
	{
		std::unique_ptr<ModeLog> openProcessLog()
		{
			const char* dir = std::getenv("TB_LOG_DIR");
			if (dir == nullptr || *dir == '\0')
			{
				return nullptr;
			}
			return std::make_unique<ModeLog>(std::filesystem::path{ dir } / "modes", ::Utils::Logger::processName());
		}
	}

	void writeModeLog(std::int32_t modeId, std::uint32_t room, std::uint32_t round, std::string_view event)
	{
		static const std::unique_ptr<ModeLog> log = openProcessLog();
		if (log)
		{
			log->write(modeId, room, round, event);
		}
	}
}
