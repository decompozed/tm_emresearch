#ifndef CAST_HANDLERS_IPC_HANDLERS_H
#define CAST_HANDLERS_IPC_HANDLERS_H
#include <cstdint>
#include <memory>
#include "App/ServerContext.h"
#include "Network/PacketRouter.h"
namespace Cast::Handlers
{
    class IpcHandlers
    {
    public:
        explicit IpcHandlers(ServerContext& ctx) : m_ctx(ctx) {}
        void registerRoutes(Network::PacketRouter<Common::Network::Session>& router);
    private:
        using Packet = Common::Network::UnecryptedPacket;
        using SessionPtr = std::shared_ptr<Common::Network::Session>;

        void on(Network::PacketRouter<Common::Network::Session>& router, std::uint16_t order, void (IpcHandlers::*handler)(const Packet&));

        void onMapId(const Packet& request);
        void onPlayerTeamInfoBatch(const Packet& request);
        void onRoomNumber(const Packet& request);
        void onRoomSync(const Packet& request);
        void onClientIp(const Packet& request);
        void onInvisibility(const Packet& request);
        void onCloseSocket(const Packet& request);
        void onTickRate(const Packet& request);
        void onConnectAnswer(const Packet& answer, const SessionPtr& link);

        ServerContext& m_ctx;
    };
}
#endif
