/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#include "HaveApi/ModeLog.h"
#include <format>

namespace Common::HaveApi
{
	ModeLog::ModeLog(std::filesystem::path directory, std::string server, int keepDays)
		: m_files{ std::move(directory), keepDays }
		, m_server{ std::move(server) }
	{
	}

	std::string ModeLog::line(std::string_view server, std::uint32_t room, std::uint32_t round, std::string_view event,
		::Utils::DailyLogFiles::Clock::time_point now)
	{
		const auto ms = std::chrono::floor<std::chrono::milliseconds>(now);
		const auto day = std::chrono::floor<std::chrono::days>(ms);
		const std::chrono::year_month_day date{ day };
		const std::chrono::hh_mm_ss time{ ms - day };
		return std::format("{:04}-{:02}-{:02} {:02}:{:02}:{:02}.{:03} [{}] room {} round {} | {}\n", static_cast<int>(date.year()),
			static_cast<unsigned>(date.month()), static_cast<unsigned>(date.day()), time.hours().count(), time.minutes().count(),
			time.seconds().count(), time.subseconds().count(), server, room, round, event);
	}

	void ModeLog::write(std::int32_t modeId, std::uint32_t room, std::uint32_t round, std::string_view event,
		::Utils::DailyLogFiles::Clock::time_point now)
	{
		m_files.write(modeLogName(modeId), line(m_server, room, round, event, now), now);
	}
}
