#ifndef CAST_HANDLERS_ROOM_COMBAT_WORLD_H
#define CAST_HANDLERS_ROOM_COMBAT_WORLD_H
#include "App/ServerContext.h"
#include "Combat/World.h"
#include "HaveApi/CombatEvent.h"
#include "Rooms/Room.h"
namespace Cast::Handlers
{
    void reportPartyCombatEvent(ServerContext& ctx, std::uint32_t mode, const Common::HaveApi::CombatEvent& event);
    void reportServerDeath(ServerContext& ctx, std::uint32_t victimAccountId);

    class RoomCombatWorld final : public Combat::World
    {
    public:
        RoomCombatWorld(ServerContext& ctx, Domain::Room& room) noexcept : m_ctx(ctx), m_room(room) {}

        [[nodiscard]] std::uint16_t serverId() const noexcept override { return m_ctx.serverId; }
        [[nodiscard]] std::uint32_t mode() const noexcept override { return m_room.getMode(); }
        [[nodiscard]] bool modeKnown() const noexcept override { return m_room.isModeKnown(); }
        [[nodiscard]] bool isHost(std::uint32_t session) const noexcept override { return m_room.isHost(session); }
        [[nodiscard]] bool isAuthorizedMember(std::uint32_t session) const noexcept override { return m_room.isAuthorizedMember(session); }
        [[nodiscard]] std::optional<Combat::PlayerState> player(std::uint32_t session) const override;
        [[nodiscard]] bool disarmed(std::uint32_t session) const override { return m_room.lava().disarmed(session); }
        [[nodiscard]] bool sparesPlayers() const override;
        [[nodiscard]] std::optional<std::uint32_t> explosiveOwner(std::uint32_t ammoId) const override { return m_room.explosiveOwner(ammoId); }
        [[nodiscard]] std::string_view nickname(std::uint32_t session) const override;

        void rememberExplosive(std::uint32_t ammoId, std::uint32_t ownerSession) override;
        void hit(std::uint32_t target, std::optional<std::uint32_t> attacker, Combat::Cause cause, bool spared) override;
        void death(std::uint32_t victim, std::optional<std::uint32_t> killer, Combat::Cause cause) override;
        void respawn(std::uint32_t host, std::uint32_t player, const Structures::Half3& position) override;
        void revive(std::uint32_t player) override;
        bool send(const Combat::Outgoing& packet) override;
        void endMatch() override { m_room.endMatch(); }
        void startMatch() override {}
        void leaveMatch(std::uint32_t session) override;
        void finishLoading(std::uint32_t session) override;

    private:
        [[nodiscard]] Common::HaveApi::CombatEvent event(const Network::Session& target, std::optional<std::uint32_t> attacker,
            Combat::Cause cause, Common::HaveApi::CombatEventKind kind) const;

        ServerContext& m_ctx;
        Domain::Room& m_room;
    };
}
#endif
