#ifndef SELL_PRICE_H
#define SELL_PRICE_H

#include <cstdint>

namespace Main
{
	namespace Details
	{
		// MicroVolts.exe 0x6CF180: the value the client shows and sends in order 100 (x87 truncation, an overflow gives 0)
		inline std::int32_t clientSellPrice(std::uint32_t sellPoint, std::uint32_t limitedTime, std::int32_t expirationDate, std::int64_t now)
		{
			if (expirationDate < 4) return static_cast<std::int32_t>(sellPoint);
			const std::uint32_t totalHours = limitedTime / 3600u;
			if (totalHours == 0) return 0;
			const double price = static_cast<double>((expirationDate - now) / 3600) * (static_cast<double>(sellPoint) / totalHours);
			if (price >= 9223372036854775808.0 || price < -9223372036854775808.0) return 0;
			return static_cast<std::int32_t>(static_cast<std::uint32_t>(static_cast<std::int64_t>(price)));
		}
	}
}

#endif
