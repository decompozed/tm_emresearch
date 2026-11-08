#ifndef CAST_STRUCTURES_MATCH_FLOW_PACKETS_H
#define CAST_STRUCTURES_MATCH_FLOW_PACKETS_H

#include <array>
#include <cstdint>
#include "Structures/Combat/CombatPackets.h"
#include "Utils/Macros.h"

namespace Cast::Structures
{
PACK_PUSH(1)
	struct LoadingProgress
	{
		UniqueId player{};
	};
	static_assert(sizeof(LoadingProgress) == 4);

	struct VoiceRequest
	{
		std::uint32_t voiceId{};
	};
	static_assert(sizeof(VoiceRequest) == 4);

	struct VoiceMessage
	{
		UniqueId speaker{};
		std::uint32_t voiceId{};
	};
	static_assert(sizeof(VoiceMessage) == 8);

	struct NameHeartbeat
	{
		std::array<char, 16> name{};
	};
	static_assert(sizeof(NameHeartbeat) == 16);

	struct SkillRequest
	{
		std::uint32_t skillId{};
	};
	static_assert(sizeof(SkillRequest) == 4);

	struct SkillUse
	{
		std::uint32_t skillId{};
		UniqueId player{};
	};
	static_assert(sizeof(SkillUse) == 8);
PACK_POP()

	inline constexpr std::uint16_t loadingProgressOrder = 257;
	inline constexpr std::uint16_t voiceMessageOrder = 273;
	inline constexpr std::uint8_t joinRequestMission = 1;
	inline constexpr std::uint8_t nameHeartbeatExtra = 52;
	inline constexpr std::uint8_t loadingDone = 9;
}

#endif
