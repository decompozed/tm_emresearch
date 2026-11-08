/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#include "../../include/Network/IpcSecurity.h"
#include "../../include/Network/IpcTls.h"
#include "../../include/Utils/SetupParser.h"
#include "../../include/Utils/Logger.h"
#include <exception>
#include <memory>

namespace Common::Network::IpcSecurity
{
	namespace
	{
		const CryptoPP::SecByteBlock& configuredSecret()
		{
			return Common::Utils::SetupParser::getInstance().getIpcSetup().secret;
		}

		std::unique_ptr<IpcTls::Context> createContext()
		{
			const auto& secret = configuredSecret();
			if (secret.empty())
			{
				return nullptr;
			}
			try
			{
				return std::make_unique<IpcTls::Context>(std::span<const std::uint8_t>{ secret.data(), secret.size() });
			}
			catch (const std::exception& e)
			{
				::Utils::Logger::log(std::string{ "[IPC] TLS setup failed: " } + e.what(), ::Utils::LogType::Error, "IpcSecurity");
				return nullptr;
			}
		}
	}

	bool initialize()
	{
		if (!required())
		{
			::Utils::Logger::log("[IPC] [Ipc] SecretEnvironmentName is not set: the links between servers are NOT encrypted nor "
				"authenticated (IP filter only)", ::Utils::LogType::Warning, "IpcSecurity::initialize");
			return true;
		}
		if (context() == nullptr)
		{
			return false;
		}
		::Utils::Logger::log("[IPC] links between servers: TLS 1.3 with the pre-shared key", ::Utils::LogType::Info);
		return true;
	}

	bool required()
	{
		return !configuredSecret().empty();
	}

	IpcTls::Context* context()
	{
		static const std::unique_ptr<IpcTls::Context> instance = createContext();
		return instance.get();
	}
}
