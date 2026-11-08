#include <gtest/gtest.h>
#include <array>
#include <bit>
#include <cstring>
#include <vector>
#include "Combat/Validation.h"
#include "Position/PositionCodec.h"
#include "Rooms/PositionBatcher.h"

using namespace Cast::Structures;
using Cast::Combat::load;

namespace
{
	constexpr std::uint16_t one = 0x3C00;
	constexpr std::uint16_t infinity = 0x7C00;

	PositionReport report(bool shot, bool jump)
	{
		PositionReport r{};
		r.position = { 0x5000, 0x5100, 0x5200 };
		r.velocity = { one, 0, 0xBC00 };
		r.time.matchTick = 123456;
		r.time.hasShot = shot;
		r.motion.actionState = 0x7F;
		r.motion.moveState = 18;
		r.motion.weaponSlot = 3;
		r.motion.yawDegrees = 359;
		r.motion.jumpToggle = 1;
		r.motion.hasJump = jump;
		r.motion.reserved = 0xF;
		return r;
	}

	PositionShot shot(std::uint16_t slot)
	{
		PositionShot s{};
		s.target = { 0x4000, 0x4100, 0x4200 };
		s.info.weaponSlot = static_cast<std::uint16_t>(slot & 0xF);
		s.info.sniperFlag = 1;
		s.info.reserved = 0x7FF;
		return s;
	}

	template<class... T>
	std::vector<std::uint8_t> bytes(const T&... parts)
	{
		std::vector<std::uint8_t> out;
		(out.insert(out.end(), reinterpret_cast<const std::uint8_t*>(&parts), reinterpret_cast<const std::uint8_t*>(&parts) + sizeof(T)), ...);
		return out;
	}

	std::string_view decode(const std::vector<std::uint8_t>& payload, PlayerPosition& out, std::uint8_t mission = 0, std::uint8_t extra = 0xF6, std::uint8_t option = 20)
	{
		return Cast::Position::decode(mission, extra, option, payload, out);
	}

	std::size_t clientEntrySize(const PositionEntryFlags& f)
	{
		return 4 + (f.hasMovement ? 12 : 0) + (f.hasShot ? 8 : 0) + (f.hasRotation ? 4 : 0) + (f.hasJump ? 4 : 0);
	}

	std::vector<std::uint8_t> entry(const PlayerPosition& position, std::uint32_t session, bool hidden = false)
	{
		std::array<std::uint8_t, maxPositionEntrySize> out{};
		const auto size = Cast::Position::encode(out, session, position, hidden);
		return { out.begin(), out.begin() + static_cast<std::ptrdiff_t>(size) };
	}
}

TEST(PositionDecode, BasicReportAndHeaderAngles)
{
	PlayerPosition position{};
	ASSERT_TRUE(decode(bytes(report(false, false)), position).empty());
	EXPECT_EQ(position.report.position.y, 0x5100);
	EXPECT_EQ(position.report.time.matchTick, 123456u);
	EXPECT_EQ(position.pitchDegrees, -10);
	EXPECT_EQ(position.tiltDegrees, 20);
}

TEST(PositionDecode, EveryVariantSize)
{
	PlayerPosition position{};
	EXPECT_TRUE(decode(bytes(report(true, false), shot(1)), position).empty());
	EXPECT_EQ(position.shot.info.weaponSlot, 1u);
	EXPECT_TRUE(decode(bytes(report(false, true), PositionJump{ one, 42 }), position).empty());
	EXPECT_EQ(position.jump.elapsedCentiseconds, 42);
	EXPECT_TRUE(decode(bytes(report(true, true), shot(4), PositionJump{ one, 7 }), position).empty());
	EXPECT_EQ(position.jump.elapsedCentiseconds, 7);
}

TEST(PositionDecode, SizeMustMatchFlags)
{
	PlayerPosition position{};
	EXPECT_FALSE(decode(bytes(report(true, false)), position).empty());
	EXPECT_FALSE(decode(bytes(report(false, false), shot(1)), position).empty());
	EXPECT_FALSE(decode(bytes(report(false, true)), position).empty());
	EXPECT_FALSE(decode(bytes(report(true, true), shot(1)), position).empty());
	EXPECT_FALSE(decode(bytes(report(true, true), shot(1), PositionJump{}, PositionJump{}), position).empty());
}

TEST(PositionDecode, TruncatedOrEmptyDropped)
{
	PlayerPosition position{};
	EXPECT_FALSE(decode({}, position).empty());
	auto payload = bytes(report(false, false));
	for (std::size_t size = 0; size < payload.size(); ++size)
	{
		const std::vector<std::uint8_t> truncated(payload.begin(), payload.begin() + static_cast<std::ptrdiff_t>(size));
		EXPECT_FALSE(decode(truncated, position).empty());
	}
}

TEST(PositionDecode, MissionMustBeZero)
{
	PlayerPosition position{};
	EXPECT_FALSE(decode(bytes(report(false, false)), position, 1).empty());
}

TEST(PositionDecode, ShotSlotOnlyOneToFour)
{
	PlayerPosition position{};
	for (const std::uint16_t slot : std::array<std::uint16_t, 4>{ 0, 5, 7, 15 })
	{
		EXPECT_FALSE(decode(bytes(report(true, false), shot(slot)), position).empty());
	}
	for (const std::uint16_t slot : std::array<std::uint16_t, 4>{ 1, 2, 3, 4 })
	{
		EXPECT_TRUE(decode(bytes(report(true, false), shot(slot)), position).empty());
	}
}

