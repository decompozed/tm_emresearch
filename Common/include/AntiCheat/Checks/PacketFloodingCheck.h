#ifndef PACKET_FLOODING_CHECK
#define PACKET_FLOODING_CHECK

#include "../Interfaces.h"
#include <cstddef>
#include <deque>
#include <iomanip>
#include <sstream>
#include <string>
#include <unordered_map>

namespace Ac
{
    class PacketFloodChecker : public IACChecker<PacketFloodingEvent>
    {
    public:
        std::optional<ACFlag> processEvent(const PacketFloodingEvent& event) override
        {
            auto& times = m_playerData[event.sessionId][event.packetId];
            times.push_back(event.eventTime);
            while (!times.empty() && event.eventTime - times.front() > event.analysisWindowMs)
                times.pop_front();

            if (times.size() <= event.maxPacketsPerSecond)
                return std::nullopt;

            ACFlag flag{
                event.accountId,
                event.floodingType,
                "Packet flood (ID " + std::to_string(event.packetId) + "): " + std::to_string(times.size()) + " packets in "
                    + seconds(times.back() - times.front()) + "s (max " + std::to_string(event.maxPacketsPerSecond) + ")"
            };
            times.clear();
            return flag;
        }

    private:
        std::unordered_map<std::uint32_t, std::unordered_map<std::uint32_t, std::deque<std::uint64_t>>> m_playerData;

        static std::string seconds(std::uint64_t ms)
        {
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(2) << static_cast<double>(ms) / 1000.0;
            return oss.str();
        }
    };
}

#endif
