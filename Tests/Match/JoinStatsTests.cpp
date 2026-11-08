#include <gtest/gtest.h>
#include "../Combat/FakeWorld.h"
#include "Enums/GameEnums.h"
#include "Match/Rules.h"
#include "Structures/Match/JoinStatePackets.h"

using namespace CombatTests;

namespace
{
	constexpr std::uint32_t joiner = 3;

	PlayerJoinStats stats(std::uint32_t session)
	{
		PlayerJoinStats entry{};
		entry.stats.meleeKills = 1;
		entry.stats.rifleKills = 2;
		entry.stats.shotgunKills = 3;
		entry.stats.sniperKills = 4;
		entry.stats.mgKills = 5;
		entry.stats.bazookaKills = 6;
		entry.stats.grenadeKills = 7;
		entry.stats.killstreak = 0xEE;
		entry.stats.totalKills = 8;
		entry.stats.totalDeaths = 9;
		entry.stats.headshots = 10;
		entry.stats.assists = 11;
		entry.stats.modeCounter = 12;
		entry.stats.roundsWon = 13;
		entry.stats.unusedBits = 0xF;
		entry.stats.unused = { 0xAA, 0xBB };
		entry.player = uid(session);
		entry.respawnTick = 0x1234;
		return entry;
	}

	Frame joinStatsFrame(std::initializer_list<PlayerJoinStats> entries)
	{
		Frame frame{ 306, 0, static_cast<std::uint8_t>(entries.size()) };
		frame.to(joiner);
		for (const auto& entry : entries)
		{
			frame.add(entry);
		}
		return frame;
	}
}

TEST(JoinStats, ForwardsTheRebuiltStatsThenTheServerState)
{
	FakeWorld world;
	world.players[4].dead = true;
	auto frame = joinStatsFrame({ stats(2), stats(4) });
	EXPECT_EQ(Cast::Match::joinStats(world, frame.packet()).route, Route::Handled);
	ASSERT_EQ(world.sent.size(), 2u);

	const auto& original = world.sent[0];
	EXPECT_EQ(original.route, Route::Recipient);
	EXPECT_EQ(original.recipient, joiner);
	EXPECT_EQ(original.order, 306);
	EXPECT_EQ(original.mission, 0);
	EXPECT_EQ(original.extra, 0);
	EXPECT_EQ(original.option, 2);
	ASSERT_EQ(original.payload.size(), 2 * sizeof(PlayerJoinStats));
	const auto first = original.at<PlayerJoinStats>(0);
	EXPECT_EQ(first.stats.meleeKills, 1);
	EXPECT_EQ(first.stats.rifleKills, 2);
	EXPECT_EQ(first.stats.shotgunKills, 3);
	EXPECT_EQ(first.stats.sniperKills, 4);
	EXPECT_EQ(first.stats.mgKills, 5);
	EXPECT_EQ(first.stats.bazookaKills, 6);
	EXPECT_EQ(first.stats.grenadeKills, 7);
	EXPECT_EQ(first.stats.killstreak, 0);
	EXPECT_EQ(first.stats.totalKills, 8);
	EXPECT_EQ(first.stats.totalDeaths, 9);
	EXPECT_EQ(first.stats.headshots, 10);
	EXPECT_EQ(first.stats.assists, 11);
	EXPECT_EQ(first.stats.modeCounter, 12);
	EXPECT_EQ(first.stats.roundsWon, 13);
	EXPECT_EQ(first.stats.unusedBits, 0);
	EXPECT_EQ(first.stats.unused[0], 0);
	EXPECT_EQ(first.stats.unused[1], 0);
	EXPECT_EQ(raw(first.player), raw(uid(2)));
	EXPECT_EQ(first.respawnTick, 0x1234u);
	EXPECT_EQ(raw(original.at<PlayerJoinStats>(sizeof(PlayerJoinStats)).player), raw(uid(4)));

	const auto& state = world.sent[1];
	EXPECT_EQ(state.route, Route::Recipient);
	EXPECT_EQ(state.recipient, joiner);
	EXPECT_EQ(state.order, 306);
	EXPECT_EQ(state.mission, serverJoinStateMission);
	EXPECT_EQ(state.extra, 0);
	EXPECT_EQ(state.option, 2);
	ASSERT_EQ(state.payload.size(), 2 * sizeof(PlayerJoinState));
	const auto alive = state.at<PlayerJoinState>(0);
	EXPECT_EQ(raw(alive.player), raw(uid(2)));
	EXPECT_EQ(alive.hp, fullHp);
	EXPECT_EQ(alive.weaponSlot, 0u);
	EXPECT_EQ(alive.state, static_cast<std::uint32_t>(Common::Enums::STATE_NORMAL));
	EXPECT_EQ(alive.unused, 0u);
	const auto dead = state.at<PlayerJoinState>(sizeof(PlayerJoinState));
	EXPECT_EQ(raw(dead.player), raw(uid(4)));
	EXPECT_EQ(dead.hp, 0u);
	EXPECT_EQ(dead.state, static_cast<std::uint32_t>(Common::Enums::STATE_DYING));
}

