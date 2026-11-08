#include <gtest/gtest.h>
#include "AntiCheat/FlagRecord.h"

TEST(CheatFlagRecord, InsertFillsEveryNotNullColumn)
{
	EXPECT_NE(Ac::insertFlagSql.find("(Description, Timestamp)"), std::string_view::npos);
	EXPECT_NE(Ac::insertFlagSql.find("VALUES (?, NOW())"), std::string_view::npos);
}

TEST(CheatFlagRecord, CreatedTableMatchesTheSchema)
{
	EXPECT_NE(Ac::createFlagsTableSql.find("Description TEXT NOT NULL"), std::string_view::npos);
	EXPECT_NE(Ac::createFlagsTableSql.find("Timestamp DATETIME NOT NULL"), std::string_view::npos);
}

TEST(CheatFlagRecord, DescriptionNamesTheAccount)
{
	const Ac::ACFlag flag{ 77, "Replay", "5 identical packets" };
	EXPECT_EQ(Ac::describeFlag(flag), "CheatType: Replay, AccountID: 77, Details: 5 identical packets");
}
