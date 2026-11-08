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
#include <cmath>
#include <string>
#include "FakeMatch.h"
#include "HaveApi/BattleRoyaleMap.h"
#include "HaveApi/Catalog.h"
#include "HaveApi/Messages.h"
#include "HaveApi/PartyMaps.h"
#include "HaveApi/Modes/BattleRoyaleMode.h"
#include "HaveApi/Modes/BattleRoyaleStorm.h"

namespace CH = Common::HaveApi;
using namespace Main::HaveApi;

namespace
{
	using Tests::FakeMatch;

	const BR::ArenaRegion& regionOf(BR::Arena arena)
	{
		return BR::arenas[static_cast<std::size_t>(arena)];
	}

	StormPlan plan(BR::Arena final, double seconds = 600.0)
	{
		return StormPlan{ final, regionOf(final).finalPoints[0], seconds };
	}

	bool arenaOpen(const StormPlan& storm, BR::Arena arena, double t)
	{
		const auto& r = regionOf(arena);
		for (std::size_t box = r.firstBox; box < static_cast<std::size_t>(r.firstBox + r.boxCount); ++box)
		{
			if (!storm.rectAt(box, t).empty())
			{
				return true;
			}
		}
		return false;
	}

	const BR::Corridor& corridor(BR::Arena a, BR::Arena b)
	{
		for (const auto& c : BR::corridors)
		{
			if ((c.low == a && c.high == b) || (c.low == b && c.high == a))
			{
				return c;
			}
		}
		return BR::corridors.front();
	}

	CH::StormRect base(std::size_t box)
	{
		return { BR::boxes[box].x1, BR::boxes[box].x2, BR::boxes[box].y1, BR::boxes[box].y2 };
	}

	struct Royale
	{
		FakeMatch match;
		BattleRoyaleMode mode{ 7 };

		explicit Royale(std::int32_t finalArena = 3, std::int32_t damage = 1)
			: match{ CH::PartyModeId::BattleRoyale, 3, { 10, finalArena, damage } }
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
	};
}

TEST(BattleRoyaleMap, RegionsDoNotOverlapAndCorridorsJoinTheirArenas)
{
	for (std::size_t a = 0; a < BR::boxes.size(); ++a)
	{
		for (std::size_t b = a + 1; b < BR::boxes.size(); ++b)
		{
			const auto& p = BR::boxes[a];
			const auto& q = BR::boxes[b];
			const float w = std::min(p.x2, q.x2) - std::max(p.x1, q.x1);
			const float h = std::min(p.y2, q.y2) - std::max(p.y1, q.y1);
			EXPECT_FALSE(w > 50.0f && h > 50.0f) << a << " " << b;
		}
	}
	for (const auto& c : BR::corridors)
	{
		const auto& box = BR::boxes[c.box];
		EXPECT_LT(box.x1, box.x2);
		EXPECT_LT(box.y1, box.y2);
		EXPECT_NE(c.low, c.high);
	}
	for (const auto& arena : BR::arenas)
	{
		EXPECT_GT(arena.finalPointCount, 0);
		for (std::size_t i = 0; i < arena.finalPointCount; ++i)
		{
			const auto& p = arena.finalPoints[i];
			EXPECT_TRUE((CH::StormRect{ arena.exclusion.x1, arena.exclusion.x2, arena.exclusion.y1, arena.exclusion.y2 }.contains(p.x, p.y)));
		}
	}
}

TEST(StormPlan, TheOppositeArenaClosesFirstThenTheSidesThenTheFinal)
{
	const StormPlan storm = plan(BR::Arena::Paper);
	EXPECT_EQ(storm.opposite(), BR::Arena::Pvc);
	EXPECT_EQ(storm.sides()[0], BR::Arena::Castle);
	EXPECT_EQ(storm.sides()[1], BR::Arena::Studio);
	for (const auto arena : { BR::Arena::Castle, BR::Arena::Paper, BR::Arena::Pvc, BR::Arena::Studio })
	{
		EXPECT_TRUE(arenaOpen(storm, arena, 149.0));
	}
	EXPECT_TRUE(arenaOpen(storm, BR::Arena::Pvc, 200.0));
	EXPECT_FALSE(arenaOpen(storm, BR::Arena::Pvc, 210.0));
	const auto& pvcCastle = corridor(BR::Arena::Pvc, BR::Arena::Castle);
	EXPECT_EQ(storm.rectAt(pvcCastle.box, 210.0), base(pvcCastle.box));
	EXPECT_TRUE(storm.rectAt(pvcCastle.box, 270.0).empty());
	EXPECT_TRUE(arenaOpen(storm, BR::Arena::Castle, 329.0));
	EXPECT_FALSE(arenaOpen(storm, BR::Arena::Castle, 330.0));
	EXPECT_FALSE(arenaOpen(storm, BR::Arena::Studio, 330.0));
	const auto& castlePaper = corridor(BR::Arena::Castle, BR::Arena::Paper);
	EXPECT_FALSE(storm.rectAt(castlePaper.box, 360.0).empty());
	EXPECT_TRUE(storm.rectAt(castlePaper.box, 390.0).empty());
	const auto& paper = regionOf(BR::Arena::Paper);
	for (std::size_t box = paper.firstBox; box < static_cast<std::size_t>(paper.firstBox + paper.boxCount); ++box)
	{
		EXPECT_EQ(storm.rectAt(box, 390.0), base(box));
	}
}

