/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#include "HaveApi/Modes/BattleRoyaleStorm.h"
#include <algorithm>
#include <cmath>

namespace Main::HaveApi
{
	namespace CH = Common::HaveApi;

	namespace
	{
		[[nodiscard]] CH::StormRect baseRect(std::size_t box)
		{
			const BR::Box& b = BR::boxes[box];
			return { b.x1, b.x2, b.y1, b.y2 };
		}

		[[nodiscard]] CH::StormRect clampTo(const CH::StormRect& r, const CH::StormRect& toward)
		{
			return { std::clamp(toward.x1, r.x1, r.x2), std::clamp(toward.x2, r.x1, r.x2), std::clamp(toward.y1, r.y1, r.y2),
				std::clamp(toward.y2, r.y1, r.y2) };
		}

		[[nodiscard]] CH::StormRect around(float x, float y, float half)
		{
			return { x - half, x + half, y - half, y + half };
		}

		[[nodiscard]] CH::StormRect collapsed(const CH::StormRect& r)
		{
			const float x = (r.x1 + r.x2) / 2.0f;
			const float y = (r.y1 + r.y2) / 2.0f;
			return { x, x, y, y };
		}

		[[nodiscard]] CH::StormRect lerp(const CH::StormRect& a, const CH::StormRect& b, double f)
		{
			const auto mix = [f](float u, float v) { return static_cast<float>(u + (v - u) * f); };
			return { mix(a.x1, b.x1), mix(a.x2, b.x2), mix(a.y1, b.y1), mix(a.y2, b.y2) };
		}

		[[nodiscard]] const BR::Point& mouthOf(const BR::Corridor& corridor, BR::Arena arena)
		{
			return corridor.low == arena ? corridor.lowMouth : corridor.highMouth;
		}

		[[nodiscard]] bool touches(const BR::Corridor& corridor, BR::Arena arena)
		{
			return corridor.low == arena || corridor.high == arena;
		}

		[[nodiscard]] const BR::Corridor& corridorBetween(BR::Arena a, BR::Arena b)
		{
			const auto found = std::ranges::find_if(BR::corridors, [&](const BR::Corridor& c) { return touches(c, a) && touches(c, b); });
			return found != BR::corridors.end() ? *found : BR::corridors.front();
		}

		[[nodiscard]] bool isCorridorBox(std::size_t box)
		{
			return std::ranges::any_of(BR::corridors, [&](const BR::Corridor& c) { return c.box == box; });
		}

		[[nodiscard]] const BR::ArenaRegion& region(BR::Arena arena)
		{
			return BR::arenas[static_cast<std::size_t>(arena)];
		}
	}

	const char* StormPlan::arenaLabel(BR::Arena arena)
	{
		switch (arena)
		{
		case BR::Arena::Castle:
			return "CASTLE";
		case BR::Arena::Paper:
			return "PAPER LAND";
		case BR::Arena::Pvc:
			return "PVC FACTORY";
		case BR::Arena::Studio:
			return "THE STUDIO";
		}
		return "";
	}

	BR::Arena StormPlan::oppositeOf(BR::Arena arena)
	{
		switch (arena)
		{
		case BR::Arena::Castle:
			return BR::Arena::Studio;
		case BR::Arena::Studio:
			return BR::Arena::Castle;
		case BR::Arena::Paper:
			return BR::Arena::Pvc;
		case BR::Arena::Pvc:
			return BR::Arena::Paper;
		}
		return BR::Arena::Castle;
	}

	BR::Arena StormPlan::weightedArena(double roll)
	{
		if (roll < 0.35)
		{
			return BR::Arena::Paper;
		}
		if (roll < 0.65)
		{
			return BR::Arena::Pvc;
		}
		if (roll < 0.90)
		{
			return BR::Arena::Castle;
		}
		return BR::Arena::Studio;
	}

