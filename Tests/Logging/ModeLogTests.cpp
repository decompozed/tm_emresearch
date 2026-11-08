/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#include <gtest/gtest.h>
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <thread>
#include <vector>
#include "HaveApi/HitTally.h"
#include "HaveApi/ModeLog.h"
#include "Utils/DailyLogFiles.h"

namespace CH = Common::HaveApi;
using Utils::DailyLogFiles;
using namespace std::chrono_literals;

namespace
{
	constexpr std::chrono::sys_days day(int y, unsigned m, unsigned d)
	{
		return std::chrono::sys_days{ std::chrono::year{ y } / std::chrono::month{ m } / std::chrono::day{ d } };
	}

	class TempDir
	{
	public:
		TempDir()
		{
			const auto suffix = std::to_string(std::random_device{}()) + "-" + std::to_string(++counter());
			m_path = std::filesystem::temp_directory_path() / ("tb-mode-log-tests-" + suffix);
			std::filesystem::create_directories(m_path);
		}
		~TempDir()
		{
			std::error_code ec;
			std::filesystem::remove_all(m_path, ec);
		}
		TempDir(const TempDir&) = delete;
		TempDir& operator=(const TempDir&) = delete;

		[[nodiscard]] const std::filesystem::path& path() const noexcept { return m_path; }

	private:
		static std::atomic<int>& counter()
		{
			static std::atomic<int> value{ 0 };
			return value;
		}
		std::filesystem::path m_path;
	};

	std::vector<std::string> lines(const std::filesystem::path& file)
	{
		std::vector<std::string> result;
		std::ifstream in{ file };
		for (std::string line; std::getline(in, line);)
		{
			result.push_back(line);
		}
		return result;
	}

	std::vector<std::string> files(const std::filesystem::path& dir)
	{
		std::vector<std::string> names;
		for (const auto& entry : std::filesystem::directory_iterator{ dir })
		{
			names.push_back(entry.path().filename().string());
		}
		std::ranges::sort(names);
		return names;
	}
}

TEST(DailyLogFiles, FileNamesCarryTheStreamAndTheUtcDay)
{
	EXPECT_EQ(DailyLogFiles::fileName("lava-escape", day(2026, 10, 6)), "lava-escape-2026-10-06.log");
	EXPECT_EQ(DailyLogFiles::fileName("../x y", day(2026, 1, 2)), "___x_y-2026-01-02.log");
	EXPECT_EQ(DailyLogFiles::dayOf("prop-hunt-2026-10-06.log"), day(2026, 10, 6));
	EXPECT_EQ(DailyLogFiles::dayOf(DailyLogFiles::fileName("chaser", day(2027, 2, 28))), day(2027, 2, 28));
	EXPECT_FALSE(DailyLogFiles::dayOf("Main.log"));
	EXPECT_FALSE(DailyLogFiles::dayOf("prop-hunt-2026-13-06.log"));
	EXPECT_FALSE(DailyLogFiles::dayOf("prop-hunt-2026-10-06.txt"));
	EXPECT_FALSE(DailyLogFiles::dayOf("2026-10-06.log"));
}

TEST(DailyLogFiles, OneFilePerStreamAndDay)
{
	TempDir dir;
	DailyLogFiles logs{ dir.path() };
	const auto morning = day(2026, 10, 6) + 8h;
	logs.write("lava-escape", "first", morning);
	logs.write("prop-hunt", "other mode", morning + 1min);
	logs.write("lava-escape", "second\n", morning + 2h);
	logs.write("lava-escape", "next day", morning + 16h + 1s);       // 00:00:01 of the 7th

	EXPECT_EQ(files(dir.path()), (std::vector<std::string>{ "lava-escape-2026-10-06.log", "lava-escape-2026-10-07.log", "prop-hunt-2026-10-06.log" }));
	EXPECT_EQ(lines(dir.path() / "lava-escape-2026-10-06.log"), (std::vector<std::string>{ "first", "second" }));
	EXPECT_EQ(lines(dir.path() / "lava-escape-2026-10-07.log"), (std::vector<std::string>{ "next day" }));
	EXPECT_EQ(lines(dir.path() / "prop-hunt-2026-10-06.log"), (std::vector<std::string>{ "other mode" }));
}

TEST(DailyLogFiles, AppendsToTheFileOfTodayAfterARestart)
{
	TempDir dir;
	const auto now = day(2026, 10, 6) + 12h;
	DailyLogFiles{ dir.path() }.write("arena", "before restart", now);
	DailyLogFiles{ dir.path() }.write("arena", "after restart", now + 1min);
	EXPECT_EQ(lines(dir.path() / "arena-2026-10-06.log"), (std::vector<std::string>{ "before restart", "after restart" }));
}