TEST(StormPlan, ACorridorClosesFromTheArenaThatClosedBeforeIt)
{
	const StormPlan storm = plan(BR::Arena::Paper);
	const auto& c = corridor(BR::Arena::Pvc, BR::Arena::Castle);
	const auto half = storm.rectAt(c.box, 240.0);
	const auto start = base(c.box);
	const BR::Point& pvcMouth = c.low == BR::Arena::Pvc ? c.lowMouth : c.highMouth;
	EXPECT_FALSE(half.contains(pvcMouth.x, pvcMouth.y + (c.low == BR::Arena::Pvc ? 1.0f : -1.0f)));
	EXPECT_NEAR(half.y2 - half.y1, (start.y2 - start.y1) / 2.0f, 1.0f);
}

TEST(StormPlan, TheMouthsOfTheFinalArenaEndInTheStormAndTheLastZoneIsAroundTheFinalPoint)
{
	for (const auto final : { BR::Arena::Castle, BR::Arena::Paper, BR::Arena::Pvc, BR::Arena::Studio })
	{
		const StormPlan storm = plan(final);
		const auto& p = regionOf(final).finalPoints[0];
		for (const auto& c : BR::corridors)
		{
			if (c.low == final || c.high == final)
			{
				const BR::Point& m = c.low == final ? c.lowMouth : c.highMouth;
				EXPECT_FALSE(storm.contains(435.0, m.x, m.y)) << static_cast<int>(final);
			}
		}
		EXPECT_TRUE(storm.contains(435.0, p.x, p.y));
		EXPECT_TRUE(storm.contains(584.0, p.x, p.y));
		EXPECT_FALSE(storm.contains(584.0, p.x + 900.0f, p.y));
		EXPECT_FALSE(storm.contains(600.0, p.x, p.y));
	}
}

TEST(StormPlan, TheStudioAsFinalStaggersTheTwoSides)
{
	const StormPlan storm = plan(BR::Arena::Studio);
	EXPECT_EQ(storm.opposite(), BR::Arena::Castle);
	EXPECT_FALSE(arenaOpen(storm, BR::Arena::Paper, 330.0));
	EXPECT_TRUE(arenaOpen(storm, BR::Arena::Pvc, 330.0));
	EXPECT_FALSE(arenaOpen(storm, BR::Arena::Pvc, 355.0));
}

TEST(StormPlan, PhasesDamageAndDeathTimersFollowTheCalendar)
{
	const StormPlan storm = plan(BR::Arena::Castle);
	EXPECT_EQ(storm.phaseAt(10.0).damagePermille, 0);
	EXPECT_EQ(storm.phaseAt(160.0).damagePermille, 15);
	EXPECT_EQ(storm.phaseAt(160.0).label, "THE STUDIO CLOSING");
	EXPECT_EQ(storm.phaseAt(280.0).damagePermille, 30);
	EXPECT_EQ(storm.phaseAt(400.0).damagePermille, 50);
	EXPECT_EQ(storm.phaseAt(500.0).damagePermille, 80);
	EXPECT_EQ(storm.phaseAt(560.0).damagePermille, 120);
	EXPECT_EQ(storm.phaseAt(590.0).damagePermille, 200);
	EXPECT_EQ(storm.phaseAt(380.0).deathSeconds, 5);
	EXPECT_EQ(storm.phaseAt(400.0).deathSeconds, 3);
	EXPECT_EQ(storm.phaseAt(560.0).deathSeconds, 1);
	EXPECT_EQ(storm.phaseAt(700.0).damagePermille, 200);
}

