#include <gtest/gtest.h>
#include "Detail/LogThrottle.h"

using Cast::Details::LogThrottle;

TEST(LogThrottle, OneLinePerIntervalAndSuppressedCount)
{
	LogThrottle throttle{ std::chrono::seconds{ 5 } };
	const auto start = LogThrottle::Clock::time_point{} + std::chrono::hours{ 1 };
	EXPECT_TRUE(throttle.allow(start));
	EXPECT_FALSE(throttle.allow(start + std::chrono::seconds{ 1 }));
	EXPECT_FALSE(throttle.allow(start + std::chrono::seconds{ 4 }));
	EXPECT_TRUE(throttle.allow(start + std::chrono::seconds{ 5 }));
	EXPECT_EQ(throttle.takeSuppressed(), 2u);
	EXPECT_EQ(throttle.takeSuppressed(), 0u);
}
