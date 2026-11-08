#ifndef CAST_HANDLERS_PARTY_HANDLERS_H
#define CAST_HANDLERS_PARTY_HANDLERS_H
#include <memory>
#include "App/ServerContext.h"
#include "HaveApi/Lava.h"
#include "HaveApi/PartyMatchInfo.h"
#include "HaveApi/Storm.h"
#include "HaveApi/Zone.h"
#include "Network/PacketRouter.h"
#include "Rooms/Room.h"
namespace Cast::Handlers
{
    class PartyHandlers
    {
    public:
        explicit PartyHandlers(ServerContext& ctx) : m_ctx(ctx) {}
        void registerRoutes(Network::PacketRouter<Common::Network::Session>& router);
    private:
        using Packet = Common::Network::UnecryptedPacket;
        using SessionPtr = std::shared_ptr<Common::Network::Session>;

        void onKillPlayer(const Packet& request);
        void onLava(const Packet& request, const SessionPtr& link);
        void onZone(const Packet& request, const SessionPtr& link);
        void onStorm(const Packet& request, const SessionPtr& link);
        void onMatchInfo(const Packet& request);

        Common::HaveApi::LavaState runLava(Domain::Room& room, const Common::HaveApi::LavaCommand& command, std::uint32_t sessionId);
        Common::HaveApi::LavaState startLava(Domain::Room& room, const Common::HaveApi::LavaCommand& command);
        Common::HaveApi::LavaState protectPlayers(Domain::Room& room, bool protect);
        Common::HaveApi::LavaState killPlayer(Domain::Room& room, std::uint32_t sessionId);
        static Common::HaveApi::LavaState probe(Domain::Room& room, std::uint32_t sessionId);
        Common::HaveApi::StormAnswer runStorm(Domain::Room& room, const Common::HaveApi::StormCommand& command);
        static Common::HaveApi::ZoneAnswer runZone(Domain::Room& room, const Common::HaveApi::ZoneCommand& command);

        ServerContext& m_ctx;
    };
}
#endif
