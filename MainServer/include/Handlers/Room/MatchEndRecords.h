#ifndef MATCH_END_RECORDS_H
#define MATCH_END_RECORDS_H

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace Main::Handlers
{
	// The host's match end lists each player of its room slots once (0xA89660 and the other mode fillers).
	template<typename Record>
	struct MatchEndRecords
	{
		std::vector<Record> players;
		std::size_t duplicates{};
		std::size_t strangers{};
		std::size_t missing{};

		bool clean() const noexcept { return duplicates == 0 && strangers == 0 && missing == 0; }

		std::string anomalies() const
		{
			return std::to_string(duplicates) + " duplicated, " + std::to_string(strangers) + " not in the room, "
				+ std::to_string(missing) + " declared but missing";
		}
	};

	template<typename Record, typename IsMember>
	MatchEndRecords<Record> readMatchEndRecords(std::span<const std::uint8_t> payload, std::size_t headerSize, std::size_t declared,
		IsMember&& isMember)
	{
		MatchEndRecords<Record> result;
		const std::size_t present = payload.size() > headerSize ? (payload.size() - headerSize) / sizeof(Record) : 0;
		const std::size_t count = std::min(declared, present);
		result.missing = declared - count;
		result.players.reserve(count);

		std::vector<std::uint32_t> seen;
		seen.reserve(count);
		for (std::size_t i = 0; i < count; ++i)
		{
			std::array<std::uint8_t, sizeof(Record)> raw{};
			std::copy_n(payload.begin() + static_cast<std::ptrdiff_t>(headerSize + i * sizeof(Record)), sizeof(Record), raw.begin());
			const auto record = std::bit_cast<Record>(raw);

			const std::uint32_t session = record.uniqueId.session;
			if (std::ranges::find(seen, session) != seen.end())
			{
				++result.duplicates;
				continue;
			}
			seen.push_back(session);

			if (!isMember(record.uniqueId))
			{
				++result.strangers;
				continue;
			}
			result.players.push_back(record);
		}
		return result;
	}
}

#endif
