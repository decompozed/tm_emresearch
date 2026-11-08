#ifndef CAST_ROOMS_POSITION_BATCHER_H
#define CAST_ROOMS_POSITION_BATCHER_H

#include <Utils/Constants.h>
#include <vector>
#include <array>
#include <cstring>
#include <span>
#include "Structures/Position/PositionPackets.h"

namespace Cast::Domain
{
    class PositionBatcher
    {
    public:
        PositionBatcher()
        {
            m_pending.reserve(Common::Constants::maxPacketBytes * 4);
            m_sizes.reserve(128);
        }

        void enqueue(std::span<const std::uint8_t> entry)
        {
            if (entry.size() < Structures::minPositionEntrySize || entry.size() > Structures::maxPositionEntrySize) return;
            m_pending.insert(m_pending.end(), entry.begin(), entry.end());
            m_sizes.push_back(static_cast<std::uint8_t>(entry.size()));
        }

        bool empty() const { return m_sizes.empty(); }

        template<class Sink>
        void flush(std::uint32_t roomTickCentiseconds, Sink&& sink)
        {
            const Structures::PositionBatchHeader header{ roomTickCentiseconds };
            std::size_t index = 0;
            std::size_t offset = 0;

            while (index < m_sizes.size())
            {
                std::memcpy(m_buffer.data(), &header, sizeof(header));
                std::size_t totalSize = sizeof(header);
                std::size_t count = 0;

                for (; index < m_sizes.size() && totalSize + m_sizes[index] <= m_buffer.size(); ++index)
                {
                    const auto size = m_sizes[index];
                    std::memcpy(m_buffer.data() + totalSize, m_pending.data() + offset, size);
                    offset += size;
                    totalSize += size;
                    ++count;
                }

                sink(static_cast<std::uint8_t>(count), std::span<const std::uint8_t>{ m_buffer.data(), totalSize });
            }

            m_pending.clear();
            m_sizes.clear();
        }

    private:
        std::vector<std::uint8_t> m_pending;
        std::vector<std::uint8_t> m_sizes;
        std::array<std::uint8_t, Structures::maxPayloadSize> m_buffer{};
    };
}
#endif
