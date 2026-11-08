/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#include "Connection/ConnectionRules.h"
#include <bit>
#include <cstring>

namespace Cast::Connection
{
	bool accepts(Stage stage, std::uint16_t order) noexcept
	{
		if (order == connectOrder)
		{
			return stage == Stage::AwaitingConnect;
		}
		return stage == Stage::Authenticated;
	}

	std::optional<Structures::ConnectRequest> parseConnect(const Command& command) noexcept
	{
		if (command.payload.size() != sizeof(Structures::ConnectRequest) || command.mission != 0 || command.extra != 0 || command.option != 0)
		{
			return std::nullopt;
		}
		Structures::ConnectRequest request{};
		std::memcpy(&request, command.payload.data(), sizeof(request));
		if (request.accountId == 0 || request.hashKey == 0 || request.playerKey.unknown != 0)
		{
			return std::nullopt;
		}
		return request;
	}

	Common::HaveApi::CastConnectCheck toCheck(const Structures::ConnectRequest& request) noexcept
	{
		Common::HaveApi::CastConnectCheck check{};
		check.accountId = request.accountId;
		check.hashKey = request.hashKey;
		check.playerKey = std::bit_cast<std::uint32_t>(request.playerKey);
		return check;
	}

	std::optional<std::uint16_t> verifiedSession(const std::optional<Common::HaveApi::CastConnectAnswer>& answer,
		const Structures::ConnectRequest& request) noexcept
	{
		if (!answer || answer->verdict != Common::HaveApi::CastConnectVerdict::Accepted || answer->session == 0 ||
			answer->session > Common::Constants::maxSessionsPerServer || answer->session != request.playerKey.session)
		{
			return std::nullopt;
		}
		return answer->session;
	}

	Outcome connectOutcome(bool socketOpen, Stage stage, std::optional<std::uint16_t> session, bool heldByAnotherSocket) noexcept
	{
		if (!socketOpen || stage != Stage::Verifying)
		{
			return Outcome::Ignore;
		}
		if (!session)
		{
			return Outcome::Reject;
		}
		return heldByAnotherSocket ? Outcome::ReplaceHolder : Outcome::Register;
	}

	bool isPing(const Command& command) noexcept
	{
		return command.payload.empty() && command.mission == 1 && command.extra == 0;
	}

	bool isCrashReport(const Command& command) noexcept
	{
		return command.payload.empty() && command.mission == 0 && command.option == 0;
	}
}
