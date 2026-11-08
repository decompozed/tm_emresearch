#ifndef EVENT_AC_H
#define EVENT_AC_H

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace Ac
{
    inline std::uint64_t nowMs()
    {
        using namespace std::chrono;
        return static_cast<std::uint64_t>(duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
    }

    struct ACEvent
    {
        virtual ~ACEvent() = default;

        enum class Type
        {
            PlayerPosition,
            PlayerKill,
            PacketFlooding,
            PacketReplication,
            ObserverCheck,
        } type;
    };

    struct PacketFloodingEvent : ACEvent
    {
        std::uint32_t sessionId;
        std::uint32_t accountId;
        std::size_t maxPacketsPerSecond;
        std::uint64_t analysisWindowMs;
        std::string floodingType;
        std::uint32_t packetId;
        std::uint64_t eventTime;

        PacketFloodingEvent(std::uint32_t sessionId_, std::uint32_t accountId_, std::size_t maxPacketsPerS_,
            std::uint64_t analysisWindowMs_, std::string type_, std::uint32_t packetId_, std::uint64_t eventTime_ = nowMs())
            : sessionId(sessionId_), accountId(accountId_), maxPacketsPerSecond(maxPacketsPerS_), analysisWindowMs(analysisWindowMs_),
            floodingType(std::move(type_)), packetId(packetId_), eventTime(eventTime_)
        {
            type = Type::PacketFlooding;
        }

        static constexpr Type typeValue = Type::PacketFlooding;
    };

    struct PacketReplicationEvent : ACEvent
    {
        std::uint32_t sessionId;
        std::uint32_t accountId;
        std::uint16_t packetId;
        std::vector<std::uint8_t> data;
        std::uint64_t eventTime;

        PacketReplicationEvent(std::uint32_t sessionId_, std::uint32_t accountId_, std::uint16_t packetId_,
            std::vector<std::uint8_t> data_, std::uint64_t eventTime_ = nowMs())
            : sessionId(sessionId_), accountId(accountId_), packetId(packetId_), data(std::move(data_)), eventTime(eventTime_)
        {
            type = Type::PacketReplication;
        }

        static constexpr Type typeValue = Type::PacketReplication;
    };

    struct ACFlag
    {
        std::uint32_t accountId;
        std::string cheatType;
        std::string details;
    };
}

#endif
