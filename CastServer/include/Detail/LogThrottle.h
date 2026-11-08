#ifndef CAST_DETAIL_LOG_THROTTLE_H
#define CAST_DETAIL_LOG_THROTTLE_H

#include <chrono>
#include <cstdint>
#include <optional>

namespace Cast::Details
{
	class LogThrottle
	{
	public:
		using Clock = std::chrono::steady_clock;

		explicit constexpr LogThrottle(Clock::duration interval) noexcept : m_interval{ interval } {}

		[[nodiscard]] bool allow(Clock::time_point now) noexcept
		{
			if (m_last && now - *m_last < m_interval)
			{
				++m_suppressed;
				return false;
			}
			m_last = now;
			return true;
		}

		[[nodiscard]] std::uint32_t takeSuppressed() noexcept
		{
			const auto suppressed = m_suppressed;
			m_suppressed = 0;
			return suppressed;
		}

	private:
		Clock::duration m_interval;
		std::optional<Clock::time_point> m_last;
		std::uint32_t m_suppressed{};
	};
}

#endif
