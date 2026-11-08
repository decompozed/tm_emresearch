#ifndef CAST_HANDLERS_RULE_DISPATCHER_H
#define CAST_HANDLERS_RULE_DISPATCHER_H
#include <initializer_list>
#include <string_view>
#include "App/ServerContext.h"
#include "Combat/World.h"
#include "Network/PacketRouter.h"
#include "Network/Sessions/CastSession.h"
#include "Rooms/Room.h"
namespace Cast::Handlers
{
    class RuleDispatcher
    {
    public:
        using Rule = Combat::Verdict (*)(Combat::World&, const Combat::Packet&);

        explicit RuleDispatcher(ServerContext& ctx) : m_ctx(ctx) {}
        void route(Rule rule, Network::PacketRouter<Network::Session>& router, std::initializer_list<std::uint16_t> orders);
    private:
        using Packet = Common::Network::UnecryptedPacket;

        void handle(Rule rule, Packet& request, Network::Session& session);
        static void logRejected(Network::Session& session, const Packet& request, std::string_view reason);
        void logPartyDrop(Domain::Room& room, Network::Session& sender, const Combat::Packet& packet, std::string_view reason);

        ServerContext& m_ctx;
    };
}
#endif
