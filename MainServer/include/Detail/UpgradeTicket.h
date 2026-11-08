/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#ifndef MAIN_UPGRADE_TICKET_H
#define MAIN_UPGRADE_TICKET_H

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>

// upgradeinfo.cdb: ui_group is not reliable (3208778 / 3208878 name 3208750), some chains have no item (4206950, 4207050, 4207150)
namespace Main::UpgradeTicket
{
	inline constexpr std::uint32_t lv8TicketId = 4305037;
	inline constexpr std::uint32_t lv9TicketId = 4305038;
	inline constexpr std::uint8_t branchCount = 3;
	inline constexpr std::uint8_t maxLevel = 9;

	[[nodiscard]] constexpr std::uint32_t ticketIdFor(std::uint8_t level) noexcept
	{
		return level == 8 ? lv8TicketId : level == 9 ? lv9TicketId : 0;
	}

	struct Position
	{
		std::uint32_t base{};
		std::uint8_t branch{};
		std::uint8_t level{};
	};

	enum class Verdict { Allowed, NotUpgradable, BadChoice, AlreadyThere, NoTarget };

	struct Result
	{
		Verdict verdict{};
		Position from{};
		std::uint32_t target{};
	};

	template<typename Lookup>
	[[nodiscard]] std::optional<Position> positionOf(std::uint32_t itemId, Lookup&& lookup)
	{
		const auto* row = lookup(itemId);
		if (row == nullptr) return std::nullopt;
		const int type = row->ui_type;
		std::uint32_t current = itemId;
		std::uint8_t level = 0;
		while (row->ui_parentid != 0)
		{
			if (++level > maxLevel) return std::nullopt;
			current = static_cast<std::uint32_t>(row->ui_parentid);
			row = lookup(current);
			if (row == nullptr) return std::nullopt;
		}
		if (row->ui_type != 0) return std::nullopt;
		if (level == 0) return Position{ current, 0, 0 };
		if (type < 1 || type > branchCount) return std::nullopt;
		return Position{ current, static_cast<std::uint8_t>(type), level };
	}

	template<typename Lookup>
	[[nodiscard]] std::optional<std::uint32_t> targetOf(std::uint32_t base, std::uint8_t branch, std::uint8_t level, Lookup&& lookup)
	{
		if (branch < 1 || branch > branchCount || level < 1 || level > maxLevel) return std::nullopt;
		const std::uint32_t id = base + (branch - 1u) * 10u + level;
		const auto position = positionOf(id, lookup);
		if (!position || position->base != base || position->branch != branch || position->level != level) return std::nullopt;
		return id;
	}

	template<typename Lookup, typename Exists>
	[[nodiscard]] Result check(std::uint32_t weaponId, std::uint8_t branch, std::uint8_t level, Lookup&& lookup, Exists&& exists)
	{
		Result result{};
		const auto from = positionOf(weaponId, lookup);
		if (!from) return { Verdict::NotUpgradable };
		result.from = *from;
		if ((level != 8 && level != 9) || branch < 1 || branch > branchCount)
		{
			result.verdict = Verdict::BadChoice;
			return result;
		}
		if (from->branch == branch && from->level >= level)
		{
			result.verdict = Verdict::AlreadyThere;
			return result;
		}
		const auto target = targetOf(from->base, branch, level, lookup);
		if (!target || !exists(*target))
		{
			result.verdict = Verdict::NoTarget;
			return result;
		}
		result.target = *target;
		result.verdict = Verdict::Allowed;
		return result;
	}

	template<typename Lookup, typename Exists>
	[[nodiscard]] std::uint8_t allowedMask(std::uint32_t weaponId, Lookup&& lookup, Exists&& exists)
	{
		std::uint8_t mask = 0;
		for (std::uint8_t branch = 1; branch <= branchCount; ++branch)
		{
			for (std::uint8_t level = 8; level <= 9; ++level)
			{
				if (check(weaponId, branch, level, lookup, exists).verdict == Verdict::Allowed)
					mask = static_cast<std::uint8_t>(mask | (1u << ((branch - 1u) * 2u + (level - 8u))));
			}
		}
		return mask;
	}

	[[nodiscard]] inline std::string branchName(const char* desc, std::size_t capacity)
	{
		std::string name(desc, std::find(desc, desc + capacity, '\0'));
		const auto mark = name.rfind(" LV");
		if (mark != std::string::npos && mark + 3 < name.size() &&
			name.find_first_not_of("0123456789", mark + 3) == std::string::npos)
			name.erase(mark);
		return name;
	}

	template<typename Lookup>
	[[nodiscard]] std::string branchNameOf(std::uint32_t base, std::uint8_t branch, Lookup&& lookup)
	{
		const auto first = targetOf(base, branch, 1, lookup);
		if (!first) return {};
		const auto* row = lookup(*first);
		return branchName(row->ui_desc.data(), row->ui_desc.size());
	}
}

#endif
