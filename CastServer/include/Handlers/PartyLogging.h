#ifndef CAST_HANDLERS_PARTY_LOGGING_H
#define CAST_HANDLERS_PARTY_LOGGING_H
#include <algorithm>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include "App/ServerContext.h"
#include "HaveApi/Lava.h"
#include "Rooms/Room.h"
namespace Cast::Handlers
{
    // The CastServer's side of the mode logs (HaveApi/ModeLog.h): only party (SquareMode) rooms write.
    [[nodiscard]] std::string describePlayer(Domain::Room& room, const Network::Session& player);
    [[nodiscard]] std::string describeSession(Domain::Room& room, std::uint32_t session);
    [[nodiscard]] std::string describeSessions(Domain::Room& room, std::span<const std::uint32_t> sessions); // names (session ids)
    [[nodiscard]] std::string describeLava(const Common::HaveApi::LavaState& state);
    void logParty(Domain::Room& room, std::string_view event);

    // the sessions a zone / storm answer lists
    template<class Answer>
    [[nodiscard]] std::span<const std::uint32_t> listedSessions(const Answer& answer)
    {
        return { answer.sessions.data(), (std::min)(static_cast<std::size_t>(answer.count), answer.sessions.size()) };
    }
    void writePartyHits(Domain::Room& room); // the hit summary since the last one (round end)
}
#endif
