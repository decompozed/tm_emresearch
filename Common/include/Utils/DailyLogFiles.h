/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#ifndef COMMON_UTILS_DAILY_LOG_FILES_H
#define COMMON_UTILS_DAILY_LOG_FILES_H

#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

namespace Utils
{
	// One file per stream and UTC day: <directory>/<stream>-YYYY-MM-DD.log. When a stream's day changes, the files of
	// every stream older than keepDays are removed. Thread-safe; every line is flushed at once.
	class DailyLogFiles
	{
	public:
		using Clock = std::chrono::system_clock;

		explicit DailyLogFiles(std::filesystem::path directory, int keepDays = 30);

		void write(std::string_view stream, std::string_view line, Clock::time_point now = Clock::now());

		[[nodiscard]] const std::filesystem::path& directory() const noexcept { return m_directory; }
		[[nodiscard]] static std::string fileName(std::string_view stream, std::chrono::sys_days day);
		// the day of a file named by fileName(); nothing for any other name
		[[nodiscard]] static std::optional<std::chrono::sys_days> dayOf(std::string_view fileName);

	private:
		struct OpenFile
		{
			std::chrono::sys_days day{};
			std::ofstream file;
		};

		void removeExpired(std::chrono::sys_days today);

		std::filesystem::path m_directory;
		int m_keepDays;
		std::mutex m_mutex;
		std::map<std::string, OpenFile, std::less<>> m_files;
		std::chrono::sys_days m_cleanedFor{};
	};
}

#endif
