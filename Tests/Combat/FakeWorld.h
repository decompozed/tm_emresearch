#ifndef TESTS_COMBAT_FAKE_WORLD_H
#define TESTS_COMBAT_FAKE_WORLD_H

#include <cstdint>
#include <cstring>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>
#include "Combat/Rules.h"
#include "Combat/World.h"
#include "Enums/RoomEnums.h"
#include "Structures/Combat/CombatPackets.h"

namespace CombatTests
{
	using namespace Cast::Combat;
	using namespace Cast::Structures;

	inline constexpr std::uint16_t server = 7;
	inline constexpr std::uint32_t hostSession = 1;

	struct Event
	{
		std::uint32_t target{};
		std::optional<std::uint32_t> attacker;
		Cause cause{};
		bool spared{};
		bool death{};
	};

	struct Respawn
	{
		std::uint32_t host{};
		std::uint32_t player{};
		Half3 position{};
	};

	struct Sent
	{
		Route route{};
		std::uint32_t sender{};
		std::uint32_t recipient{};
		std::uint16_t order{};
		std::uint8_t mission{};
		std::uint8_t extra{};
		std::uint8_t option{};
		std::vector<std::uint8_t> payload;

		template<class T>
		T at(std::size_t offset) const
		{
			T value{};
			std::memcpy(&value, payload.data() + offset, sizeof(T));
			return value;
		}
	};

	class FakeWorld final : public World
	{
	public:
		std::uint32_t roomMode{ Common::Enums::TeamDeathMatch };
		bool knownMode{ true };
		std::optional<std::uint32_t> host{ hostSession };
		std::map<std::uint32_t, PlayerState> players;
		std::set<std::uint32_t> disarmedPlayers;
		bool spares{};
		std::map<std::uint32_t, std::uint32_t> explosives;
		std::vector<Event> events;
		std::vector<Respawn> respawns;
		std::vector<std::uint32_t> revived;
		std::map<std::uint32_t, std::string> nicknames;
		std::set<std::uint32_t> detached;
		std::vector<Sent> sent;
		std::vector<std::uint32_t> leftMatch;
		std::vector<std::uint32_t> loaded;
		bool matchEnded{};
		bool matchStarted{};

		FakeWorld()
		{
			for (std::uint32_t session = 1; session <= 4; ++session)
			{
				players[session] = PlayerState{ true, false, false };
			}
		}

		std::uint16_t serverId() const noexcept override { return server; }
		std::uint32_t mode() const noexcept override { return roomMode; }
		bool modeKnown() const noexcept override { return knownMode; }
		bool isHost(std::uint32_t session) const noexcept override { return host && *host == session; }
		bool isAuthorizedMember(std::uint32_t session) const noexcept override { return players.contains(session); }
		std::optional<PlayerState> player(std::uint32_t session) const override
		{
			const auto it = players.find(session);
			return it == players.end() ? std::nullopt : std::optional{ it->second };
		}
		bool disarmed(std::uint32_t session) const override { return disarmedPlayers.contains(session); }
		bool sparesPlayers() const override { return spares; }
		std::optional<std::uint32_t> explosiveOwner(std::uint32_t ammoId) const override
		{
			const auto it = explosives.find(ammoId);
			return it == explosives.end() ? std::nullopt : std::optional{ it->second };
		}
		std::string_view nickname(std::uint32_t session) const override
		{
			const auto it = nicknames.find(session);
			return it == nicknames.end() ? std::string_view{} : std::string_view{ it->second };
		}
		void rememberExplosive(std::uint32_t ammoId, std::uint32_t ownerSession) override { explosives[ammoId] = ownerSession; }
		void hit(std::uint32_t target, std::optional<std::uint32_t> attacker, Cause cause, bool spared) override
		{
			events.push_back({ target, attacker, cause, spared, false });
		}
		void death(std::uint32_t victim, std::optional<std::uint32_t> killer, Cause cause) override
		{
			players[victim].dead = true;
			players[victim].staleDeath = false;
			events.push_back({ victim, killer, cause, false, true });
		}
		void respawn(std::uint32_t hostId, std::uint32_t player, const Half3& position) override
		{
			players[player].dead = false;
			players[player].staleDeath = false;
			players[player].inMatch = true;
			respawns.push_back({ hostId, player, position });
		}
		void revive(std::uint32_t player) override
		{
			players[player].dead = false;
			players[player].staleDeath = false;
			revived.push_back(player);
		}
		bool send(const Outgoing& packet) override
		{
			if (packet.route == Route::Recipient && detached.contains(packet.recipient))
			{
				return false;
			}
			sent.push_back({ packet.route, packet.sender, packet.recipient, packet.order, packet.mission, packet.extra, packet.option,
				{ packet.payload.begin(), packet.payload.end() } });
			return true;
		}
		void endMatch() override { matchEnded = true; }
		void startMatch() override { matchStarted = true; }
		void leaveMatch(std::uint32_t session) override { leftMatch.push_back(session); }
		void finishLoading(std::uint32_t session) override { loaded.push_back(session); }
	};

	inline UniqueId uid(std::uint32_t session, std::uint32_t serverId = server)
	{
		UniqueId id{};
		id.session = session & 0xFFFF;
		id.server = serverId & 0x7FFF;
		return id;
	}

	inline std::uint32_t raw(const UniqueId& id)
	{
		std::uint32_t value{};
		std::memcpy(&value, &id, sizeof(value));
		return value;
	}

	inline HpField hpField(std::uint32_t hp, std::uint32_t part = 0)
	{
		HpField field{};
		field.hp = hp & 0xFFFFF;
		field.hitPart = part & 0x1F;
		return field;
	}

	inline DamageEntry entry(std::uint32_t session, std::uint32_t hp)
	{
		DamageEntry value{};
		value.target = uid(session);
		value.hp = hpField(hp);
		return value;
	}

	inline Half3 half3(std::uint16_t x = 0x3C00, std::uint16_t y = 0x4000, std::uint16_t z = 0x4200)
	{
		return Half3{ x, y, z };
	}

	inline ShotTrace trace()
	{
		ShotTrace value{};
		value.start = half3();
		value.direction = half3(0x3C00, 0, 0);
		return value;
	}

	class Frame
	{
	public:
		std::vector<std::uint8_t> data;
		std::uint16_t order{};
		std::uint32_t sender{ hostSession };
		std::uint8_t mission{};
		std::uint8_t extra{};
		std::uint8_t option{};
		std::uint32_t target{};

		Frame(std::uint16_t orderValue, std::uint8_t extraValue, std::uint8_t optionValue)
			: order{ orderValue }, extra{ extraValue }, option{ optionValue } {}

		template<class T>
		Frame& add(const T& part)
		{
			const auto* bytes = reinterpret_cast<const std::uint8_t*>(&part);
			data.insert(data.end(), bytes, bytes + sizeof(T));
			return *this;
		}

		Frame& from(std::uint32_t session)
		{
			sender = session;
			return *this;
		}

		Frame& to(std::uint32_t session)
		{
			target = session;
			return *this;
		}

		Frame& withMission(std::uint8_t value)
		{
			mission = value;
			return *this;
		}

		Packet packet() { return Packet{ sender, order, mission, extra, option, data, target }; }

		template<class T>
		T at(std::size_t offset) const
		{
			T value{};
			std::memcpy(&value, data.data() + offset, sizeof(T));
			return value;
		}
	};
}

#endif
