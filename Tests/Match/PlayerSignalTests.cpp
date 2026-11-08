#include <gtest/gtest.h>
#include <cstring>
#include "../Combat/FakeWorld.h"
#include "Match/Rules.h"
#include "Structures/Match/MatchFlowPackets.h"

using namespace CombatTests;

namespace
{
	constexpr std::uint32_t speaker = 3;

	Frame voice(std::uint32_t voiceId)
	{
		Frame frame{ 280, 0, 0 };
		frame.from(speaker).add(VoiceRequest{ voiceId });
		return frame;
	}

	NameHeartbeat name(const char* text)
	{
		NameHeartbeat heartbeat{};
		std::memcpy(heartbeat.name.data(), text, std::min<std::size_t>(std::strlen(text), heartbeat.name.size()));
		return heartbeat;
	}

	Frame heartbeat(const char* text)
	{
		Frame frame{ 284, nameHeartbeatExtra, 0 };
		frame.from(speaker).to(hostSession).add(name(text));
		return frame;
	}

	Frame skillRequest()
	{
		Frame frame{ 429, 0, 0 };
		frame.from(speaker).to(hostSession).add(SkillRequest{ 42 });
		return frame;
	}

	Frame skillOutcome(const UniqueId& player)
	{
		Frame frame{ 429, 0, 0 };
		frame.add(SkillUse{ 42, player });
		return frame;
	}
}

TEST(VoiceMessage, BuildsTheMessageForEveryoneElse)
{
	FakeWorld world;
	auto frame = voice(17);
	EXPECT_EQ(Cast::Match::voiceMessage(world, frame.packet()).route, Route::Handled);
	ASSERT_EQ(world.sent.size(), 1u);
	const auto& sent = world.sent[0];
	EXPECT_EQ(sent.route, Route::MatchExceptSender);
	EXPECT_EQ(sent.sender, speaker);
	EXPECT_EQ(sent.order, 273);
	EXPECT_EQ(sent.mission, 0);
	EXPECT_EQ(sent.extra, 0);
	EXPECT_EQ(sent.option, 0);
	ASSERT_EQ(sent.payload.size(), sizeof(VoiceMessage));
	const auto message = sent.at<VoiceMessage>(0);
	EXPECT_EQ(raw(message.speaker), raw(uid(speaker)));
	EXPECT_EQ(message.voiceId, 17u);
}

TEST(VoiceMessage, BadCommandsDropped)
{
	FakeWorld world;
	auto longer = voice(1);
	longer.add(std::uint32_t{ 0 });
	EXPECT_EQ(Cast::Match::voiceMessage(world, longer.packet()).route, Route::Drop);
	auto withExtra = voice(1);
	withExtra.extra = 1;
	EXPECT_EQ(Cast::Match::voiceMessage(world, withExtra.packet()).route, Route::Drop);
	auto fromStranger = voice(1);
	fromStranger.from(9);
	EXPECT_EQ(Cast::Match::voiceMessage(world, fromStranger.packet()).route, Route::Drop);
	EXPECT_TRUE(world.sent.empty());
}

TEST(NameHeartbeat, NameRewrittenAndSentToTheHost)
{
	FakeWorld world;
	world.nicknames[speaker] = "Player";
	auto frame = heartbeat("SomeoneElse");
	EXPECT_EQ(Cast::Match::nameHeartbeat(world, frame.packet()).route, Route::Host);
	const auto rewritten = frame.at<NameHeartbeat>(0);
	EXPECT_EQ(std::memcmp(rewritten.name.data(), name("Player").name.data(), rewritten.name.size()), 0);
}

TEST(NameHeartbeat, FullLengthNicknameKept)
{
	FakeWorld world;
	world.nicknames[speaker] = "ABCDEFGHIJKLMNOP";
	auto frame = heartbeat("x");
	EXPECT_EQ(Cast::Match::nameHeartbeat(world, frame.packet()).route, Route::Host);
	EXPECT_EQ(std::memcmp(frame.at<NameHeartbeat>(0).name.data(), "ABCDEFGHIJKLMNOP", 16), 0);
}

TEST(NameHeartbeat, BadCommandsDropped)
{
	FakeWorld world;
	auto unknownNickname = heartbeat("Player");
	EXPECT_EQ(Cast::Match::nameHeartbeat(world, unknownNickname.packet()).route, Route::Drop);
	world.nicknames[speaker] = "Player";
	auto mainServerExtra = heartbeat("Player");
	mainServerExtra.extra = 42;
	EXPECT_EQ(Cast::Match::nameHeartbeat(world, mainServerExtra.packet()).route, Route::Drop);
	Frame shorter{ 284, nameHeartbeatExtra, 0 };
	shorter.from(speaker).add(std::uint32_t{ 0 });
	EXPECT_EQ(Cast::Match::nameHeartbeat(world, shorter.packet()).route, Route::Drop);
	auto withOption = heartbeat("Player");
	withOption.option = 1;
	EXPECT_EQ(Cast::Match::nameHeartbeat(world, withOption.packet()).route, Route::Drop);
}

TEST(SkillUse, RequestGoesToTheHost)
{
	FakeWorld world;
	auto frame = skillRequest();
	EXPECT_EQ(Cast::Match::skillUse(world, frame.packet()).route, Route::Host);
}

TEST(SkillUse, BadRequestDropped)
{
	FakeWorld world;
	auto longer = skillRequest();
	longer.add(std::uint32_t{ 0 });
	EXPECT_EQ(Cast::Match::skillUse(world, longer.packet()).route, Route::Drop);
	auto withExtra = skillRequest();
	withExtra.extra = 1;
	EXPECT_EQ(Cast::Match::skillUse(world, withExtra.packet()).route, Route::Drop);
	auto fromStranger = skillRequest();
	fromStranger.from(9);
	EXPECT_EQ(Cast::Match::skillUse(world, fromStranger.packet()).route, Route::Drop);
}

TEST(SkillUse, OutcomeFromTheHostGoesToTheMatch)
{
	FakeWorld world;
	auto frame = skillOutcome(uid(speaker));
	EXPECT_EQ(Cast::Match::skillUse(world, frame.packet()).route, Route::Match);
}

TEST(SkillUse, BadOutcomeDropped)
{
	FakeWorld world;
	auto stranger = skillOutcome(uid(9));
	EXPECT_EQ(Cast::Match::skillUse(world, stranger.packet()).route, Route::Drop);
	auto otherServer = skillOutcome(uid(speaker, server + 1));
	EXPECT_EQ(Cast::Match::skillUse(world, otherServer.packet()).route, Route::Drop);
	auto flagged = uid(speaker);
	flagged.unknown = 1;
	auto badKey = skillOutcome(flagged);
	EXPECT_EQ(Cast::Match::skillUse(world, badKey.packet()).route, Route::Drop);
	Frame shorter{ 429, 0, 0 };
	shorter.add(SkillRequest{ 42 });
	EXPECT_EQ(Cast::Match::skillUse(world, shorter.packet()).route, Route::Drop);
}