TEST(StormPlan, ShorterAndLongerMatchesScaleTheCalendar)
{
	const StormPlan eight = plan(BR::Arena::Paper, 480.0);
	EXPECT_TRUE(arenaOpen(eight, BR::Arena::Pvc, 119.0));
	EXPECT_FALSE(arenaOpen(eight, BR::Arena::Pvc, 168.0));
	EXPECT_FALSE(eight.contains(480.0, regionOf(BR::Arena::Paper).finalPoints[0].x, regionOf(BR::Arena::Paper).finalPoints[0].y));
	const StormPlan twelve = plan(BR::Arena::Paper, 720.0);
	EXPECT_TRUE(arenaOpen(twelve, BR::Arena::Pvc, 179.0));
	EXPECT_DOUBLE_EQ(twelve.phaseAt(200.0).start, 180.0);
}

TEST(StormPlan, TheZoneMessageMovesEveryBoxToTheNextKeyAndDrawsOnlyMovedSides)
{
	const StormPlan storm = plan(BR::Arena::Paper);
	const auto zone = storm.zoneAt(160.0, 2000);
	EXPECT_EQ(zone.active, 1);
	EXPECT_EQ(zone.boxCount, static_cast<std::int32_t>(BR::boxes.size()));
	EXPECT_EQ(zone.damagePerSecond, 30);
	EXPECT_EQ(zone.phaseEndsInMs, 50000);
	for (std::size_t i = 0; i < BR::boxes.size(); ++i)
	{
		const auto& b = zone.boxes[i];
		const auto then = storm.rectAt(i, 160.0 + b.moveSeconds);
		EXPECT_NEAR(b.x1 + b.x1Speed * b.moveSeconds, then.x1, 0.5f);
		EXPECT_NEAR(b.y2 + b.y2Speed * b.moveSeconds, then.y2, 0.5f);
	}
	const auto& pvc = regionOf(BR::Arena::Pvc);
	EXPECT_NE(zone.boxes[pvc.firstBox].drawSides, 0);
	EXPECT_EQ(zone.boxes[regionOf(BR::Arena::Paper).firstBox].drawSides, 0);
	EXPECT_EQ(zone.boxes[corridor(BR::Arena::Castle, BR::Arena::Paper).box].kind, 1);
	EXPECT_EQ(zone.boxes[pvc.firstBox].kind, 0);
}

TEST(StormPlan, TheFinalArenaFollowsTheWeights)
{
	EXPECT_EQ(StormPlan::weightedArena(0.0), BR::Arena::Paper);
	EXPECT_EQ(StormPlan::weightedArena(0.34), BR::Arena::Paper);
	EXPECT_EQ(StormPlan::weightedArena(0.36), BR::Arena::Pvc);
	EXPECT_EQ(StormPlan::weightedArena(0.66), BR::Arena::Castle);
	EXPECT_EQ(StormPlan::weightedArena(0.95), BR::Arena::Studio);
}

TEST(BattleRoyaleStorm, TheStormIsSentAtTheStartAndAtEveryKeyThenCleared)
{
	Royale br{ 3 };
	ASSERT_EQ(br.match.storms.size(), 1u);
	EXPECT_EQ(br.mode.storm()->finalArena(), BR::Arena::Paper);
	br.at(100);
	EXPECT_EQ(br.match.storms.size(), 1u);
	br.at(150);
	EXPECT_EQ(br.match.storms.size(), 2u);
	EXPECT_EQ(br.match.storms.back().phase, 1);
	br.mode.onRoundEnd(br.match, RoundEndReason::LastStanding);
	EXPECT_EQ(br.match.stormClears, 1);
	EXPECT_EQ(br.mode.storm(), nullptr);
}

TEST(BattleRoyaleStorm, ThePhaseIsAnnouncedAndTheExitsPhaseShowsTheRadarForFiveSeconds)
{
	Royale br{ 2 };
	br.at(150);
	ASSERT_FALSE(br.match.announcements.empty());
	EXPECT_EQ(br.match.announcements.back(), std::make_pair(CH::Announcement::BrZoneClosing, std::string{ "THE STUDIO CLOSING" }));
	br.at(390);
	ASSERT_EQ(br.match.radar.size(), 1u);
	EXPECT_TRUE(br.match.radar.back());
	br.at(394);
	EXPECT_EQ(br.match.radar.size(), 1u);
	br.at(395);
	ASSERT_EQ(br.match.radar.size(), 2u);
	EXPECT_FALSE(br.match.radar.back());
}

