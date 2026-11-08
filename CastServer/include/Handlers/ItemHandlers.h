#ifndef CAST_HANDLERS_ITEM_HANDLERS_H
#define CAST_HANDLERS_ITEM_HANDLERS_H
#include "Handlers/RuleDispatcher.h"
namespace Cast::Handlers
{
    class ItemHandlers
    {
    public:
        explicit ItemHandlers(ServerContext& ctx) : m_rules(ctx) {}
        void registerRoutes(Network::PacketRouter<Network::Session>& router);
    private:
        RuleDispatcher m_rules;
    };
}
#endif
