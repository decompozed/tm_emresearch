/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#ifndef COMMON_HAVE_API_MESSAGE_READER_H
#define COMMON_HAVE_API_MESSAGE_READER_H

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <type_traits>
#include "HaveApi/MessageHeader.h"

// Parses the data part of an order-81 packet sent by the DLL: [u32 length][40-byte header][payload].
// Nothing a client sends is trusted: every read is bounds-checked.
namespace Common::HaveApi
{
	class ReceivedMessage
	{
	public:
		[[nodiscard]] static std::optional<ReceivedMessage> parse(std::span<const std::uint8_t> data)
		{
			if (data.size() < sizeof(std::uint32_t) + sizeof(MessageHeader))
			{
				return std::nullopt;
			}
			const auto length = read<std::uint32_t>(data, 0);
			const std::span<const std::uint8_t> message = data.subspan(sizeof(std::uint32_t));
			if (length < sizeof(MessageHeader) || length > message.size())
			{
				return std::nullopt;
			}
			return ReceivedMessage{ message.first(length) };
		}

		[[nodiscard]] std::uint16_t id() const noexcept
		{
			return read<MessageHeader>(m_message, 0).id();
		}

		[[nodiscard]] std::span<const std::uint8_t> payload() const noexcept
		{
			return m_message.subspan(sizeof(MessageHeader));
		}

		// The payload as `Payload` when it is at least that long (the DLL always sends the full structure).
		template<typename Payload>
			requires std::is_trivially_copyable_v<Payload>
		[[nodiscard]] std::optional<Payload> payloadAs() const noexcept
		{
			if (payload().size() < sizeof(Payload))
			{
				return std::nullopt;
			}
			return read<Payload>(payload(), 0);
		}

	private:
		explicit ReceivedMessage(std::span<const std::uint8_t> message) : m_message{ message } {}

		template<typename T>
		[[nodiscard]] static T read(std::span<const std::uint8_t> bytes, std::size_t offset) noexcept
		{
			std::array<std::uint8_t, sizeof(T)> raw{};
			std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(offset), sizeof(T), raw.begin());
			return std::bit_cast<T>(raw);
		}

		std::span<const std::uint8_t> m_message;
	};
}

#endif
