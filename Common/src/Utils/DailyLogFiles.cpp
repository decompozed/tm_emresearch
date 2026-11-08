/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#include "Utils/DailyLogFiles.h"
#include <algorithm>
#include <cctype>
#include <format>
#include <vector>

namespace Utils
{
	namespace
	{
		constexpr std::string_view extension = ".log";
		constexpr std::size_t dateLength = 10; // YYYY-MM-DD

		std::string safeStream(std::string_view stream)
		{
			std::string name;
			for (const char c : stream)
			{
				const bool allowed = std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_';
				name += allowed ? c : '_';
			}
			return name.empty() ? std::string{ "log" } : name;
		}

		std::optional<int> number(std::string_view digits)
		{
			int value = 0;
			for (const char c : digits)
			{
				if (!std::isdigit(static_cast<unsigned char>(c)))
				{
					return std::nullopt;
				}
				value = value * 10 + (c - '0');
			}
			return value;
		}
	}

	DailyLogFiles::DailyLogFiles(std::filesystem::path directory, int keepDays)
		: m_directory{ std::move(directory) }
		, m_keepDays{ (std::max)(keepDays, 1) }
	{
	}

	std::string DailyLogFiles::fileName(std::string_view stream, std::chrono::sys_days day)
	{
		const std::chrono::year_month_day date{ day };
		return std::format("{}-{:04}-{:02}-{:02}{}", safeStream(stream), static_cast<int>(date.year()),
			static_cast<unsigned>(date.month()), static_cast<unsigned>(date.day()), extension);
	}

	std::optional<std::chrono::sys_days> DailyLogFiles::dayOf(std::string_view fileName)
	{
		// <stream>-YYYY-MM-DD.log
		if (fileName.size() < dateLength + extension.size() + 2 || !fileName.ends_with(extension))
		{
			return std::nullopt;
		}
		const std::string_view date = fileName.substr(fileName.size() - extension.size() - dateLength, dateLength);
		if (fileName[fileName.size() - extension.size() - dateLength - 1] != '-' || date[4] != '-' || date[7] != '-')
		{
			return std::nullopt;
		}
		const auto year = number(date.substr(0, 4));
		const auto month = number(date.substr(5, 2));
		const auto day = number(date.substr(8, 2));
		if (!year || !month || !day)
		{
			return std::nullopt;
		}
		const std::chrono::year_month_day ymd{ std::chrono::year{ *year }, std::chrono::month{ static_cast<unsigned>(*month) },
			std::chrono::day{ static_cast<unsigned>(*day) } };
		return ymd.ok() ? std::optional{ std::chrono::sys_days{ ymd } } : std::nullopt;
	}

	void DailyLogFiles::write(std::string_view stream, std::string_view line, Clock::time_point now)
	{
		const auto today = std::chrono::floor<std::chrono::days>(now);
		const std::string name = safeStream(stream);
		std::lock_guard lock{ m_mutex };
		auto found = m_files.find(name);
		if (found == m_files.end())
		{
			found = m_files.emplace(name, OpenFile{}).first;
		}
		OpenFile& open = found->second;
		if (!open.file.is_open() || open.day != today)
		{
			open.file.close();
			std::error_code ec;
			std::filesystem::create_directories(m_directory, ec);
			open.day = today;
			open.file.open(m_directory / fileName(name, today), std::ios::app | std::ios::binary);
			if (m_cleanedFor != today)
			{
				removeExpired(today);
			}
		}
		if (!open.file.is_open())
		{
			return;
		}
		open.file << line;
		if (!line.ends_with('\n'))
		{
			open.file << '\n';
		}
		open.file.flush();
	}

	void DailyLogFiles::removeExpired(std::chrono::sys_days today)
	{
		m_cleanedFor = today;
		const auto oldest = today - std::chrono::days{ m_keepDays - 1 };
		std::error_code ec;
		std::vector<std::filesystem::path> expired;
		for (std::filesystem::directory_iterator it{ m_directory, ec }, end; !ec && it != end; it.increment(ec))
		{
			if (const auto day = dayOf(it->path().filename().string()); day && *day < oldest && it->is_regular_file(ec))
			{
				expired.push_back(it->path());
			}
		}
		for (const auto& path : expired)
		{
			std::filesystem::remove(path, ec);
		}
	}
}
