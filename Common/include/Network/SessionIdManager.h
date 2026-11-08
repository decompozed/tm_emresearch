
#ifndef SESSIONS_ID_MANAGER_H
#define SESSIONS_ID_MANAGER_H

#include <stack>
#include <unordered_set>
#include <optional>
#include <iostream>

class SessionIdManager
{
private:
	std::stack<std::size_t> m_availableIDs;
	std::unordered_set<std::size_t> m_usedIDs;
	std::size_t m_firstSessionID;
	std::size_t m_maxSessionIDs;

public:
	SessionIdManager(std::size_t firstID, std::size_t count) : m_firstSessionID(firstID), m_maxSessionIDs(count)
	{
		for (std::size_t i = firstID; i < firstID + count; ++i)
		{
			m_availableIDs.push(i);
		}
	}

	std::size_t first() const noexcept { return m_firstSessionID; }
	std::size_t last() const noexcept { return m_firstSessionID + m_maxSessionIDs - 1; }

	std::optional<std::size_t> getNewSessionID()
	{
		if (m_availableIDs.empty())
		{
			return std::nullopt;
		}

		const std::size_t newID = m_availableIDs.top();
		m_availableIDs.pop();
		m_usedIDs.insert(newID);

		return newID;
	}

	void releaseSessionID(std::size_t id)
	{
		auto it = m_usedIDs.find(id);
		if (it != m_usedIDs.end())
		{
			m_usedIDs.erase(it);
			m_availableIDs.push(id);
		}
	}
};

#endif