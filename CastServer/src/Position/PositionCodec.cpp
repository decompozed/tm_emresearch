/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#include "Position/PositionCodec.h"
#include <bit>
#include "Combat/Validation.h"

namespace Cast::Position
{
	using namespace Structures;

	std::size_t reportSize(const PositionReport& report) noexcept
	{
		return sizeof(PositionReport) + (report.time.hasShot ? sizeof(PositionShot) : 0) + (report.motion.hasJump ? sizeof(PositionJump) : 0);
	}

	std::string_view decode(std::uint8_t mission, std::uint8_t extra, std::uint8_t option, std::span<const std::uint8_t> payload,
		PlayerPosition& out) noexcept
	{
		if (mission != 0) return "bad mission";
		if (payload.size() < sizeof(PositionReport)) return "bad size";
		out = PlayerPosition{};
		out.report = Combat::load<PositionReport>(payload);
		if (payload.size() != reportSize(out.report)) return "size does not match the shot/jump flags";
		if (!Combat::isFinite(out.report.position) || !Combat::isFinite(out.report.velocity)) return "non-finite position";

		std::size_t offset = sizeof(PositionReport);
		if (out.report.time.hasShot)
		{
			out.shot = Combat::load<PositionShot>(payload, offset);
			offset += sizeof(PositionShot);
			if (out.shot.info.weaponSlot < positionShotFirstSlot || out.shot.info.weaponSlot > positionShotLastSlot) return "shot weapon slot out of 1..4";
			if (!Combat::isFinite(out.shot.target)) return "non-finite shot target";
		}
		if (out.report.motion.hasJump)
		{
			out.jump = Combat::load<PositionJump>(payload, offset);
			if (!Combat::isFinite(out.jump.startZ)) return "non-finite jump start";
		}

		out.pitchDegrees = std::bit_cast<std::int8_t>(extra);
		out.tiltDegrees = std::bit_cast<std::int8_t>(option);
		return {};
	}

	std::size_t encode(std::span<std::uint8_t, maxPositionEntrySize> out, std::uint32_t sessionId, const PlayerPosition& position, bool hidden) noexcept
	{
		const auto& report = position.report;
		const bool hasShot = report.time.hasShot;
		const bool hasJump = report.motion.hasJump;

		PositionEntryFlags flags{};
		flags.sessionId = sessionId & 0x3FFF;
		flags.hasMovement = 1;
		flags.hasShot = hasShot;
		flags.actionState = report.motion.actionState;
		flags.hasRotation = 1;
		flags.moveState = report.motion.moveState;
		flags.jumpToggle = report.motion.jumpToggle;
		flags.hasJump = hasJump;

		PositionMovement movement{ report.position, report.velocity };
		if (hidden) movement.position.z = 0;

		PositionRotation rotation{};
		rotation.pitchDegrees = position.pitchDegrees;
		rotation.tiltDegrees = position.tiltDegrees;
		rotation.yawDegrees = report.motion.yawDegrees;
		rotation.weaponSlot = report.motion.weaponSlot;

		std::size_t offset = 0;
		const auto put = [&](const auto& block) { Combat::store(out, offset, block); offset += sizeof(block); };
		put(flags);
		put(movement);
		if (hasShot)
		{
			PositionShot shot{};
			shot.target = position.shot.target;
			shot.info.weaponSlot = position.shot.info.weaponSlot;
			shot.info.sniperFlag = position.shot.info.sniperFlag;
			put(shot);
		}
		put(rotation);
		if (hasJump) put(position.jump);
		return offset;
	}
}
