/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#include "Rooms/RoomMembership.h"
#include <algorithm>

namespace Cast::Domain
{
	bool RoomMembership::apply(const Common::HaveApi::RoomSyncMessage& message)
	{
		if (!isNewer(message.header.sequence))
		{
			return false;
		}
		m_lastSequence = message.header.sequence;
		m_count = static_cast<std::uint8_t>(std::min<std::size_t>(message.header.count, m_members.size()));
		std::copy_n(message.entries.begin(), m_count, m_members.begin());
		m_host = message.header.hostSession;
		return true;
	}

	const Common::HaveApi::RoomSyncEntry* RoomMembership::member(std::uint32_t session) const noexcept
	{
		for (std::size_t i = 0; i < m_count; ++i)
		{
			if (m_members[i].session == session)
			{
				return &m_members[i];
			}
		}
		return nullptr;
	}

	AttachVerdict RoomMembership::attachVerdict(std::uint32_t session, std::uint16_t order, std::uint32_t headerSession, bool attachedHere) const noexcept
	{
		if (!isAuthorizedMember(session))
		{
			return AttachVerdict::NotMember;
		}
		if (attachedHere)
		{
			return AttachVerdict::AlreadyAttached;
		}
		if (m_host == 0)
		{
			return AttachVerdict::HostUnknown;
		}
		if (order == createOrder)
		{
			return session == m_host ? AttachVerdict::Attach : AttachVerdict::NotHost;
		}
		return headerSession == m_host ? AttachVerdict::Attach : AttachVerdict::WrongHost;
	}
}
