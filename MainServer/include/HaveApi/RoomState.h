/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#ifndef MAIN_HAVE_API_ROOM_STATE_H
#define MAIN_HAVE_API_ROOM_STATE_H

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <utility>
#include "HaveApi/Catalog.h"
#include "HaveApi/Messages.h"
#include "Utils/SetupParser.h"

namespace Main::HaveApi
{
	class PartyMatch;

	// have_api state of one room: the room options (message 0x06) and the chosen party game (message 0x0A).
	// Values 0 mean "not set": the game's own behaviour.
	struct KillCount
	{
		std::uint32_t kills{};
		std::uint32_t deaths{};
	};

	class RoomState
	{
	public:
		// The option check box is offered according to [HaveApi] RoomOptionCheckBox (default: offered, unchecked).
		RoomState()
		{
			m_options.optionAvailable = Common::Utils::SetupParser::getInstance().getHaveApiSetup().roomOptionCheckBox ? 1 : 0;
		}

		[[nodiscard]] const Common::HaveApi::RoomOptionsPayload& options() const noexcept { return m_options; }
		[[nodiscard]] const Common::HaveApi::PartySettingsPayload& partySettings() const noexcept { return m_party; }

		[[nodiscard]] bool hasPartyGame() const noexcept
		{
			return m_party.modeId != static_cast<std::int32_t>(Common::HaveApi::PartyModeId::None);
		}

		[[nodiscard]] std::int32_t option(Common::HaveApi::RoomOptionKind kind) const noexcept
		{
			switch (kind)
			{
			case Common::HaveApi::RoomOptionKind::MaxHealth:
				return m_options.maxHealth;
			case Common::HaveApi::RoomOptionKind::WeaponBalance:
				return m_options.weaponBalance;
			case Common::HaveApi::RoomOptionKind::OptionCheckBox:
				return m_options.optionEnabled;
			}
			return -1;
		}

		void setOption(Common::HaveApi::RoomOptionKind kind, std::int32_t value) noexcept
		{
			switch (kind)
			{
			case Common::HaveApi::RoomOptionKind::MaxHealth:
				m_options.maxHealth = value;
				break;
			case Common::HaveApi::RoomOptionKind::WeaponBalance:
				m_options.weaponBalance = value;
				break;
			case Common::HaveApi::RoomOptionKind::OptionCheckBox:
				m_options.optionEnabled = value;
				break;
			}
		}

		void setPartySettings(const Common::HaveApi::PartySettingsPayload& settings) noexcept { m_party = settings; }
		void clearPartyGame() noexcept { m_party = {}; }

		[[nodiscard]] PartyMatch* match() const noexcept { return m_match.get(); }
		void setMatch(std::shared_ptr<PartyMatch> match) noexcept { m_match = std::move(match); }
		void stopMatch();

		// Scrimmage: the host's match end carries only damage, so the server counts the deaths the Cast reports
		// (C2M_partyCombatEvent) for the players' scoreboard. Cleared when a match starts.
		void recordDeath(std::uint32_t victimAccountId, std::uint32_t attackerAccountId)
		{
			++m_killCounts[victimAccountId].deaths;
			if (attackerAccountId != 0 && attackerAccountId != victimAccountId)
			{
				++m_killCounts[attackerAccountId].kills;
			}
		}
		[[nodiscard]] KillCount killCountOf(std::uint32_t accountId) const
		{
			const auto found = m_killCounts.find(accountId);
			return found != m_killCounts.end() ? found->second : KillCount{};
		}
		void clearKillCounts() noexcept { m_killCounts.clear(); }

		struct PartyResult
		{
			std::int32_t placement{};
			std::uint64_t playedUntil{};
			std::int32_t entrants{};
		};
		void setPartyResults(std::unordered_map<std::uint32_t, PartyResult> results) { m_partyResults = std::move(results); }
		[[nodiscard]] const PartyResult* partyResultOf(std::uint32_t sessionId) const
		{
			const auto found = m_partyResults.find(sessionId);
			return found != m_partyResults.end() ? &found->second : nullptr;
		}
		void clearPartyResults() noexcept { m_partyResults.clear(); }

	private:
		Common::HaveApi::RoomOptionsPayload m_options{};
		Common::HaveApi::PartySettingsPayload m_party{};
		std::shared_ptr<PartyMatch> m_match;
		std::unordered_map<std::uint32_t, KillCount> m_killCounts; // account id -> this match's kills / deaths
		std::unordered_map<std::uint32_t, PartyResult> m_partyResults;
	};
}

#endif
