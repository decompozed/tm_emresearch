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
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <map>
#include <memory>
#include <numeric>
#include <random>
#include <set>
#include <string>
#include <vector>
#include "FakeMatch.h"
#include "HaveApi/BattleRoyaleMap.h"
#include "HaveApi/Catalog.h"
#include "HaveApi/PartyMaps.h"
#include "HaveApi/Modes/BattleRoyaleMode.h"
#include "HaveApi/Modes/BattleRoyaleStorm.h"
#include "HaveApi/PartyRewards.h"
#include "Rooms/PartyStorm.h"

namespace CH = Common::HaveApi;
using namespace Main::HaveApi;
using Tests::FakeMatch;

namespace
{
	using Clock = std::chrono::steady_clock;

	struct Royale
	{
		FakeMatch match;
		BattleRoyaleMode mode;

		Royale(std::size_t players, std::int32_t finalArena, std::int32_t damage = 1, std::uint32_t seed = 7)
			: match{ CH::PartyModeId::BattleRoyale, players, { 10, finalArena, damage } }
			, mode{ seed }
		{
			match.seconds = 600;
			match.map = CH::battleRoyaleMap;
			mode.onMatchStart(match);
			mode.onRoundStart(match);
		}

		void at(std::int64_t second)
		{
			match.elapsed = std::chrono::seconds{ second };
			mode.onTick(match);
		}

		std::vector<CH::Announcement> events() const
		{
			std::vector<CH::Announcement> out;
			for (const auto& [event, text] : match.announcements)
			{
				out.push_back(event);
			}
			return out;
		}

		std::string textOf(CH::Announcement event) const
		{
			const auto found = std::ranges::find(match.announcements, event, &decltype(match.announcements)::value_type::first);
			return found != match.announcements.end() ? found->second : std::string{};
		}
	};

	const BR::ArenaRegion& regionOf(BR::Arena arena)
	{
		return BR::arenas[static_cast<std::size_t>(arena)];
	}

	struct Simulated
	{
		BR::Arena arena{};
		BR::Point at{};
		bool runner{};
	};

	struct Outcome
	{
		std::vector<std::int32_t> placements;
		std::int64_t endedAt{};
		double meanTickMicroseconds{};
		double maxTickMicroseconds{};
		std::size_t stormDeaths{};
		std::size_t announcements{};
		std::int32_t stormClears{};
		std::string winner;
	};

