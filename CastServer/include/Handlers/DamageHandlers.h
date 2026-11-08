#ifndef CAST_HANDLERS_DAMAGE_HANDLERS_H
#define CAST_HANDLERS_DAMAGE_HANDLERS_H
#include "Handlers/RuleDispatcher.h"
namespace Cast::Handlers
{
    class DamageHandlers
    {
    public:
        explicit DamageHandlers(ServerContext& ctx) : m_rules(ctx) {}
        void registerRoutes(Network::PacketRouter<Network::Session>& router);
    private:
        RuleDispatcher m_rules;
    };
}
#endif
