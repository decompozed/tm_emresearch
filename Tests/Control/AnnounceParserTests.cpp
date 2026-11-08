#include <gtest/gtest.h>

#include <string>

#include "Control/AnnounceParser.h"

using Main::Control::AnnounceCommand;
using Main::Control::AnnounceKind;
using Main::Control::parseAnnounceControl;

TEST(AnnounceParser, AnnounceTypeAndMessage)
{
	const AnnounceCommand cmd = parseAnnounceControl("announce\nServer restart in 5 minutes");
	EXPECT_EQ(cmd.status, AnnounceCommand::Status::Ok);
	EXPECT_EQ(cmd.kind, AnnounceKind::Announce);
	EXPECT_EQ(cmd.message, "Server restart in 5 minutes");
}

TEST(AnnounceParser, TipType)
{
	const AnnounceCommand cmd = parseAnnounceControl("tip\nPress F to pay respects");
	EXPECT_EQ(cmd.status, AnnounceCommand::Status::Ok);
	EXPECT_EQ(cmd.kind, AnnounceKind::Tip);
	EXPECT_EQ(cmd.message, "Press F to pay respects");
}

TEST(AnnounceParser, TypeIsCaseInsensitiveAndTrimmed)
{
	const AnnounceCommand cmd = parseAnnounceControl("  TIP \r\nhello");
	EXPECT_EQ(cmd.kind, AnnounceKind::Tip);
	EXPECT_EQ(cmd.message, "hello");
}

TEST(AnnounceParser, UnknownTypeDefaultsToAnnounce)
{
	const AnnounceCommand cmd = parseAnnounceControl("banana\nhello");
	EXPECT_EQ(cmd.status, AnnounceCommand::Status::Ok);
	EXPECT_EQ(cmd.kind, AnnounceKind::Announce);
	EXPECT_EQ(cmd.message, "hello");
}

TEST(AnnounceParser, EmptyFileIsEmpty)
{
	EXPECT_EQ(parseAnnounceControl("").status, AnnounceCommand::Status::Empty);
}

TEST(AnnounceParser, TypeLineOnlyIsEmpty)
{
	EXPECT_EQ(parseAnnounceControl("announce\n   \n").status, AnnounceCommand::Status::Empty);
}

TEST(AnnounceParser, MessageAtLimitIsOk)
{
	const std::string message(Main::Control::kMaxAnnounceBytes, 'x');
	const AnnounceCommand cmd = parseAnnounceControl("announce\n" + message);
	EXPECT_EQ(cmd.status, AnnounceCommand::Status::Ok);
	EXPECT_EQ(cmd.message.size(), Main::Control::kMaxAnnounceBytes);
}

TEST(AnnounceParser, MessageOverLimitIsTooLong)
{
	const std::string message(Main::Control::kMaxAnnounceBytes + 1, 'x');
	EXPECT_EQ(parseAnnounceControl("announce\n" + message).status, AnnounceCommand::Status::TooLong);
}

TEST(AnnounceParser, MultilineMessageKeepsInnerNewlines)
{
	const AnnounceCommand cmd = parseAnnounceControl("announce\nline one\nline two\n");
	EXPECT_EQ(cmd.status, AnnounceCommand::Status::Ok);
	EXPECT_EQ(cmd.message, "line one\nline two");
}