	Outcome playMatch(std::size_t players, std::int32_t finalArena, std::int32_t damage, std::uint32_t seed)
	{
		Royale br{ players, finalArena, damage, seed };
		const CH::BattleSpawnsPayload& spread = br.match.spawnAreas.at(0);
		std::map<std::int32_t, Simulated> world;
		for (std::int32_t i = 0; i < spread.playerCount; ++i)
		{
			const CH::BattleSpawnPlayer& p = spread.players[static_cast<std::size_t>(i)];
			const auto arena = static_cast<BR::Arena>(p.arena);
			const BR::ArenaRegion& region = regionOf(arena);
			world[p.playerId] = { arena, region.finalPoints[static_cast<std::size_t>(i) % region.finalPointCount], i % 3 != 0 };
		}
		const BR::Arena final = br.mode.storm()->finalArena();
		const BR::ArenaRegion& finalRegion = regionOf(final);
		std::mt19937 random{ seed };
		std::set<std::int32_t> dead;
		std::size_t stormDeaths = 0;
		bool left = false;
		double total = 0.0;
		double longest = 0.0;
		std::int64_t ticks = 0;
		Outcome outcome;
		for (std::int64_t second = 0; second <= 600; ++second)
		{
			const double t = static_cast<double>(second);
			for (auto& [id, sim] : world)
			{
				if (sim.runner && sim.arena != final && !br.mode.storm()->contains(t + 20.0, sim.at.x, sim.at.y))
				{
					sim.arena = final;
					sim.at = finalRegion.finalPoints[static_cast<std::size_t>(id) % finalRegion.finalPointCount];
				}
			}
			br.match.outside.clear();
			for (const PartyPlayer& player : br.match.players())
			{
				const Simulated& sim = world[player.playerId];
				if (player.alive && !br.mode.storm()->contains(t, sim.at.x, sim.at.y))
				{
					br.match.outside.push_back(player.playerId);
				}
			}
			const std::size_t killedBefore = br.match.killed.size();
			const auto start = Clock::now();
			br.at(second);
			const double spent = std::chrono::duration<double, std::micro>(Clock::now() - start).count();
			total += spent;
			longest = (std::max)(longest, spent);
			++ticks;
			for (std::size_t k = killedBefore; k < br.match.killed.size(); ++k)
			{
				const std::int32_t id = br.match.killed[k];
				EXPECT_TRUE(dead.insert(id).second);
				const auto victim = std::ranges::find(br.match.players(), id, &PartyPlayer::playerId);
				br.mode.onDeath(br.match, *victim, nullptr);
				++stormDeaths;
			}
			if (second % 37 == 0 && second >= 60)
			{
				std::vector<PartyPlayer*> alive;
				for (PartyPlayer& player : br.match.players())
				{
					if (player.alive)
					{
						alive.push_back(&player);
					}
				}
				if (alive.size() >= 3)
				{
					std::ranges::shuffle(alive, random);
					dead.insert(alive[1]->playerId);
					br.mode.onDeath(br.match, *alive[1], alive[0]);
				}
			}
			if (!left && second == 250)
			{
				for (PartyPlayer& player : br.match.players())
				{
					if (player.alive)
					{
						br.mode.onPlayerLeft(br.match, player);
						left = true;
						break;
					}
				}
			}
			if (br.mode.isRoundOver(br.match))
			{
				br.mode.onRoundEnd(br.match, RoundEndReason::LastStanding);
				outcome.endedAt = second;
				break;
			}
		}
		if (outcome.endedAt == 0)
		{
			br.mode.onRoundEnd(br.match, RoundEndReason::TimeUp);
			outcome.endedAt = 600;
		}
		br.mode.onMatchEnd(br.match);
		for (const PartyPlayer& player : br.match.players())
		{
			outcome.placements.push_back(player.placement);
			if (player.placement == 1)
			{
				outcome.winner = player.name;
			}
		}
		outcome.meanTickMicroseconds = total / static_cast<double>(ticks);
		outcome.maxTickMicroseconds = longest;
		outcome.stormDeaths = stormDeaths;
		outcome.announcements = br.match.announcements.size();
		outcome.stormClears = br.match.stormClears;
		if (!br.match.announcements.empty() && br.match.announcements.back().first == CH::Announcement::BrLastToyStanding)
		{
			EXPECT_EQ(br.match.announcements.back().second, outcome.winner);
		}
		else
		{
			ADD_FAILURE() << "no winner announcement";
		}
		return outcome;
	}
}

TEST(BattleRoyaleAnnouncements, EachArenaIsAnnouncedThirtySecondsBeforeItCloses)
{
	Royale br{ 3, 2 };
	for (std::int64_t second = 0; second <= 300; ++second)
	{
		br.at(second);
	}
	const std::vector<CH::Announcement> expected = { CH::Announcement::BrStudioClosesSoon, CH::Announcement::BrZoneClosing,
		CH::Announcement::BrZoneClosing, CH::Announcement::BrPaperPvcCloseSoon, CH::Announcement::BrZoneClosing };
	EXPECT_EQ(br.events(), expected);
	EXPECT_EQ(br.textOf(CH::Announcement::BrStudioClosesSoon), "THE STUDIO CLOSES IN 30 SECONDS");
	EXPECT_EQ(br.textOf(CH::Announcement::BrPaperPvcCloseSoon), "PAPER LAND + PVC FACTORY CLOSE IN 30 SECONDS");
	EXPECT_EQ(br.match.announcements[1].second, "THE STUDIO CLOSING");
}

