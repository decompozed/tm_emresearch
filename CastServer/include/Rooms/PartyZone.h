/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#ifndef CAST_ROOMS_PARTY_ZONE_H
#define CAST_ROOMS_PARTY_ZONE_H

#include <chrono>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>
#include "HaveApi/Zone.h"
#include "Rooms/PartyLava.h"

namespace Cast::Domain
{
	class PartyZone
	{
	public:
		using Clock = PartyLava::Clock;

		static constexpr std::chrono::seconds positionMaxAge{ 3 };
		static constexpr std::chrono::seconds spawnGrace{ 5 };

		explicit PartyZone(std::uint8_t index = 0) : m_index{ index } {}

		void set(const Common::HaveApi::ZoneCommand& command);
		void addMembers(const Common::HaveApi::ZoneCommand& command);
		void clear();
		[[nodiscard]] bool active() const noexcept { return m_active; }

		[[nodiscard]] Common::HaveApi::ZoneAnswer occupants(const PartyLava& positions, Clock::time_point now = Clock::now()) const;
		bool updateOccupant(std::uint32_t sessionId, float x, float y, float z, Common::HaveApi::ZoneAnswer& changed,
			Clock::time_point now = Clock::now());
		Common::HaveApi::ZoneAnswer resetOccupants(const PartyLava& positions, Clock::time_point now = Clock::now());

		void onRespawn(std::uint32_t sessionId, float x, float y, Clock::time_point now = Clock::now());
		[[nodiscard]] bool inSpawnGrace(std::uint32_t sessionId, Clock::time_point now = Clock::now()) const;

	private:
		struct Respawn { float x{}, y{}; Clock::time_point at{}; };

		void addTeams(const Common::HaveApi::ZoneCommand& command);
		[[nodiscard]] Common::HaveApi::ZoneAnswer answer(std::span<const std::uint32_t> sessions) const;
		[[nodiscard]] float distanceToZone(float x, float y) const;
		[[nodiscard]] bool contains(float x, float y, float z) const;

		std::uint8_t m_index{};
		bool m_active{};
		Common::HaveApi::ZoneShape m_shape{ Common::HaveApi::ZoneShape::Circle };
		float m_x{}, m_y{}, m_radius{}, m_keepOut{}, m_zMin{}, m_zMax{};
		std::unordered_map<std::uint32_t, std::int32_t> m_teams;
		std::unordered_map<std::uint32_t, Respawn> m_respawns;
		std::vector<std::uint32_t> m_inside;
	};
}

#endif