TEST(JoinStats, UnknownPlayersOnlyInTheForwardedStats)
{
	FakeWorld world;
	auto frame = joinStatsFrame({ stats(9), stats(2) });
	EXPECT_EQ(Cast::Match::joinStats(world, frame.packet()).route, Route::Handled);
	ASSERT_EQ(world.sent.size(), 2u);
	EXPECT_EQ(world.sent[0].option, 2);
	EXPECT_EQ(world.sent[1].option, 1);
	EXPECT_EQ(raw(world.sent[1].at<PlayerJoinState>(0).player), raw(uid(2)));
}

TEST(JoinStats, NoServerStateWithoutKnownPlayers)
{
	FakeWorld world;
	auto frame = joinStatsFrame({ stats(9) });
	EXPECT_EQ(Cast::Match::joinStats(world, frame.packet()).route, Route::Handled);
	ASSERT_EQ(world.sent.size(), 1u);
	EXPECT_EQ(world.sent[0].mission, 0);
}

TEST(JoinStats, SizeMustMatchTheCount)
{
	FakeWorld world;
	auto frame = joinStatsFrame({ stats(2) });
	frame.option = 2;
	EXPECT_EQ(Cast::Match::joinStats(world, frame.packet()).route, Route::Drop);
	Frame empty{ 306, 0, 0 };
	empty.to(joiner);
	EXPECT_EQ(Cast::Match::joinStats(world, empty.packet()).route, Route::Drop);
	Frame tooMany{ 306, 0, 17 };
	tooMany.to(joiner);
	for (int i = 0; i < 17; ++i)
	{
		tooMany.add(stats(2));
	}
	EXPECT_EQ(Cast::Match::joinStats(world, tooMany.packet()).route, Route::Drop);
	EXPECT_TRUE(world.sent.empty());
}

TEST(JoinStats, ForgedServerMissionDropped)
{
	FakeWorld world;
	auto frame = joinStatsFrame({ stats(2) });
	frame.withMission(serverJoinStateMission);
	EXPECT_EQ(Cast::Match::joinStats(world, frame.packet()).route, Route::Drop);
	auto withExtra = joinStatsFrame({ stats(2) });
	withExtra.extra = 1;
	EXPECT_EQ(Cast::Match::joinStats(world, withExtra.packet()).route, Route::Drop);
	EXPECT_TRUE(world.sent.empty());
}

TEST(JoinStats, BadKeyDropped)
{
	FakeWorld world;
	auto entry = stats(2);
	entry.player.unknown = 1;
	auto frame = joinStatsFrame({ entry });
	EXPECT_EQ(Cast::Match::joinStats(world, frame.packet()).route, Route::Drop);
	EXPECT_TRUE(world.sent.empty());
}

TEST(JoinStats, OnlyFromTheHostToAnAttachedMember)
{
	FakeWorld world;
	auto fromGuest = joinStatsFrame({ stats(2) });
	fromGuest.from(2);
	EXPECT_EQ(Cast::Match::joinStats(world, fromGuest.packet()).route, Route::Drop);
	auto toHost = joinStatsFrame({ stats(2) });
	toHost.to(hostSession);
	EXPECT_EQ(Cast::Match::joinStats(world, toHost.packet()).route, Route::Drop);
	world.detached.insert(joiner);
	auto toDetached = joinStatsFrame({ stats(2) });
	EXPECT_EQ(Cast::Match::joinStats(world, toDetached.packet()).route, Route::Drop);
	EXPECT_TRUE(world.sent.empty());
}
