#ifndef CAST_MATCH_RULES_H
#define CAST_MATCH_RULES_H

#include "Combat/World.h"

namespace Cast::Match
{
	using Combat::Packet;
	using Combat::Verdict;
	using Combat::World;

	[[nodiscard]] Verdict matchEnd(World& world, const Packet& packet);
	[[nodiscard]] Verdict guestReady(World& world, const Packet& packet);
	[[nodiscard]] Verdict matchLeave(World& world, const Packet& packet);
	[[nodiscard]] Verdict matchStart(World& world, const Packet& packet);
	[[nodiscard]] Verdict matchStop(World& world, const Packet& packet);
	[[nodiscard]] Verdict loadingProgress(World& world, const Packet& packet);

	[[nodiscard]] Verdict joinStateRequest(World& world, const Packet& packet);
	[[nodiscard]] Verdict hostClock(World& world, const Packet& packet);
	[[nodiscard]] Verdict joinStats(World& world, const Packet& packet);
	[[nodiscard]] Verdict unitStates(World& world, const Packet& packet);
	[[nodiscard]] Verdict modeState(World& world, const Packet& packet);
	[[nodiscard]] Verdict modeSync(World& world, const Packet& packet);

	[[nodiscard]] Verdict voiceMessage(World& world, const Packet& packet);
	[[nodiscard]] Verdict nameHeartbeat(World& world, const Packet& packet);
	[[nodiscard]] Verdict skillUse(World& world, const Packet& packet);
}

#endif