	StormPlan::StormPlan(BR::Arena finalArena, BR::Point finalPoint, double matchSeconds)
		: m_final{ finalArena }
		, m_opposite{ oppositeOf(finalArena) }
		, m_scale{ matchSeconds / baseSeconds }
		, m_moves(BR::boxes.size())
	{
		std::size_t side = 0;
		for (std::size_t i = 0; i < BR::arenaCount; ++i)
		{
			const auto arena = static_cast<BR::Arena>(i);
			if (arena != m_final && arena != m_opposite && side < m_sides.size())
			{
				m_sides[side++] = arena;
			}
		}
		const double k = m_scale;

		CH::StormRect exits{ 1e9f, -1e9f, 1e9f, -1e9f };
		for (const BR::Corridor& c : BR::corridors)
		{
			if (touches(c, m_opposite))
			{
				const BR::Point& m = mouthOf(c, m_opposite);
				exits = { (std::min)(exits.x1, m.x), (std::max)(exits.x2, m.x), (std::min)(exits.y1, m.y), (std::max)(exits.y2, m.y) };
			}
		}
		exits = { exits.x1 - mouthMargin, exits.x2 + mouthMargin, exits.y1 - mouthMargin, exits.y2 + mouthMargin };
		closeArena(m_opposite, 150.0 * k, exits);
		for (const BR::Arena s : m_sides)
		{
			sweepCorridor(corridorBetween(m_opposite, s), m_opposite, 210.0 * k, 270.0 * k);
		}

		const double stagger = m_final == BR::Arena::Studio ? studioStagger * k : 0.0;
		for (std::size_t i = 0; i < m_sides.size(); ++i)
		{
			const double start = 270.0 * k + (i == 1 ? stagger : 0.0);
			const BR::Corridor& toFinal = corridorBetween(m_sides[i], m_final);
			const BR::Point& m = mouthOf(toFinal, m_sides[i]);
			closeArena(m_sides[i], start, around(m.x, m.y, mouthMargin));
			sweepCorridor(toFinal, m_sides[i], start + 60.0 * k, 390.0 * k);
		}

		const BR::Box& e = region(m_final).exclusion;
		shrinkFinal(390.0 * k, 435.0 * k, { e.x1, e.x2, e.y1, e.y2 });
		shrinkFinal(480.0 * k, 525.0 * k, around(finalPoint.x, finalPoint.y, firstShrinkHalf));
		shrinkFinal(555.0 * k, 585.0 * k, around(finalPoint.x, finalPoint.y, secondShrinkHalf));
		shrinkFinal(585.0 * k, 600.0 * k, around(finalPoint.x, finalPoint.y, 0.0f));

		const std::string sides = std::string{ arenaLabel(m_sides[0]) } + " + " + arenaLabel(m_sides[1]);
		addPhase(0.0, 150.0 * k, "STORM IN", 0, 5, false);
		addPhase(150.0 * k, 210.0 * k, std::string{ arenaLabel(m_opposite) } + " CLOSING", 15, 5, true);
		addPhase(210.0 * k, 270.0 * k, "CORRIDORS CLOSING", 15, 5, true);
		addPhase(270.0 * k, 330.0 * k + stagger, sides + " CLOSING", 30, 5, true);
		addPhase(330.0 * k + stagger, 390.0 * k, "CORRIDORS CLOSING", 30, 5, true);
		addPhase(390.0 * k, 435.0 * k, "EXITS CLOSING", 50, 3, true);
		addPhase(435.0 * k, 480.0 * k, "STORM IN", 50, 3, false);
		addPhase(480.0 * k, 525.0 * k, "ZONE SHRINKING", 80, 3, true);
		addPhase(525.0 * k, 555.0 * k, "STORM IN", 80, 3, false);
		addPhase(555.0 * k, 585.0 * k, "ZONE SHRINKING", 120, 1, true);
		addPhase(585.0 * k, 600.0 * k, "FINAL CLOSING", 200, 1, true);

		std::ranges::sort(m_keys);
		const auto last = std::ranges::unique(m_keys, [](double a, double b) { return std::abs(a - b) < 1e-6; });
		m_keys.erase(last.begin(), last.end());
	}

	void StormPlan::addPhase(double start, double end, std::string label, std::int32_t damagePermille, std::int32_t deathSeconds, bool closing)
	{
		m_phases.push_back({ start, end, std::move(label), damagePermille, deathSeconds, closing });
		m_keys.push_back(start);
		m_keys.push_back(end);
	}

	template<typename Target>
	void StormPlan::move(std::size_t box, double start, double end, Target target)
	{
		const CH::StormRect from = rectAt(box, start);
		m_moves[box].push_back({ start, end, from, target(from) });
		m_keys.push_back(start);
		m_keys.push_back(end);
	}

	void StormPlan::closeArena(BR::Arena arena, double start, CH::StormRect toward)
	{
		const BR::ArenaRegion& r = region(arena);
		const double k = m_scale;
		m_closings.push_back({ arena, start });
		for (std::size_t box = r.firstBox; box < static_cast<std::size_t>(r.firstBox + r.boxCount); ++box)
		{
			move(box, start, start + 45.0 * k, [&](const CH::StormRect& from) { return clampTo(from, toward); });
			move(box, start + 45.0 * k, start + 60.0 * k, [](const CH::StormRect& from) { return collapsed(from); });
		}
	}

