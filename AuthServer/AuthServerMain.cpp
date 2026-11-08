/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */


#include "Utils/Logger.h"
#include "Utils/CrashHandler.h"
#include <chrono>
#include <format>

#include <asio/execution_context.hpp>
#include "../include/AuthServer.h"

#include <iostream>
#include <Utils/SetupParser.h>
#include <Network/IpcSecurity.h>
#include "Utils/Utils.h"


int main()
{
	Utils::Logger::setProcessName("Auth");
	Utils::CrashHandler::install();
	Common::Utils::setConsoleTitle(L"Microvolts Auth Server");

	auto const time = std::chrono::current_zone()->to_local(std::chrono::system_clock::now());
	auto const time_s = std::format("{:%Y-%m-%d %X}", time);
	Utils::Logger::log("Auth server initialized on " + time_s, Utils::LogType::Info, "AuthServer");
	auto parsedServerInfo = Common::Utils::SetupParser::getInstance().getAuthSetup();
	if (!Common::Network::IpcSecurity::initialize())
	{
		return 1;
	}

	Utils::Logger::log(std::format("Server Information: IP: {},  Port: {}, GradedPort: {}",
		parsedServerInfo.ip, parsedServerInfo.port, parsedServerInfo.gradedPort), Utils::LogType::Normal);

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

	asio::io_context io_context;
	Auth::AuthServer srv(io_context, parsedServerInfo.ip, parsedServerInfo.vpnIp, parsedServerInfo.port, parsedServerInfo.gradedPort);
	srv.asyncAccept();
	io_context.run();
}
