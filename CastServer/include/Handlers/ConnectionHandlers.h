#ifndef CAST_HANDLERS_PLAYER_CONNECTION_HANDLERS_H
#define CAST_HANDLERS_PLAYER_CONNECTION_HANDLERS_H
#include <optional>
#include "App/ServerContext.h"
#include "HaveApi/CastConnect.h"
#include "Network/PacketRouter.h"
#include "Network/Sessions/CastSession.h"
#include "Structures/Connection/ConnectionPackets.h"
namespace Cast::Handlers
{
    class ConnectionHandlers
    {
    public:
        explicit ConnectionHandlers(ServerContext& ctx) : m_ctx(ctx) {}
        void registerRoutes(Network::PacketRouter<Network::Session>& router);
    private:
        using Packet     = Common::Network::UnecryptedPacket;
        using SessionPtr = std::shared_ptr<Network::Session>;
        void onConnect(const Packet&, SessionPtr);
        void onVerified(SessionPtr, const Structures::ConnectRequest&, const std::optional<Common::HaveApi::CastConnectAnswer>&);
        void authenticate(const SessionPtr&, std::uint16_t sessionId, std::uint32_t accountId);
        void onPing(const Packet&, SessionPtr);
        void onCrash(const Packet&, SessionPtr);
        ServerContext& m_ctx;
    };
}
#endif
