#ifndef CAST_COMBAT_RULES_H
#define CAST_COMBAT_RULES_H

#include "Combat/World.h"

namespace Cast::Combat
{
	[[nodiscard]] Verdict explosionDamage(World& world, const Packet& packet);
	[[nodiscard]] Verdict itemEffect(World& world, const Packet& packet);
	[[nodiscard]] Verdict stateKill(World& world, const Packet& packet);
	[[nodiscard]] Verdict effectOutcome(World& world, const Packet& packet);
	[[nodiscard]] Verdict multiHit(World& world, const Packet& packet);
	[[nodiscard]] Verdict singleHit(World& world, const Packet& packet);
	[[nodiscard]] Verdict projectileLaunch(World& world, const Packet& packet);
	[[nodiscard]] Verdict reload(World& world, const Packet& packet);

	[[nodiscard]] Verdict itemDamageRequest(World& world, const Packet& packet);
	[[nodiscard]] Verdict multiHitRequest(World& world, const Packet& packet);
	[[nodiscard]] Verdict singleHitRequest(World& world, const Packet& packet);
	[[nodiscard]] Verdict projectileRequest(World& world, const Packet& packet);
	[[nodiscard]] Verdict reloadRequest(World& world, const Packet& packet);

	[[nodiscard]] Verdict respawnRequest(World& world, const Packet& packet);
	[[nodiscard]] Verdict playerRespawn(World& world, const Packet& packet);
	[[nodiscard]] Verdict botMove(World& world, const Packet& packet);
	[[nodiscard]] Verdict botRespawn(World& world, const Packet& packet);
	[[nodiscard]] Verdict npcMoves(World& world, const Packet& packet);
	[[nodiscard]] Verdict npcProjectile(World& world, const Packet& packet);
	[[nodiscard]] Verdict monsterSpawn(World& world, const Packet& packet);
	[[nodiscard]] Verdict bossSchedule(World& world, const Packet& packet);

	[[nodiscard]] Verdict itemPickupRequest(World& world, const Packet& packet);
	[[nodiscard]] Verdict itemPickup(World& world, const Packet& packet);
	[[nodiscard]] Verdict itemUseRequest(World& world, const Packet& packet);
	[[nodiscard]] Verdict itemUse(World& world, const Packet& packet);
	[[nodiscard]] Verdict mapItemSpawn(World& world, const Packet& packet);
	[[nodiscard]] Verdict itemGrant(World& world, const Packet& packet);
	[[nodiscard]] Verdict bombActionRequest(World& world, const Packet& packet);
	[[nodiscard]] Verdict bombAction(World& world, const Packet& packet);
	[[nodiscard]] Verdict bombDropRequest(World& world, const Packet& packet);
	[[nodiscard]] Verdict mapItemSnapshot(World& world, const Packet& packet);
	[[nodiscard]] Verdict inventorySnapshot(World& world, const Packet& packet);
	[[nodiscard]] Verdict effectSnapshot(World& world, const Packet& packet);
}

#endif