TEST(BattleRoyaleAnnouncements, WithTheStudioAsFinalTheSidesAreAnnouncedApart)
{
	Royale br{ 3, 5 };
	for (std::int64_t second = 0; second <= 300; ++second)
	{
		br.at(second);
	}
	const std::vector<CH::Announcement> expected = { CH::Announcement::BrCastleClosesSoon, CH::Announcement::BrZoneClosing,
		CH::Announcement::BrZoneClosing, CH::Announcement::BrPaperClosesSoon, CH::Announcement::BrPvcClosesSoon, CH::Announcement::BrZoneClosing };
	EXPECT_EQ(br.events(), expected);
	EXPECT_EQ(br.textOf(CH::Announcement::BrPvcClosesSoon), "PVC FACTORY CLOSES IN 30 SECONDS");
}

TEST(BattleRoyaleAnnouncements, CastleAndStudioCloseTogetherWhenPaperIsFinal)
{
	Royale br{ 3, 3 };
	for (std::int64_t second = 0; second <= 260; ++second)
	{
		br.at(second);
	}
	EXPECT_EQ(br.events().front(), CH::Announcement::BrPvcClosesSoon);
	EXPECT_EQ(br.textOf(CH::Announcement::BrCastleStudioCloseSoon), "CASTLE + THE STUDIO CLOSE IN 30 SECONDS");
}

TEST(BattleRoyaleAnnouncements, AWarningMissedByALateTickIsNotShownAfterTheClosingStarted)
{
	Royale br{ 3, 2 };
	br.at(100);
	br.at(151);
	EXPECT_EQ(br.events(), std::vector<CH::Announcement>{ CH::Announcement::BrZoneClosing });
}

TEST(BattleRoyaleAnnouncements, ThePlayerEnteringTheStormIsToldOnceNotAtEveryStep)
{
	Royale br{ 3, 2 };
	br.at(159);
	br.match.outside = { 101 };
	br.at(160);
	br.match.outside.clear();
	br.at(161);
	br.match.outside = { 101 };
	br.at(162);
	EXPECT_EQ(br.match.playerAnnouncements.size(), 1u);
	br.match.outside.clear();
	br.at(163);
	br.match.outside = { 101 };
	br.at(172);
	ASSERT_EQ(br.match.playerAnnouncements.size(), 2u);
	EXPECT_EQ(br.match.playerAnnouncements.back(), std::make_pair(CH::Announcement::BrInStorm, 101));
}

TEST(BattleRoyaleAnnouncements, CalmPhasesKeepTheTextLine)
{
	Royale br{ 3, 2 };
	br.at(435);
	ASSERT_FALSE(br.match.texts.empty());
	EXPECT_EQ(br.match.texts.back().second, "STORM IN");
}

TEST(BattleRoyaleSpawns, SixteenPlayersAreSpreadFourPerArena)
{
	Royale br{ 16, 1 };
	ASSERT_EQ(br.match.spawnAreas.size(), 1u);
	const CH::BattleSpawnsPayload& areas = br.match.spawnAreas.front();
	EXPECT_EQ(areas.playerCount, 16);
	std::map<std::int32_t, int> perArena;
	std::set<std::int32_t> ids;
	for (std::int32_t i = 0; i < areas.playerCount; ++i)
	{
		++perArena[areas.players[static_cast<std::size_t>(i)].arena];
		ids.insert(areas.players[static_cast<std::size_t>(i)].playerId);
	}
	EXPECT_EQ(ids.size(), 16u);
	for (std::int32_t arena = 0; arena < 4; ++arena)
	{
		EXPECT_EQ(perArena[arena], 4);
	}
	ASSERT_EQ(areas.boxCount, 9);
	for (std::int32_t i = 0; i < areas.boxCount; ++i)
	{
		const CH::BattleSpawnBox& box = areas.boxes[static_cast<std::size_t>(i)];
		const BR::ArenaRegion& region = BR::arenas[static_cast<std::size_t>(box.arena)];
		bool inRegion = false;
		for (std::size_t b = region.firstBox; b < static_cast<std::size_t>(region.firstBox + region.boxCount); ++b)
		{
			inRegion = inRegion || (BR::boxes[b].x1 == box.x1 && BR::boxes[b].x2 == box.x2 && BR::boxes[b].y1 == box.y1 && BR::boxes[b].y2 == box.y2);
		}
		EXPECT_TRUE(inRegion);
	}
}

