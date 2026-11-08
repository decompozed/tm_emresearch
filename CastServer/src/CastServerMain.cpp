/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#include <chrono>
#include <format>
#include <string>
#include <ConstantDatabase/CdbSingleton.h>
#include <ConstantDatabase/Structures/CdbMapInfo.h>
#include <Network/IpcSecurity.h>
#include <Network/IpcStats.h>
#include <Utils/CrashHandler.h>
#include <Utils/Logger.h>
#include <Utils/SetupParser.h>
#include <Utils/Utils.h>
#include "CastServer.h"

#ifdef _WIN32
#include <windows.h>
#include <timeapi.h>
#pragma comment(lib, "winmm.lib")
#endif

void printInitialInformation()
{
	auto const time = std::chrono::current_zone()->to_local(std::chrono::system_clock::now());
	auto const time_s = std::format("{:%Y-%m-%d %X}", time);

	Utils::Logger::log("Cast server initialized on " + time_s, Utils::LogType::Info);
	const std::string cdbItemInfoPath = "../ExternalLibraries/cgd_original/ENG";
	const std::string cdbMapInfoName = "mapinfo.cdb";
	using mapInfo = Common::ConstantDatabase::CdbSingleton<Common::ConstantDatabase::CdbMapInfo>;
	mapInfo::initialize(cdbItemInfoPath, cdbMapInfoName);
}

int main()
{
	Utils::Logger::setProcessName("Cast");
	Utils::CrashHandler::install();
	Common::Utils::setConsoleTitle(L"Microvolts Cast Server");
	printInitialInformation();

#ifdef _WIN32
	// Windows' default 15.6 ms timer resolution turns the 16 ms position batches (tick rate 60) into 31 ms
	timeBeginPeriod(1);
#endif
	asio::io_context io_context;

	auto parsedServerInfo = Common::Utils::SetupParser::getInstance().getSelfCastServerInfo();
	if (!Common::Network::IpcSecurity::initialize())
	{
		return 1;
	}
	Utils::Logger::log(std::format("Server Information: IP: {},  Port: {},  IPC Port: {},  Server Number: {}",
		parsedServerInfo.ip, parsedServerInfo.port,
		parsedServerInfo.ipcPort, parsedServerInfo.serverNumber),
		Utils::LogType::Normal);

	const std::string banner = R"(

 _____           ____        _   _   _           _   _  ___  
|_   _|__  _   _| __ )  __ _| |_| |_| | ___  ___| | | |/ _ \ 
  | |/ _ \| | | |  _ \ / _` | __| __| |/ _ \/ __| |_| | | | |
  | | (_) | |_| | |_) | (_| | |_| |_| |  __/\__ \  _  | |_| |
  |_|\___/ \__, |____/ \__,_|\__|\__|_|\___||___/_| |_|\__\_\
           |___/

    ToyBattlesHQ Emulator
    Copyright (C) 2026 DownWithTheFallen (https://github.com/DownWithTheFallen)

    Licensed under ToyBattlesHQ Custom License (TBCL v1.0)

    NON-COMMERCIAL USE ONLY
    MODIFICATIONS MUST BE PUBLIC
    ATTRIBUTION REQUIRED

    This software is provided "AS IS", WITHOUT WARRANTY OF ANY KIND.

    Full license terms:
    https://github.com/DownWithTheFallen/ToyBattlesHQ/blob/toybattles_mvsurge/LICENSE

    GitHub:
    https://github.com/DownWithTheFallen/ToyBattlesHQ

)";

	Utils::Logger::log(banner, Utils::LogType::Info);

	Cast::CastServer srv(io_context, parsedServerInfo.ip, parsedServerInfo.port, parsedServerInfo.ipcPort, parsedServerInfo.serverNumber);

	srv.asyncAccept();
	srv.asyncAcceptMainServer();
	Common::Network::IpcStats::startReporter(io_context);
	io_context.run();
}
