#ifndef CAST_COMBAT_WORLD_H
#define CAST_COMBAT_WORLD_H

#include <chrono>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include "Structures/Combat/CombatPackets.h"

namespace Cast::Combat
{
	struct PlayerState
	{
		bool inMatch{};
		bool observer{};
		bool dead{};
		bool staleDeath{};
	};

	// The host handles only live shooters (0xAC7D20).
	inline constexpr std::chrono::seconds staleDeathDelay{ 3 };

	enum class Cause : std::uint8_t
	{
		Weapon,
		Explosive,
	};

	enum class Route : std::uint8_t
	{
		Drop,
		Match,
		MatchExceptSender,
		Host,
		Recipient,
		Handled,
	};

	struct Outgoing
	{
		Route route{ Route::Drop };
		std::uint32_t sender{};
		std::uint32_t recipient{};
		std::uint16_t order{};
		std::uint8_t mission{};
		std::uint8_t extra{};
		std::uint8_t option{};
		std::span<const std::uint8_t> payload;
	};

	class World
	{
	public:
		virtual ~World() = default;

		[[nodiscard]] virtual std::uint16_t serverId() const noexcept = 0;
		[[nodiscard]] virtual std::uint32_t mode() const noexcept = 0;
		[[nodiscard]] virtual bool modeKnown() const noexcept = 0;
		[[nodiscard]] virtual bool isHost(std::uint32_t session) const noexcept = 0;
		[[nodiscard]] virtual bool isAuthorizedMember(std::uint32_t session) const noexcept = 0;
		[[nodiscard]] virtual std::optional<PlayerState> player(std::uint32_t session) const = 0;
		[[nodiscard]] virtual bool disarmed(std::uint32_t session) const = 0;
		[[nodiscard]] virtual bool sparesPlayers() const = 0;
		[[nodiscard]] virtual std::optional<std::uint32_t> explosiveOwner(std::uint32_t ammoId) const = 0;
		[[nodiscard]] virtual std::string_view nickname(std::uint32_t session) const = 0;

		virtual void rememberExplosive(std::uint32_t ammoId, std::uint32_t ownerSession) = 0;
		virtual void hit(std::uint32_t target, std::optional<std::uint32_t> attacker, Cause cause, bool spared) = 0;
		virtual void death(std::uint32_t victim, std::optional<std::uint32_t> killer, Cause cause) = 0;
		virtual void respawn(std::uint32_t host, std::uint32_t player, const Structures::Half3& position) = 0;
		virtual void revive(std::uint32_t player) = 0;
		virtual bool send(const Outgoing& packet) = 0;
		virtual void endMatch() = 0;
		virtual void startMatch() = 0;
		virtual void leaveMatch(std::uint32_t session) = 0;
		virtual void finishLoading(std::uint32_t session) = 0;
	};

	struct Packet
	{
		std::uint32_t sender{};
		std::uint16_t order{};
		std::uint8_t mission{};
		std::uint8_t extra{};
		std::uint8_t option{};
		std::span<std::uint8_t> payload;
		std::uint32_t target{};
	};

	struct Verdict
	{
		Route route{ Route::Drop };
		std::string_view reason;
		std::uint8_t option{};
		std::size_t size{};

		[[nodiscard]] static Verdict drop(std::string_view why) noexcept { return { Route::Drop, why, 0, 0 }; }
		[[nodiscard]] static Verdict to(Route route, const Packet& packet) noexcept { return { route, {}, packet.option, packet.payload.size() }; }
	};
}

#endif
