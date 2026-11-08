#ifndef CAST_HANDLERS_POSITION_HANDLERS_H
#define CAST_HANDLERS_POSITION_HANDLERS_H
#include "App/ServerContext.h"
#include "Network/PacketRouter.h"
#include "Network/Sessions/CastSession.h"
#include "Rooms/Room.h"
namespace Cast::Handlers
{
    class PositionHandlers
    {
    public:
        explicit PositionHandlers(ServerContext& ctx) : m_ctx(ctx) {}
        void registerRoutes(Network::PacketRouter<Network::Session>& router);
    private:
        using Packet = Common::Network::UnecryptedPacket;
        using SessionPtr = std::shared_ptr<Network::Session>;

        void onPlayerPosition(const Packet& request, const SessionPtr& session);
        bool killedByPartyArea(Domain::Room& room, Network::Session& session, const Structures::Half3& at);
        void updateZones(Domain::Room& room, std::uint32_t sessionId, float x, float y, float z);
        void updateStorm(Domain::Room& room, std::uint32_t sessionId, float x, float y);
        bool killedByLava(Domain::Room& room, Network::Session& session, float x, float y, float z);

        ServerContext& m_ctx;
    };
}
#endif