TEST(BattleRoyaleSpawns, FewPlayersGoToTheLargestArenasFirst)
{
	Royale br{ 5, 1 };
	const CH::BattleSpawnsPayload& areas = br.match.spawnAreas.front();
	std::map<std::int32_t, int> perArena;
	for (std::int32_t i = 0; i < areas.playerCount; ++i)
	{
		++perArena[areas.players[static_cast<std::size_t>(i)].arena];
	}
	EXPECT_EQ(perArena[static_cast<std::int32_t>(BR::Arena::Paper)], 2);
	EXPECT_EQ(perArena[static_cast<std::int32_t>(BR::Arena::Castle)], 1);
	EXPECT_EQ(perArena[static_cast<std::int32_t>(BR::Arena::Pvc)], 1);
	EXPECT_EQ(perArena[static_cast<std::int32_t>(BR::Arena::Studio)], 1);
}

TEST(BattleRoyaleRewards, ThePlacementBonusGrowsWithThePlayersBeaten)
{
	EXPECT_EQ(placementBonusMinutesFor(1, 2), 0u);
	EXPECT_EQ(placementBonusMinutesFor(1, 3), 1u);
	EXPECT_EQ(placementBonusMinutesFor(2, 3), 0u);
	EXPECT_EQ(placementBonusMinutesFor(1, 4), 2u);
	EXPECT_EQ(placementBonusMinutesFor(2, 4), 1u);
	EXPECT_EQ(placementBonusMinutesFor(3, 4), 0u);
	EXPECT_EQ(placementBonusMinutesFor(1, 5), 3u);
	EXPECT_EQ(placementBonusMinutesFor(2, 5), 2u);
	EXPECT_EQ(placementBonusMinutesFor(3, 5), 1u);
	EXPECT_EQ(placementBonusMinutesFor(1, 16), 3u);
	EXPECT_EQ(placementBonusMinutesFor(3, 16), 1u);
	EXPECT_EQ(placementBonusMinutesFor(4, 16), 0u);
	EXPECT_EQ(placementBonusMinutesFor(0, 16), 0u);
}

TEST(BattleRoyaleRewards, MinutesCountUntilTheEliminationAndAreCapped)
{
	constexpr std::uint64_t start = 1'000'000;
	EXPECT_EQ(playedMinutes(start, start + 600'000, start + 179'000, 15), 2u);
	EXPECT_EQ(playedMinutes(start, start + 600'000, start + 900'000, 15), 10u);
	EXPECT_EQ(playedMinutes(start, start + 3'600'000, start + 3'600'000, 15), 15u);
	EXPECT_EQ(playedMinutes(start, start - 1, start - 1, 15), 0u);
}

TEST(BattleRoyaleLoad, SixteenPlayerMatchesEndWithEveryPlaceGivenOnce)
{
	for (std::int32_t finalArena = 1; finalArena <= 5; ++finalArena)
	{
		for (std::int32_t damage = 1; damage <= 2; ++damage)
		{
			const Outcome outcome = playMatch(16, finalArena, damage, static_cast<std::uint32_t>(finalArena * 10 + damage));
			std::vector<std::int32_t> places = outcome.placements;
			std::ranges::sort(places);
			std::vector<std::int32_t> expected(16);
			std::iota(expected.begin(), expected.end(), 1);
			EXPECT_EQ(places, expected) << "final " << finalArena << " damage " << damage;
			EXPECT_GT(outcome.stormDeaths, 0u);
			EXPECT_LE(outcome.endedAt, 600);
			EXPECT_EQ(outcome.stormClears, 1);
			EXPECT_LT(outcome.meanTickMicroseconds, 1000.0);
			std::printf("[BattleRoyaleLoad] final %d damage %d: ended at %llds, storm deaths %zu, announcements %zu, tick mean %.2f us, max %.2f us\n",
				finalArena, damage, static_cast<long long>(outcome.endedAt), outcome.stormDeaths, outcome.announcements,
				outcome.meanTickMicroseconds, outcome.maxTickMicroseconds);
		}
	}
}