TEST(DailyLogFiles, ANewDayRemovesTheFilesOlderThanTheKeptDays)
{
	TempDir dir;
	for (const auto& name : { "lava-escape-2026-09-01.log", "prop-hunt-2026-09-06.log", "prop-hunt-2026-09-07.log", "Main.log", "notes-2026-09.log" })
	{
		std::ofstream{ dir.path() / name } << "old\n";
	}
	DailyLogFiles logs{ dir.path(), 30 };
	logs.write("lava-escape", "today", day(2026, 10, 6) + 1h);        // keeps 2026-09-07 .. 2026-10-06
	EXPECT_EQ(files(dir.path()), (std::vector<std::string>{ "Main.log", "lava-escape-2026-10-06.log", "notes-2026-09.log", "prop-hunt-2026-09-07.log" }));

	logs.write("lava-escape", "tomorrow", day(2026, 10, 7) + 1h);      // keeps 2026-09-08 .. 2026-10-07
	EXPECT_EQ(files(dir.path()), (std::vector<std::string>{ "Main.log", "lava-escape-2026-10-06.log", "lava-escape-2026-10-07.log", "notes-2026-09.log" }));
}

TEST(DailyLogFiles, ConcurrentWritersLoseNoLine)
{
	TempDir dir;
	DailyLogFiles logs{ dir.path() };
	const auto now = day(2026, 10, 6) + 10h;
	constexpr int threads = 8;
	constexpr int perThread = 200;
	std::vector<std::thread> writers;
	for (int t = 0; t < threads; ++t)
	{
		writers.emplace_back([&, t] {
			for (int i = 0; i < perThread; ++i)
			{
				logs.write(t % 2 == 0 ? "chaser" : "arena", "thread " + std::to_string(t) + " line " + std::to_string(i), now);
			}
		});
	}
	for (auto& writer : writers)
	{
		writer.join();
	}
	const auto chaser = lines(dir.path() / "chaser-2026-10-06.log");
	const auto arena = lines(dir.path() / "arena-2026-10-06.log");
	EXPECT_EQ(chaser.size() + arena.size(), static_cast<std::size_t>(threads * perThread));
	for (const auto& line : chaser)
	{
		EXPECT_EQ(line.rfind("thread ", 0), 0u) << line;
	}
}

TEST(ModeLog, EveryPartyGameHasItsOwnFile)
{
	EXPECT_EQ(CH::modeLogName(static_cast<std::int32_t>(CH::PartyModeId::LavaEscape)), "lava-escape");
	EXPECT_EQ(CH::modeLogName(static_cast<std::int32_t>(CH::PartyModeId::PropHunt)), "prop-hunt");
	EXPECT_EQ(CH::modeLogName(static_cast<std::int32_t>(CH::PartyModeId::BattleRoyale)), "battle-royale");
	EXPECT_EQ(CH::modeLogName(0), "party-unknown");
	EXPECT_EQ(CH::modeLogName(99), "party-unknown");
	std::vector<std::string_view> names;
	for (std::int32_t id = 1; id <= static_cast<std::int32_t>(CH::PartyModeId::BattleRoyale); ++id)
	{
		names.push_back(CH::modeLogName(id));
		EXPECT_NE(names.back(), "party-unknown") << id;
	}
	std::ranges::sort(names);
	EXPECT_EQ(std::ranges::adjacent_find(names), names.end());
}

TEST(ModeLog, LinesCarryTimeServerRoomAndRound)
{
	const auto at = day(2026, 10, 6) + 13h + 4min + 5s + 67ms;
	EXPECT_EQ(CH::ModeLog::line("Cast", 12, 3, "lava start", at), "2026-10-06 13:04:05.067 [Cast] room 12 round 3 | lava start\n");
}

TEST(ModeLog, WritesToTheFileOfTheRoomsGame)
{
	TempDir dir;
	CH::ModeLog log{ dir.path(), "Main" };
	const auto at = day(2026, 10, 6) + 1h;
	log.write(static_cast<std::int32_t>(CH::PartyModeId::LavaEscape), 4, 1, "match start", at);
	log.write(static_cast<std::int32_t>(CH::PartyModeId::Hardpoint), 5, 2, "zone", at);
	log.write(0, 6, 0, "no game", at);
	EXPECT_EQ(files(dir.path()), (std::vector<std::string>{ "hardpoint-2026-10-06.log", "lava-escape-2026-10-06.log", "party-unknown-2026-10-06.log" }));
	EXPECT_EQ(lines(dir.path() / "lava-escape-2026-10-06.log"), (std::vector<std::string>{ "2026-10-06 01:00:00.000 [Main] room 4 round 1 | match start" }));
}

TEST(HitTally, OneEntryPerAttackerAndVictim)
{
	CH::HitTally tally;
	tally.add(1, 2, CH::DeathCause::Weapon);
	tally.add(1, 2, CH::DeathCause::Explosive, true);
	tally.add(1, 2, CH::DeathCause::Weapon);
	tally.add(2, 1, CH::DeathCause::Server);
	const auto entries = tally.take();
	ASSERT_EQ(entries.size(), 2u);
	EXPECT_EQ(entries[0].attacker, 1u);
	EXPECT_EQ(entries[0].victim, 2u);
	EXPECT_EQ(entries[0].weapon, 2u);
	EXPECT_EQ(entries[0].explosive, 1u);
	EXPECT_EQ(entries[0].spared, 1u);
	EXPECT_EQ(entries[0].total(), 3u);
	EXPECT_EQ(entries[1].other, 1u);
	EXPECT_TRUE(tally.empty());
}