	void StormPlan::sweepCorridor(const BR::Corridor& corridor, BR::Arena from, double start, double end)
	{
		const bool fromLow = corridor.low == from;
		move(corridor.box, start, end, [&](CH::StormRect r) {
			if (corridor.alongX)
			{
				(fromLow ? r.x1 : r.x2) = fromLow ? r.x2 : r.x1;
			}
			else
			{
				(fromLow ? r.y1 : r.y2) = fromLow ? r.y2 : r.y1;
			}
			return r;
		});
	}

	void StormPlan::shrinkFinal(double start, double end, CH::StormRect toward)
	{
		const BR::ArenaRegion& r = region(m_final);
		for (std::size_t box = r.firstBox; box < static_cast<std::size_t>(r.firstBox + r.boxCount); ++box)
		{
			move(box, start, end, [&](const CH::StormRect& from) { return clampTo(from, toward); });
		}
	}

	CH::StormRect StormPlan::rectAt(std::size_t box, double t) const
	{
		CH::StormRect rect = baseRect(box);
		for (const Move& m : m_moves[box])
		{
			if (t >= m.end)
			{
				rect = m.to;
				continue;
			}
			if (t > m.start)
			{
				rect = lerp(m.from, m.to, (t - m.start) / (m.end - m.start));
			}
			break;
		}
		return rect;
	}

	bool StormPlan::contains(double t, float x, float y) const
	{
		for (std::size_t box = 0; box < m_moves.size(); ++box)
		{
			if (rectAt(box, t).contains(x, y))
			{
				return true;
			}
		}
		return false;
	}

	std::size_t StormPlan::segmentIndex(double t) const
	{
		return static_cast<std::size_t>(std::ranges::upper_bound(m_keys, t) - m_keys.begin());
	}

	const StormPlan::Phase& StormPlan::phaseAt(double t) const
	{
		const auto found = std::ranges::find_if(m_phases, [&](const Phase& p) { return t < p.end; });
		return found != m_phases.end() ? *found : m_phases.back();
	}

	CH::BattleZonePayload StormPlan::zoneAt(double t, std::int32_t maxHealth) const
	{
		CH::BattleZonePayload zone{};
		const Phase& phase = phaseAt(t);
		zone.active = 1;
		zone.phase = static_cast<std::int32_t>(&phase - m_phases.data());
		zone.phaseEndsInMs = static_cast<std::int32_t>(std::lround((std::max)(phase.end - t, 0.0) * 1000.0));
		zone.damagePerSecond = phase.damagePermille * maxHealth / 1000;
		const std::size_t length = (std::min)(phase.label.size(), zone.label.size() - 1);
		std::copy_n(phase.label.begin(), length, zone.label.begin());

		const auto next = std::ranges::upper_bound(m_keys, t);
		const bool moving = next != m_keys.end();
		const double until = moving ? *next : t;
		const float seconds = static_cast<float>(until - t);
		zone.boxCount = static_cast<std::int32_t>((std::min)(m_moves.size(), zone.boxes.size()));
		for (std::size_t i = 0; i < static_cast<std::size_t>(zone.boxCount); ++i)
		{
			const CH::StormRect now = rectAt(i, t);
			const CH::StormRect then = moving ? rectAt(i, until) : now;
			const CH::StormRect base = baseRect(i);
			const auto speed = [&](float a, float b) { return seconds > 0.0f ? (b - a) / seconds : 0.0f; };
			CH::BattleZoneBox& box = zone.boxes[i];
			box = { now.x1, speed(now.x1, then.x1), now.x2, speed(now.x2, then.x2), now.y1, speed(now.y1, then.y1), now.y2,
				speed(now.y2, then.y2), seconds, BR::boxes[i].zFloor, static_cast<std::uint8_t>(isCorridorBox(i) ? 1 : 0), 0, 0 };
			const auto moved = [](float origin, float a, float b) {
				return std::abs(a - origin) >= drawThreshold || std::abs(b - origin) >= drawThreshold;
			};
			box.drawSides = static_cast<std::uint8_t>((moved(base.x1, now.x1, then.x1) ? CH::SideX1 : 0) |
				(moved(base.x2, now.x2, then.x2) ? CH::SideX2 : 0) | (moved(base.y1, now.y1, then.y1) ? CH::SideY1 : 0) |
				(moved(base.y2, now.y2, then.y2) ? CH::SideY2 : 0));
		}
		return zone;
	}
}
