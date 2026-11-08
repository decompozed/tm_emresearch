#ifndef CAST_HANDLERS_ROOM_HANDLERS_H
#define CAST_HANDLERS_ROOM_HANDLERS_H
#include "App/ServerContext.h"
#include "Network/PacketRouter.h"
#include "Network/Sessions/CastSession.h"
namespace Cast::Handlers
{
    class RoomHandlers
    {
    public:
        explicit RoomHandlers(ServerContext& ctx) : m_ctx(ctx) {}
        void registerRoutes(Network::PacketRouter<Network::Session>& router);
    private:
        using Packet = Common::Network::UnecryptedPacket;
        using SessionPtr = std::shared_ptr<Network::Session>;

        void onAttach(const Packet& request, const SessionPtr& session);
        void onLeave(const SessionPtr& session);

        ServerContext& m_ctx;
    };
}
#endif
