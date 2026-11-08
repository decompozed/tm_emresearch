#ifndef CAST_ROOMS_ROOM_MEMBERSHIP_H
#define CAST_ROOMS_ROOM_MEMBERSHIP_H

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include "HaveApi/RoomSync.h"

namespace Cast::Domain
{
	enum class AttachVerdict : std::uint8_t
	{
		Attach,
		AlreadyAttached, // a repeated 277 / 140: nothing changes (never a toggle)
		NotMember,
		HostUnknown,
		NotHost,
		WrongHost,
	};

	class RoomMembership
	{
	public:
		static constexpr std::uint16_t createOrder = 277;
		static constexpr std::uint16_t joinOrder = 140;

		bool apply(const Common::HaveApi::RoomSyncMessage& message);
		[[nodiscard]] bool isNewer(std::uint32_t sequence) const noexcept { return m_lastSequence == 0 || sequence > m_lastSequence; }
		void forgetSequence() noexcept { m_lastSequence = 0; }
		void invalidateHost() noexcept { m_host = 0; }

		[[nodiscard]] bool isHost(std::uint32_t session) const noexcept { return m_host != 0 && session == m_host; }
		[[nodiscard]] std::optional<std::uint32_t> host() const noexcept { return m_host != 0 ? std::optional{ m_host } : std::nullopt; }
		[[nodiscard]] bool isAuthorizedMember(std::uint32_t session) const noexcept { return member(session) != nullptr; }
		[[nodiscard]] const Common::HaveApi::RoomSyncEntry* member(std::uint32_t session) const noexcept;
		[[nodiscard]] std::span<const Common::HaveApi::RoomSyncEntry> members() const noexcept { return { m_members.data(), m_count }; }
		[[nodiscard]] std::uint32_t lastSequence() const noexcept { return m_lastSequence; }

		[[nodiscard]] AttachVerdict attachVerdict(std::uint32_t session, std::uint16_t order, std::uint32_t headerSession, bool attachedHere) const noexcept;

	private:
		std::array<Common::HaveApi::RoomSyncEntry, Common::HaveApi::maxRoomSyncEntries> m_members{};
		std::uint8_t m_count{};
		std::uint32_t m_host{};
		std::uint32_t m_lastSequence{};
	};

	[[nodiscard]] constexpr const char* toString(AttachVerdict verdict) noexcept
	{
		switch (verdict)
		{
		case AttachVerdict::Attach: return "attach";
		case AttachVerdict::AlreadyAttached: return "already attached";
		case AttachVerdict::NotMember: return "not a member of any room";
		case AttachVerdict::HostUnknown: return "room host unknown";
		case AttachVerdict::NotHost: return "not the room host";
		case AttachVerdict::WrongHost: return "TCP header is not the room host";
		}
		return "?";
	}
}

#endif
