#ifndef CAST_HANDLERS_MATCH_MATCH_HANDLERS_H
#define CAST_HANDLERS_MATCH_MATCH_HANDLERS_H
#include "Handlers/RuleDispatcher.h"
namespace Cast::Handlers
{
    class MatchHandlers
    {
    public:
        explicit MatchHandlers(ServerContext& ctx) : m_rules(ctx) {}
        void registerRoutes(Network::PacketRouter<Network::Session>& router);
    private:
        RuleDispatcher m_rules;
    };
}
#endif
