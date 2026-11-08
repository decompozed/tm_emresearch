/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#ifndef COMMON_HAVE_API_MODE_LOG_H
#define COMMON_HAVE_API_MODE_LOG_H

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include "HaveApi/Catalog.h"
#include "Utils/DailyLogFiles.h"

// Party match logs: one file per party game and UTC day, <TB_LOG_DIR>/modes/<game>-YYYY-MM-DD.log, written by both
// MainServer and CastServer (a line names its server). Every line: UTC time to the millisecond, server, room, round, event.
namespace Common::HaveApi
{
	[[nodiscard]] constexpr std::string_view modeLogName(std::int32_t modeId)
	{
		switch (static_cast<PartyModeId>(modeId))
		{
		case PartyModeId::ChickenRun: return "chicken-run";
		case PartyModeId::Chaser: return "chaser";
		case PartyModeId::Arena: return "arena";
		case PartyModeId::TeamClash: return "team-clash";
		case PartyModeId::LavaEscape: return "lava-escape";
		case PartyModeId::Hardpoint: return "hardpoint";
		case PartyModeId::Headquarters: return "headquarters";
		case PartyModeId::PropHunt: return "prop-hunt";
		case PartyModeId::EliminationShuffle: return "elimination-shuffle";
		case PartyModeId::Domination: return "domination";
		case PartyModeId::BattleRoyale: return "battle-royale";
		case PartyModeId::None: break;
		}
		return "party-unknown";
	}

	inline constexpr int modeLogKeepDays = 30;

	class ModeLog
	{
	public:
		ModeLog(std::filesystem::path directory, std::string server, int keepDays = modeLogKeepDays);

		void write(std::int32_t modeId, std::uint32_t room, std::uint32_t round, std::string_view event,
			::Utils::DailyLogFiles::Clock::time_point now = ::Utils::DailyLogFiles::Clock::now());

		[[nodiscard]] static std::string line(std::string_view server, std::uint32_t room, std::uint32_t round, std::string_view event,
			::Utils::DailyLogFiles::Clock::time_point now);

	private:
		::Utils::DailyLogFiles m_files;
		std::string m_server;
	};

	// The process's mode log (<TB_LOG_DIR>/modes, server = Utils::Logger::processName()); nothing is written without
	// TB_LOG_DIR.
	void writeModeLog(std::int32_t modeId, std::uint32_t room, std::uint32_t round, std::string_view event);
}

#endif