TEST(BattleRoyaleStorm, OutsideTheStormThePlayerDiesAfterTheCountdown)
{
	Royale br;
	br.match.outside = { 100 };
	br.at(100);
	EXPECT_TRUE(br.match.texts.empty());
	br.at(160);
	ASSERT_FALSE(br.match.texts.empty());
	EXPECT_EQ(br.match.texts.back(), std::make_pair(100, std::string{ "LEAVE THE STORM! 5" }));
	br.at(163);
	EXPECT_EQ(br.match.texts.back().second, "LEAVE THE STORM! 2");
	EXPECT_TRUE(br.match.killed.empty());
	br.at(165);
	ASSERT_EQ(br.match.killed.size(), 1u);
	EXPECT_EQ(br.match.killed.front(), 100);
	br.at(166);
	EXPECT_EQ(br.match.killed.size(), 1u);
	EXPECT_TRUE(br.match.stormDamage.empty());
}

TEST(BattleRoyaleStorm, BackInsideTheCountdownStartsAgain)
{
	Royale br;
	br.match.outside = { 101 };
	br.at(160);
	br.at(163);
	br.match.outside.clear();
	br.at(164);
	br.match.outside = { 101 };
	br.at(165);
	EXPECT_EQ(br.match.texts.back().second, "LEAVE THE STORM! 5");
	br.at(169);
	EXPECT_TRUE(br.match.killed.empty());
	br.at(170);
	EXPECT_EQ(br.match.killed.size(), 1u);
}

TEST(BattleRoyaleStorm, LaterPhasesKillFaster)
{
	Royale br;
	br.match.outside = { 102 };
	br.at(560);
	EXPECT_EQ(br.match.texts.back().second, "LEAVE THE STORM! 1");
	br.at(561);
	EXPECT_EQ(br.match.killed.size(), 1u);
}

TEST(BattleRoyaleStorm, GradualDamageGoesToEveryClientAndTheServerKillsPastTheCap)
{
	Royale br{ 3, 2 };
	br.match.outside = { 100 };
	br.at(160);
	ASSERT_EQ(br.match.stormDamage.size(), 1u);
	ASSERT_EQ(br.match.stormDamage.back().size(), 1u);
	EXPECT_EQ(br.match.stormDamage.back().front().playerId, 100);
	EXPECT_EQ(br.match.stormDamage.back().front().damage, 30);
	ASSERT_EQ(br.match.playerAnnouncements.size(), 1u);
	EXPECT_EQ(br.match.playerAnnouncements.back(), std::make_pair(CH::Announcement::BrInStorm, 100));
	EXPECT_EQ(br.match.playerAnnouncementTexts.back(), "YOU ARE IN THE STORM!");
	br.match.outside = { 100, 101 };
	for (std::int64_t second = 586; second < 593; ++second)
	{
		br.at(second);
	}
	EXPECT_EQ(br.mode.stormDamageOf(100), 30 + 7 * 400);
	EXPECT_EQ(br.match.stormDamage.back().size(), 2u);
	EXPECT_TRUE(br.match.killed.empty());
	br.at(593);
	ASSERT_EQ(br.match.killed.size(), 1u);
	EXPECT_EQ(br.match.killed.front(), 100);
	br.at(594);
	ASSERT_EQ(br.match.killed.size(), 2u);
	EXPECT_EQ(br.match.killed.back(), 101);
	br.at(595);
	EXPECT_EQ(br.match.killed.size(), 2u);
}

TEST(BattleRoyaleStorm, TheHostReportsALethalHitOnlyForAPlayerRecentlyOutside)
{
	Royale br{ 3, 2 };
	br.at(160);
	br.mode.onStormLethal(br.match, br.match.player(0));
	EXPECT_TRUE(br.match.killed.empty());
	br.match.recentlyOutside = { 100 };
	br.mode.onStormLethal(br.match, br.match.player(0));
	ASSERT_EQ(br.match.killed.size(), 1u);
	br.mode.onStormLethal(br.match, br.match.player(0));
	EXPECT_EQ(br.match.killed.size(), 1u);
	br.match.recentlyOutside = { 101 };
	br.match.player(1).alive = false;
	br.mode.onStormLethal(br.match, br.match.player(1));
	EXPECT_EQ(br.match.killed.size(), 1u);
}

TEST(BattleRoyaleStorm, WithTheTimerTheHostCannotKill)
{
	Royale br{ 3, 1 };
	br.match.recentlyOutside = { 100 };
	br.at(160);
	br.mode.onStormLethal(br.match, br.match.player(0));
	EXPECT_TRUE(br.match.killed.empty());
}

TEST(BattleRoyaleStorm, TheFinalArenaSetting)
{
	for (std::int32_t setting = 2; setting <= 5; ++setting)
	{
		Royale br{ setting };
		EXPECT_EQ(static_cast<std::int32_t>(br.mode.storm()->finalArena()), setting - 2);
	}
	Royale random{ 1 };
	EXPECT_NE(random.mode.storm(), nullptr);
}
