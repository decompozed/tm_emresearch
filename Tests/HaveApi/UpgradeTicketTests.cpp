/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#include <gtest/gtest.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include "Detail/UpgradeTicket.h"

namespace
{
	namespace UT = Main::UpgradeTicket;

	struct Row
	{
		int ui_type{};
		int ui_parentid{};
		std::array<char, 200> ui_desc{};
	};

	struct Table
	{
		std::map<std::uint32_t, Row> rows;
		std::set<std::uint32_t> items;

		void add(std::uint32_t id, int type, std::uint32_t parent, const std::string& desc)
		{
			Row row{ type, static_cast<int>(parent), {} };
			std::memcpy(row.ui_desc.data(), desc.data(), desc.size());
			rows[id] = row;
			items.insert(id);
		}

		void addWeapon(std::uint32_t base, const std::array<std::string, 3>& names)
		{
			add(base, 0, 0, "");
			for (std::uint32_t branch = 1; branch <= 3; ++branch)
			{
				std::uint32_t parent = base;
				for (std::uint32_t level = 1; level <= 9; ++level)
				{
					const std::uint32_t id = base + (branch - 1) * 10 + level;
					add(id, static_cast<int>(branch), parent, names[branch - 1] + " LV" + std::to_string(level));
					parent = id;
				}
			}
		}

		auto lookup() const
		{
			return [this](std::uint32_t id) -> const Row* {
				const auto found = rows.find(id);
				return found != rows.end() ? &found->second : nullptr;
			};
		}

		auto exists() const
		{
			return [this](std::uint32_t id) { return items.contains(id); };
		}
	};

	constexpr std::uint32_t base = 3011650;

	Table weaponTable()
	{
		Table table;
		table.addWeapon(base, { "Power", "Run Speed", "Range" });
		return table;
	}

	std::uint8_t bit(std::uint8_t branch, std::uint8_t level)
	{
		return static_cast<std::uint8_t>(1u << ((branch - 1u) * 2u + (level - 8u)));
	}
}

TEST(UpgradeTicket, TicketIds)
{
	EXPECT_EQ(UT::ticketIdFor(8), 4305037u);
	EXPECT_EQ(UT::ticketIdFor(9), 4305038u);
	EXPECT_EQ(UT::ticketIdFor(7), 0u);
	EXPECT_EQ(UT::ticketIdFor(0), 0u);
}

TEST(UpgradeTicket, PositionFollowsTheChain)
{
	const Table table = weaponTable();
	const auto atBase = UT::positionOf(base, table.lookup());
	ASSERT_TRUE(atBase);
	EXPECT_EQ(atBase->base, base);
	EXPECT_EQ(atBase->branch, 0);
	EXPECT_EQ(atBase->level, 0);

	const auto range5 = UT::positionOf(base + 25, table.lookup());
	ASSERT_TRUE(range5);
	EXPECT_EQ(range5->base, base);
	EXPECT_EQ(range5->branch, 3);
	EXPECT_EQ(range5->level, 5);

	EXPECT_FALSE(UT::positionOf(4305037, table.lookup()));
}

TEST(UpgradeTicket, TargetId)
{
	const Table table = weaponTable();
	EXPECT_EQ(UT::targetOf(base, 1, 8, table.lookup()), std::optional<std::uint32_t>(base + 8));
	EXPECT_EQ(UT::targetOf(base, 2, 9, table.lookup()), std::optional<std::uint32_t>(base + 19));
	EXPECT_EQ(UT::targetOf(base, 3, 8, table.lookup()), std::optional<std::uint32_t>(base + 28));
	EXPECT_FALSE(UT::targetOf(base, 0, 8, table.lookup()));
	EXPECT_FALSE(UT::targetOf(base, 4, 8, table.lookup()));
	EXPECT_FALSE(UT::targetOf(base, 1, 10, table.lookup()));
}

TEST(UpgradeTicket, TargetIsValidatedNotComputed)
{
	Table table = weaponTable();
	table.rows[base + 18].ui_type = 3;
	EXPECT_FALSE(UT::targetOf(base, 2, 8, table.lookup()));
	table.rows[base + 28].ui_parentid = static_cast<int>(base + 26);
	EXPECT_FALSE(UT::targetOf(base, 3, 8, table.lookup()));
	table.rows.erase(base + 7);
	EXPECT_FALSE(UT::targetOf(base, 1, 8, table.lookup()));
	EXPECT_FALSE(UT::positionOf(base + 9, table.lookup()));
}

