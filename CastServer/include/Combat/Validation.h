#ifndef CAST_COMBAT_VALIDATION_H
#define CAST_COMBAT_VALIDATION_H

#include <bit>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include "Combat/World.h"
#include "Structures/Combat/CombatPackets.h"

namespace Cast::Combat
{
	template<class T>
		requires std::is_trivially_copyable_v<T>
	[[nodiscard]] T load(std::span<const std::uint8_t> payload, std::size_t offset = 0) noexcept
	{
		T value{};
		std::memcpy(&value, payload.data() + offset, sizeof(T));
		return value;
	}

	template<class T>
		requires std::is_trivially_copyable_v<T>
	void store(std::span<std::uint8_t> payload, std::size_t offset, const T& value) noexcept
	{
		std::memcpy(payload.data() + offset, &value, sizeof(T));
	}

	[[nodiscard]] constexpr bool isFinite(std::uint16_t half) noexcept { return ((half >> 10) & 0x1F) != 0x1F; }
	[[nodiscard]] constexpr bool isFinite(const Structures::Half3& v) noexcept { return isFinite(v.x) && isFinite(v.y) && isFinite(v.z); }
	[[nodiscard]] constexpr bool isFinite(const Structures::ShotTrace& t) noexcept { return isFinite(t.start) && isFinite(t.direction); }

	[[nodiscard]] constexpr bool hasEmptyHeader(const Packet& packet) noexcept { return packet.mission == 0 && packet.extra == 0 && packet.option == 0; }

	[[nodiscard]] inline std::uint32_t rawId(const Structures::UniqueId& id) noexcept { return std::bit_cast<std::uint32_t>(id) & 0x7FFFFFFF; }

	enum class UnitKind : std::uint8_t
	{
		Invalid,
		Player,
		Npc,
	};

	struct Unit
	{
		UnitKind kind{ UnitKind::Invalid };
		std::uint32_t session{};
		PlayerState state{};

		[[nodiscard]] bool isPlayer() const noexcept { return kind == UnitKind::Player; }
		[[nodiscard]] bool inMatch() const noexcept { return kind == UnitKind::Npc || (isPlayer() && state.inMatch && !state.observer); }
		[[nodiscard]] bool deadPlayer() const noexcept { return isPlayer() && state.dead; }
		[[nodiscard]] bool staleDeadPlayer() const noexcept { return deadPlayer() && state.staleDeath; }
		[[nodiscard]] std::optional<std::uint32_t> playerSession() const noexcept { return isPlayer() ? std::optional{ session } : std::nullopt; }
	};

	[[nodiscard]] bool allowsNpcs(std::uint32_t mode) noexcept;
	[[nodiscard]] Unit resolve(const World& world, const Structures::UniqueId& id);

	[[nodiscard]] std::string_view checkHostOutcome(const World& world, const Packet& packet);
	[[nodiscard]] std::string_view checkMatchRequester(const World& world, const Packet& packet);
	[[nodiscard]] std::string_view checkLiveRequester(const World& world, const Packet& packet);
	[[nodiscard]] std::string_view checkRequester(const World& world, const Packet& packet);
	[[nodiscard]] std::string_view checkSnapshot(const World& world, const Packet& packet);
	[[nodiscard]] bool isRoomPlayer(const World& world, const Structures::UniqueId& id);
	void reviveIfStale(World& world, Unit& unit);

	bool spareLethalHit(const World& world, std::optional<std::uint32_t> attacker, std::uint32_t target, Structures::HpField& hp);
	void reportDamage(World& world, std::uint32_t target, const Structures::HpField& hp, std::optional<std::uint32_t> attacker, Cause cause, bool spared);

	struct EntryRules
	{
		std::optional<std::uint32_t> attacker;
		std::optional<std::uint32_t> excludedTarget;
		Cause cause{};
	};

	[[nodiscard]] Verdict applyDamageEntries(World& world, const Packet& packet, std::size_t headerSize, const EntryRules& rules);
}

#endif
