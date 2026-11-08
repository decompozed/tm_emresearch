/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#ifndef COMMON_HAVE_API_MESSAGE_WRITER_H
#define COMMON_HAVE_API_MESSAGE_WRITER_H

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>
#include <vector>
#include "HaveApi/MessageHeader.h"
#include "HaveApi/Messages.h"

// Builds the data part of an order-81 packet for the DLL: [u32 length][40-byte header][payload], where
// length = 40 + payload size (the u32 itself is not counted).
namespace Common::HaveApi
{
	// Longest text that still fits in one packet (1440 bytes minus TCP/command headers, length and header).
	inline constexpr std::size_t maxTextLength = 1300;

	class MessageWriter
	{
	public:
		explicit MessageWriter(MessageId id)
		{
			MessageHeader header{};
			header.setId(id);
			m_bytes.resize(sizeof(std::uint32_t));
			append(header);
		}

		template<typename Payload>
			requires std::is_trivially_copyable_v<Payload>
		MessageWriter& append(const Payload& payload)
		{
			const auto bytes = std::bit_cast<std::array<std::uint8_t, sizeof(Payload)>>(payload);
			m_bytes.insert(m_bytes.end(), bytes.begin(), bytes.end());
			return *this;
		}

		template<typename Element>
		MessageWriter& appendRange(std::span<const Element> elements)
		{
			for (const Element& element : elements)
			{
				append(element);
			}
			return *this;
		}

		// NUL-terminated text, cut at `maxLength` characters.
		MessageWriter& appendText(std::string_view text, std::size_t maxLength = maxTextLength)
		{
			const std::string_view kept = text.substr(0, (std::min)(text.size(), maxLength));
			m_bytes.insert(m_bytes.end(), kept.begin(), kept.end());
			m_bytes.push_back(0);
			return *this;
		}

		[[nodiscard]] std::vector<std::uint8_t> finish()
		{
			const auto length = static_cast<std::uint32_t>(m_bytes.size() - sizeof(std::uint32_t));
			const auto lengthBytes = std::bit_cast<std::array<std::uint8_t, sizeof(length)>>(length);
			std::ranges::copy(lengthBytes, m_bytes.begin());
			return std::move(m_bytes);
		}

	private:
		std::vector<std::uint8_t> m_bytes;
	};

	[[nodiscard]] inline std::vector<std::uint8_t> encodeEmpty(MessageId id)
	{
		return MessageWriter{ id }.finish();
	}

	template<typename Payload>
	[[nodiscard]] std::vector<std::uint8_t> encode(MessageId id, const Payload& payload)
	{
		return MessageWriter{ id }.append(payload).finish();
	}

	// 0x02 status text, 0x07 popup, 0x26 music file.
	[[nodiscard]] inline std::vector<std::uint8_t> encodeText(MessageId id, std::string_view text)
	{
		return MessageWriter{ id }.appendText(text).finish();
	}
}

#endif
