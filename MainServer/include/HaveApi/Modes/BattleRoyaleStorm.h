/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#ifndef MAIN_HAVE_API_MODES_BATTLE_ROYALE_STORM_H
#define MAIN_HAVE_API_MODES_BATTLE_ROYALE_STORM_H

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>
#include "HaveApi/BattleRoyaleMap.h"
#include "HaveApi/Messages.h"
#include "HaveApi/Storm.h"

namespace Main::HaveApi
{
	namespace BR = Common::HaveApi::BattleRoyaleMap;

	class StormPlan
	{
	public:
		static constexpr double baseSeconds = 600.0;
		static constexpr float mouthMargin = 400.0f;
		static constexpr float firstShrinkHalf = 1250.0f;
		static constexpr float secondShrinkHalf = 400.0f;
		static constexpr double studioStagger = 25.0;
		static constexpr float drawThreshold = 1.0f;

		struct Phase
		{
			double start{};
			double end{};
			std::string label;
			std::int32_t damagePermille{}; // of the max health, per second
			std::int32_t deathSeconds{};
			bool closing{};
		};

		struct Closing
		{
			BR::Arena arena{};
			double start{};
		};

		StormPlan(BR::Arena finalArena, BR::Point finalPoint, double matchSeconds);

		[[nodiscard]] BR::Arena finalArena() const noexcept { return m_final; }
		[[nodiscard]] BR::Arena opposite() const noexcept { return m_opposite; }
		[[nodiscard]] const std::array<BR::Arena, 2>& sides() const noexcept { return m_sides; }
		[[nodiscard]] double scale() const noexcept { return m_scale; }
		[[nodiscard]] double exitsClosing() const noexcept { return 390.0 * m_scale; }

		[[nodiscard]] std::size_t boxCount() const noexcept { return m_moves.size(); }
		[[nodiscard]] Common::HaveApi::StormRect rectAt(std::size_t box, double t) const;
		[[nodiscard]] bool contains(double t, float x, float y) const;
		[[nodiscard]] std::size_t segmentIndex(double t) const;
		[[nodiscard]] const Phase& phaseAt(double t) const;
		[[nodiscard]] std::span<const Phase> phases() const noexcept { return m_phases; }
		[[nodiscard]] std::span<const Closing> closings() const noexcept { return m_closings; }
		[[nodiscard]] Common::HaveApi::BattleZonePayload zoneAt(double t, std::int32_t maxHealth) const;

		[[nodiscard]] static const char* arenaLabel(BR::Arena arena);
		[[nodiscard]] static BR::Arena oppositeOf(BR::Arena arena);
		[[nodiscard]] static BR::Arena weightedArena(double roll);

	private:
		struct Move
		{
			double start{};
			double end{};
			Common::HaveApi::StormRect from{};
			Common::HaveApi::StormRect to{};
		};

		template<typename Target>
		void move(std::size_t box, double start, double end, Target target);
		void closeArena(BR::Arena arena, double start, Common::HaveApi::StormRect toward);
		void sweepCorridor(const BR::Corridor& corridor, BR::Arena from, double start, double end);
		void shrinkFinal(double start, double end, Common::HaveApi::StormRect toward);
		void addPhase(double start, double end, std::string label, std::int32_t damagePermille, std::int32_t deathSeconds, bool closing);

		BR::Arena m_final;
		BR::Arena m_opposite;
		std::array<BR::Arena, 2> m_sides{};
		double m_scale;
		std::vector<std::vector<Move>> m_moves;
		std::vector<double> m_keys;
		std::vector<Phase> m_phases;
		std::vector<Closing> m_closings;
	};
}

#endif