TEST(PositionDecode, NonFiniteHalvesDropped)
{
	PlayerPosition position{};
	auto badPosition = report(false, false);
	badPosition.position.z = infinity;
	EXPECT_FALSE(decode(bytes(badPosition), position).empty());
	auto badVelocity = report(false, false);
	badVelocity.velocity.x = 0xFE00;
	EXPECT_FALSE(decode(bytes(badVelocity), position).empty());
	auto badTarget = shot(1);
	badTarget.target.x = infinity;
	EXPECT_FALSE(decode(bytes(report(true, false), badTarget), position).empty());
	EXPECT_FALSE(decode(bytes(report(false, true), PositionJump{ infinity, 0 }), position).empty());
}

TEST(PositionEncode, FieldByFieldEntry)
{
	PlayerPosition position{};
	ASSERT_TRUE(decode(bytes(report(true, true), shot(3), PositionJump{ 0x4400, 55 }), position).empty());
	const auto out = entry(position, 0x12345);
	ASSERT_EQ(out.size(), maxPositionEntrySize);

	const auto flags = load<PositionEntryFlags>(out);
	EXPECT_EQ(flags.sessionId, 0x2345u);
	EXPECT_EQ(flags.hasMovement, 1u);
	EXPECT_EQ(flags.hasShot, 1u);
	EXPECT_EQ(flags.actionState, 0x7Fu);
	EXPECT_EQ(flags.hasRotation, 1u);
	EXPECT_EQ(flags.moveState, 18u);
	EXPECT_EQ(flags.jumpToggle, 1u);
	EXPECT_EQ(flags.hasJump, 1u);

	const auto movement = load<PositionMovement>(out, 4);
	EXPECT_EQ(movement.position.z, 0x5200);
	EXPECT_EQ(movement.velocity.z, 0xBC00);

	const auto shotBlock = load<PositionShot>(out, 16);
	EXPECT_EQ(shotBlock.target.y, 0x4100);
	EXPECT_EQ(shotBlock.info.weaponSlot, 3u);
	EXPECT_EQ(shotBlock.info.sniperFlag, 1u);
	EXPECT_EQ(shotBlock.info.reserved, 0u);

	const auto rotation = load<PositionRotation>(out, 24);
	EXPECT_EQ(rotation.pitchDegrees, -10);
	EXPECT_EQ(rotation.tiltDegrees, 20);
	EXPECT_EQ(rotation.yawDegrees, 359u);
	EXPECT_EQ(rotation.weaponSlot, 3u);
	EXPECT_EQ(rotation.reserved, 0u);

	const auto jump = load<PositionJump>(out, 28);
	EXPECT_EQ(jump.startZ, 0x4400);
	EXPECT_EQ(jump.elapsedCentiseconds, 55);
}

TEST(PositionEncode, SizesMatchTheClientReader)
{
	for (const bool withShot : { false, true })
	{
		for (const bool withJump : { false, true })
		{
			PlayerPosition position{};
			position.report = report(withShot, withJump);
			position.shot = shot(1);
			const auto out = entry(position, 7);
			EXPECT_EQ(out.size(), clientEntrySize(load<PositionEntryFlags>(out)));
			EXPECT_EQ(out.size(), Cast::Position::reportSize(position.report));
		}
	}
}

TEST(PositionEncode, HiddenPlayerHasZeroHeight)
{
	PlayerPosition position{};
	position.report = report(false, false);
	const auto out = entry(position, 7, true);
	const auto movement = load<PositionMovement>(out, 4);
	EXPECT_EQ(movement.position.z, 0);
	EXPECT_EQ(movement.position.x, 0x5000);
}

TEST(PositionBatcher, KeepsEveryEntryInOrderWithinTheClientBuffer)
{
	Cast::Domain::PositionBatcher batcher;
	constexpr std::uint32_t total = 200;
	for (std::uint32_t i = 0; i < total; ++i)
	{
		PlayerPosition position{};
		position.report = report(i % 2 == 0, i % 3 == 0);
		position.shot = shot(2);
		batcher.enqueue(entry(position, i));
	}

	std::uint32_t next = 0;
	std::size_t batches = 0;
	batcher.flush(4321, [&](std::uint8_t count, std::span<const std::uint8_t> payload)
		{
			++batches;
			ASSERT_LE(payload.size(), maxPayloadSize);
			EXPECT_EQ(load<PositionBatchHeader>(payload).roomTickCentiseconds, 4321u);
			std::size_t offset = sizeof(PositionBatchHeader);
			for (std::uint32_t i = 0; i < count; ++i)
			{
				ASSERT_LE(offset + sizeof(PositionEntryFlags), payload.size());
				const auto flags = load<PositionEntryFlags>(payload, offset);
				EXPECT_EQ(flags.sessionId, next++);
				offset += clientEntrySize(flags);
			}
			EXPECT_EQ(offset, payload.size());
		});
	EXPECT_EQ(next, total);
	EXPECT_GT(batches, 1u);
	EXPECT_TRUE(batcher.empty());
}

TEST(PositionBatcher, IgnoresMalformedEntriesAndEmptyFlush)
{
	Cast::Domain::PositionBatcher batcher;
	batcher.enqueue(std::vector<std::uint8_t>(minPositionEntrySize - 1));
	batcher.enqueue(std::vector<std::uint8_t>(maxPositionEntrySize + 1));
	EXPECT_TRUE(batcher.empty());
	bool sent = false;
	batcher.flush(0, [&](std::uint8_t, std::span<const std::uint8_t>) { sent = true; });
	EXPECT_FALSE(sent);
}