TEST(BattleRoyaleLoad, ThirtyTwoRoomsOfSixteenTickInAFractionOfASecond)
{
	constexpr std::size_t rooms = 32;
	std::vector<std::unique_ptr<Royale>> matches;
	for (std::size_t room = 0; room < rooms; ++room)
	{
		matches.push_back(std::make_unique<Royale>(16, static_cast<std::int32_t>(1 + room % 5), static_cast<std::int32_t>(1 + room % 2), static_cast<std::uint32_t>(room)));
		for (std::int32_t id = 100; id < 108; ++id)
		{
			matches.back()->match.outside.push_back(id);
		}
	}
	double longest = 0.0;
	double total = 0.0;
	for (std::int64_t second = 0; second < 600; ++second)
	{
		const auto start = Clock::now();
		for (auto& m : matches)
		{
			m->at(second);
		}
		const double spent = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
		longest = (std::max)(longest, spent);
		total += spent;
	}
	std::printf("[BattleRoyaleLoad] %zu rooms x 16 players, MainServer storm tick: mean %.3f ms, max %.3f ms per second of play\n", rooms,
		total / 600.0, longest);
	EXPECT_LT(total / 600.0, 50.0);
}

TEST(BattleRoyaleLoad, CastServerStormCostPerPositionStaysTiny)
{
	using Cast::Domain::PartyLava;
	using Cast::Domain::PartyStorm;
	constexpr std::size_t rooms = 32;
	constexpr std::size_t members = 16;
	const StormPlan plan{ BR::Arena::Paper, BR::arenas[1].finalPoints[0], 600.0 };
	std::vector<PartyStorm> storms(rooms);
	std::vector<PartyLava> positions(rooms);
	const auto t0 = PartyStorm::Clock::now();
	const CH::BattleZonePayload zone = plan.zoneAt(160.0, 2000);
	for (std::size_t room = 0; room < rooms; ++room)
	{
		CH::StormCommand command{};
		command.action = CH::StormAction::Set;
		for (std::int32_t i = 0; i < zone.boxCount; ++i)
		{
			const CH::BattleZoneBox& b = zone.boxes[static_cast<std::size_t>(i)];
			command.boxes[command.boxCount++] = { b.x1, b.x1Speed, b.x2, b.x2Speed, b.y1, b.y1Speed, b.y2, b.y2Speed, b.moveSeconds };
		}
		for (std::size_t m = 0; m < members; ++m)
		{
			command.members[command.memberCount++] = static_cast<std::uint32_t>(room * 100 + m + 1);
		}
		storms[room].set(command, t0);
	}
	std::mt19937 random{ 1 };
	std::uniform_real_distribution<float> x{ -8000.0f, 15000.0f };
	std::uniform_real_distribution<float> y{ -12000.0f, 11000.0f };
	std::size_t updates = 0;
	std::size_t changes = 0;
	const auto start = Clock::now();
	for (int step = 0; step < 600; ++step)
	{
		const auto now = t0 + std::chrono::milliseconds{ 100 * step };
		for (std::size_t room = 0; room < rooms; ++room)
		{
			for (std::size_t m = 0; m < members; ++m)
			{
				const auto session = static_cast<std::uint32_t>(room * 100 + m + 1);
				const float px = x(random);
				const float py = y(random);
				positions[room].setPosition(session, px, py, 0.0f, now);
				CH::StormAnswer changed{};
				changes += storms[room].update(session, px, py, changed, now) ? 1 : 0;
				++updates;
			}
			if (step % 10 == 0)
			{
				const CH::StormAnswer answer = storms[room].evaluate(positions[room], now);
				EXPECT_LE(answer.count, members);
			}
		}
	}
	const double micro = std::chrono::duration<double, std::micro>(Clock::now() - start).count();
	std::printf("[BattleRoyaleLoad] CastServer: %zu position updates (%zu rooms x %zu players, 10 a second for 60 s), %zu outside changes, %.3f us per update with the query every second\n",
		updates, rooms, members, changes, micro / static_cast<double>(updates));
	EXPECT_LT(micro / static_cast<double>(updates), 20.0);
}
