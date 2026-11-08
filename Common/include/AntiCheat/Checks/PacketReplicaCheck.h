#ifndef PACKET_REPLICATION_CHECK
#define PACKET_REPLICATION_CHECK

#include "../Interfaces.h"
#include <cstddef>
#include <functional>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Ac
{
    class PacketReplicationChecker : public IACChecker<PacketReplicationEvent>
    {
    public:
        static constexpr std::uint64_t analysisWindowMs = 20000;
        static constexpr std::size_t replicaThreshold = 5;

        std::optional<ACFlag> processEvent(const PacketReplicationEvent& event) override
        {
            auto& playerPackets = m_playerData[event.sessionId];
            prune(playerPackets, event.eventTime);

            auto& timestamps = playerPackets[packetHash(event.data)];
            timestamps.push_back(event.eventTime);
            if (timestamps.size() < replicaThreshold)
                return std::nullopt;

            ACFlag flag{
                event.accountId,
                "Possible Packet Replication (e.g. WPE)",
                "Packet replication detected (ID " + std::to_string(event.packetId) + ", session " + std::to_string(event.sessionId)
                    + "): " + std::to_string(timestamps.size()) + " identical packets within " + seconds(analysisWindowMs) + "s window"
            };
            m_playerData.erase(event.sessionId);
            return flag;
        }

        std::size_t trackedSessions() const noexcept { return m_playerData.size(); }

    private:
        using Timestamps = std::vector<std::uint64_t>;
        using PacketsByHash = std::unordered_map<std::size_t, Timestamps>;

        std::unordered_map<std::uint32_t, PacketsByHash> m_playerData;

        static void prune(PacketsByHash& packets, std::uint64_t now)
        {
            for (auto it = packets.begin(); it != packets.end();)
            {
                std::erase_if(it->second, [now](std::uint64_t ts) { return now - ts > analysisWindowMs; });
                it = it->second.empty() ? packets.erase(it) : std::next(it);
            }
        }

        static std::size_t packetHash(const std::vector<std::uint8_t>& data)
        {
            return std::hash<std::string_view>{}(std::string_view(reinterpret_cast<const char*>(data.data()), data.size()));
        }

        static std::string seconds(std::uint64_t ms)
        {
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(2) << static_cast<double>(ms) / 1000.0;
            return oss.str();
        }
    };
}

#endif
