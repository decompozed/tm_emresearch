#ifndef CAST_CONNECTION_CONNECTION_RULES_H
#define CAST_CONNECTION_CONNECTION_RULES_H

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include "HaveApi/CastConnect.h"
#include "Structures/Connection/ConnectionPackets.h"
#include "Utils/Constants.h"

namespace Cast::Connection
{
	inline constexpr std::size_t firstLocalSessionId = Common::Constants::maxSessionsPerServer + 1;
	inline constexpr std::size_t localSessionIds = Common::Constants::maxSessionsPerServer;
	static_assert(firstLocalSessionId + localSessionIds - 1 <= 0x3FFF);

	inline constexpr std::uint16_t pingOrder = 71;
	inline constexpr std::uint16_t connectOrder = 252;
	inline constexpr std::uint16_t crashOrder = 253;

	inline constexpr std::chrono::seconds verifyTimeout{ 5 };
	inline constexpr std::chrono::seconds authenticationDeadline{ 15 };

	enum class Stage : std::uint8_t
	{
		AwaitingConnect,
		Verifying,
		Authenticated,
	};

	enum class Outcome : std::uint8_t
	{
		Ignore,
		Reject,
		Register,
		ReplaceHolder,
	};

	struct Command
	{
		std::uint16_t order{};
		std::uint8_t mission{};
		std::uint8_t extra{};
		std::uint8_t option{};
		std::span<const std::uint8_t> payload;
	};

	[[nodiscard]] bool accepts(Stage stage, std::uint16_t order) noexcept;
	[[nodiscard]] std::optional<Structures::ConnectRequest> parseConnect(const Command& command) noexcept;
	[[nodiscard]] Common::HaveApi::CastConnectCheck toCheck(const Structures::ConnectRequest& request) noexcept;
	[[nodiscard]] std::optional<std::uint16_t> verifiedSession(const std::optional<Common::HaveApi::CastConnectAnswer>& answer,
		const Structures::ConnectRequest& request) noexcept;
	[[nodiscard]] Outcome connectOutcome(bool socketOpen, Stage stage, std::optional<std::uint16_t> session, bool heldByAnotherSocket) noexcept;
	[[nodiscard]] bool isPing(const Command& command) noexcept;
	[[nodiscard]] bool isCrashReport(const Command& command) noexcept;
}

#endif
