/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#ifndef CAST_ROOMS_PARTY_LAVA_H
#define CAST_ROOMS_PARTY_LAVA_H

#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include "HaveApi/Lava.h"
#include "HaveApi/LavaMaps.h"
#include "HaveApi/PartySpawns.h"

namespace Cast::Domain
{
	// The state returned here is what the clients draw (0x23 / 0x24): the kill test uses the same numbers.
	class PartyLava
	{
	public:
		using Clock = std::chrono::steady_clock;
		struct Position { float x{}, y{}, z{}; Clock::time_point at{}; };

		static constexpr float startMargin = 400.0f;
		static constexpr float wallMargin = 2000.0f;
		static constexpr float mapWallMargin = 200.0f;
		static constexpr float maxDepthBelowSpawns = 2000.0f; // a map's lowest collision point further down is far scenery
		static constexpr std::chrono::seconds positionMaxAge{ 10 };

		// What a start was placed from (the mode logs)
		struct StartReport
		{
			std::size_t freshPositions{};          // younger than positionMaxAge: the lava is placed around them
			std::size_t knownPositions{};          // received since the last stop (or fresh): the floor stays below them
			std::optional<float> lowestFreshZ;
			std::optional<float> lowestKnownZ;
			std::optional<float> lowestSpawnZ;     // HaveApi/PartySpawns.h
			std::optional<float> mapMinZ;          // HaveApi/LavaMaps.h
		};

		void setPosition(std::uint32_t sessionId, float x, float y, float z, Clock::time_point now = Clock::now());
		[[nodiscard]] std::optional<Position> position(std::uint32_t sessionId) const;
		[[nodiscard]] const std::unordered_map<std::uint32_t, Position>& positions() const noexcept { return m_positions; }
		void forgetPositions() { m_positions.clear(); }

		// Floor: startMargin below the lowest fresh player position (none: below the lower of the map's lowest spawn and
		// its lowest collision point), and never less than startMargin below any known position or the lowest spawn.
		// Walls: the map's bounds (unknown map: around the positions / spawns), always outside every known position.
		Common::HaveApi::LavaState start(const Common::HaveApi::LavaCommand& command, std::uint32_t map, Clock::time_point now = Clock::now());
		[[nodiscard]] const StartReport& lastStart() const noexcept { return m_lastStart; }
		Common::HaveApi::LavaState setSpeed(const Common::HaveApi::LavaCommand& command, Clock::time_point now = Clock::now());
		Common::HaveApi::LavaState stop(Clock::time_point now = Clock::now());
		[[nodiscard]] Common::HaveApi::LavaState state(Clock::time_point now = Clock::now()) const;
		[[nodiscard]] bool active() const noexcept { return m_active; }
		bool reaches(std::uint32_t sessionId, float x, float y, float z, const char*& reason, Clock::time_point now = Clock::now());

		void setProtectPlayers(bool on) noexcept { m_protectPlayers = on; }
		[[nodiscard]] bool protectsPlayers() const noexcept { return m_protectPlayers; }
		void setDisarmed(std::uint32_t sessionId, bool on);
		void armEveryone() { m_disarmed.clear(); }
		[[nodiscard]] bool disarmed(std::uint32_t sessionId) const { return m_disarmed.contains(sessionId); }

	private:
		struct Box
		{
			float minX{}, minY{}, minZ{}, maxX{}, maxY{};
			bool any{};
			void add(float x, float y, float z);
		};

		void place(const Box& box, float floorZ, float margin);
		void keepOutside(const Box& box, float margin);
		void beginMoving(const Common::HaveApi::LavaCommand& command, Clock::time_point now);
		void applySpeeds(float factor);

		Common::HaveApi::LavaState m_base{};
		Clock::time_point m_since{};
		Clock::time_point m_stoppedAt{};
		StartReport m_lastStart{};
		bool m_active{};
		bool m_protectPlayers{};
		float m_baseFloorSpeed{};
		std::array<float, 4> m_baseWallSpeeds{};
		std::unordered_map<std::uint32_t, Position> m_positions;
		std::unordered_set<std::uint32_t> m_killed;
		std::unordered_set<std::uint32_t> m_disarmed;
	};
}

#endif