TEST(UpgradeTicket, FromBaseEveryBranch)
{
	const Table table = weaponTable();
	for (std::uint8_t branch = 1; branch <= 3; ++branch)
	{
		for (std::uint8_t level = 8; level <= 9; ++level)
		{
			const auto result = UT::check(base, branch, level, table.lookup(), table.exists());
			EXPECT_EQ(result.verdict, UT::Verdict::Allowed);
			EXPECT_EQ(result.target, base + (branch - 1u) * 10u + level);
		}
	}
	EXPECT_EQ(UT::allowedMask(base, table.lookup(), table.exists()), 0x3F);
}

TEST(UpgradeTicket, SameBranchNeedsAHigherLevel)
{
	const Table table = weaponTable();
	const std::uint32_t power7 = base + 7;
	EXPECT_EQ(UT::check(power7, 1, 8, table.lookup(), table.exists()).verdict, UT::Verdict::Allowed);
	const std::uint32_t power8 = base + 8;
	EXPECT_EQ(UT::check(power8, 1, 8, table.lookup(), table.exists()).verdict, UT::Verdict::AlreadyThere);
	EXPECT_EQ(UT::check(power8, 1, 9, table.lookup(), table.exists()).verdict, UT::Verdict::Allowed);
	const std::uint32_t power9 = base + 9;
	EXPECT_EQ(UT::check(power9, 1, 8, table.lookup(), table.exists()).verdict, UT::Verdict::AlreadyThere);
	EXPECT_EQ(UT::check(power9, 1, 9, table.lookup(), table.exists()).verdict, UT::Verdict::AlreadyThere);
	EXPECT_EQ(UT::allowedMask(power9, table.lookup(), table.exists()), bit(2, 8) | bit(2, 9) | bit(3, 8) | bit(3, 9));
	EXPECT_EQ(UT::allowedMask(power8, table.lookup(), table.exists()), 0x3F & ~bit(1, 8));
}

TEST(UpgradeTicket, BranchChangeIsAllowed)
{
	const Table table = weaponTable();
	const auto result = UT::check(base + 9, 3, 8, table.lookup(), table.exists());
	EXPECT_EQ(result.verdict, UT::Verdict::Allowed);
	EXPECT_EQ(result.target, base + 28);
	EXPECT_EQ(result.from.branch, 1);
	EXPECT_EQ(result.from.level, 9);
}

TEST(UpgradeTicket, Refusals)
{
	Table table = weaponTable();
	EXPECT_EQ(UT::check(4305037, 1, 8, table.lookup(), table.exists()).verdict, UT::Verdict::NotUpgradable);
	EXPECT_EQ(UT::check(base, 1, 7, table.lookup(), table.exists()).verdict, UT::Verdict::BadChoice);
	EXPECT_EQ(UT::check(base, 0, 8, table.lookup(), table.exists()).verdict, UT::Verdict::BadChoice);
	EXPECT_EQ(UT::check(base, 4, 9, table.lookup(), table.exists()).verdict, UT::Verdict::BadChoice);
	table.items.erase(base + 29);
	EXPECT_EQ(UT::check(base, 3, 9, table.lookup(), table.exists()).verdict, UT::Verdict::NoTarget);
	EXPECT_EQ(UT::allowedMask(base, table.lookup(), table.exists()), 0x3F & ~bit(3, 9));
}

TEST(UpgradeTicket, BranchNames)
{
	const Table table = weaponTable();
	EXPECT_EQ(UT::branchNameOf(base, 1, table.lookup()), "Power");
	EXPECT_EQ(UT::branchNameOf(base, 2, table.lookup()), "Run Speed");
	EXPECT_EQ(UT::branchNameOf(base, 3, table.lookup()), "Range");
	EXPECT_EQ(UT::branchNameOf(base, 4, table.lookup()), "");
	const char plain[] = "Firing Rate";
	EXPECT_EQ(UT::branchName(plain, sizeof(plain)), "Firing Rate");
	const char level[] = "Zoom Speed LV12";
	EXPECT_EQ(UT::branchName(level, sizeof(level)), "Zoom Speed");
}
