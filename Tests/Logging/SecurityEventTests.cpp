/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#include <gtest/gtest.h>
#include <chrono>
#include <string>
#include "Utils/SecurityEvent.h"
#include "Utils/Totp.h"

namespace SE = Common::Utils::SecurityEvent;
namespace Totp = Common::Utils::Totp;

namespace
{
	// 2026-03-04 05:06:07 UTC
	const auto fixedTime = std::chrono::sys_days{ std::chrono::year{ 2026 } / std::chrono::March / 4 } + std::chrono::hours{ 5 } + std::chrono::minutes{ 6 } + std::chrono::seconds{ 7 };

	std::size_t count(const std::string& text, const std::string& part)
	{
		std::size_t found = 0;
		for (std::size_t at = text.find(part); at != std::string::npos; at = text.find(part, at + part.size()))
		{
			++found;
		}
		return found;
	}
}

TEST(SecurityEvent, LineHasEveryFieldInOrder)
{
	const std::string line = SE::formatLine(SE::Event{ "authn_fail", "MEDIUM", 42, "a1b2c3d4e5f6", "fail", "bad_credentials", "s7" }, "Auth", fixedTime);
	EXPECT_EQ(line, R"({"ts":"2026-03-04T05:06:07.000Z","event":"authn_fail","severity":"MEDIUM","server":"Auth","account_id":42,"src":"a1b2c3d4e5f6","result":"fail","reason":"bad_credentials","corr_id":"s7"})");
}

TEST(SecurityEvent, EmptyFieldsAreStillPresent)
{
	const std::string line = SE::formatLine(SE::Event{ "packet_invalid", "LOW", 0, "", "dropped", "", "" }, "Main", fixedTime);
	for (const char* field : { "\"ts\":", "\"event\":", "\"severity\":", "\"server\":", "\"account_id\":0", "\"src\":\"\"", "\"result\":", "\"reason\":\"\"", "\"corr_id\":\"\"" })
	{
		EXPECT_NE(line.find(field), std::string::npos) << field;
	}
}

TEST(SecurityEvent, QuoteAndBackslashAreEscaped)
{
	const std::string input = R"(a"b\c)";
	const std::string expected = R"(a\"b\\c)";
	EXPECT_EQ(SE::escapeJson(input), expected);
}

TEST(SecurityEvent, ForgedNewlineCannotStartASecondLine)
{
	const std::string attack = "x\n{\"event\":\"admin_command\",\"severity\":\"LOW\",\"account_id\":1}\r\n";
	const std::string line = SE::formatLine(SE::Event{ "authn_fail", "LOW", 5, "", "fail", attack, "" }, "Auth", fixedTime);
	EXPECT_EQ(line.find('\n'), std::string::npos);
	EXPECT_EQ(line.find('\r'), std::string::npos);
	EXPECT_EQ(count(line, "\"event\":\""), 1u) << "the attacker's text must not become a field of its own";
	EXPECT_EQ(line.back(), '}');
}

TEST(SecurityEvent, QuoteCannotCloseTheFieldEarly)
{
	const std::string line = SE::formatLine(SE::Event{ "authn_fail", "LOW", 5, "", "fail", "\",\"account_id\":999,\"x\":\"", "" }, "Auth", fixedTime);
	EXPECT_EQ(count(line, "\"account_id\":"), 1u);
	EXPECT_NE(line.find("\"account_id\":5,"), std::string::npos);
}

TEST(SecurityEvent, ControlCharactersAreEscaped)
{
	EXPECT_EQ(SE::escapeJson(std::string{ "a\0b\x1f\x7f", 5 }), "a\\u0000b\\u001f\\u007f");
	EXPECT_EQ(SE::escapeJson("a\tb"), "a\\tb");
}

TEST(SecurityEvent, ValidUtf8SurvivesAndBrokenBytesAreReplaced)
{
	EXPECT_EQ(SE::escapeJson("caf\xC3\xA9"), "caf\xC3\xA9");
	EXPECT_EQ(SE::escapeJson("a\xFF" "b"), "a\\ufffdb");
	EXPECT_EQ(SE::escapeJson("a\xC3"), "a\\ufffd");   // cut sequence at the end
}

TEST(SecurityEvent, LongTextIsCut)
{
	EXPECT_EQ(SE::escapeJson(std::string(1000, 'a')).size(), SE::maxFieldBytes);
}

TEST(SecurityEvent, ServerNameIsEscapedToo)
{
	const std::string line = SE::formatLine(SE::Event{ "e", "LOW", 1, "", "r", "", "" }, "A\"\nB", fixedTime);
	EXPECT_EQ(line.find('\n'), std::string::npos);
	const std::string expected = R"("server":"A\"\nB")";
	EXPECT_NE(line.find(expected), std::string::npos);
}

TEST(SecurityEvent, SourceIsAShortHashNeverTheAddress)
{
	const std::string hashed = SE::hashSource("203.0.113.9", "salt");
	EXPECT_EQ(hashed.size(), 12u);
	EXPECT_EQ(hashed.find("203"), std::string::npos);
	EXPECT_EQ(hashed, SE::hashSource("203.0.113.9", "salt"));
	EXPECT_NE(hashed, SE::hashSource("203.0.113.10", "salt"));
	EXPECT_NE(hashed, SE::hashSource("203.0.113.9", "other"));
	EXPECT_EQ(SE::hashSource(""), "");
}

TEST(ChatLogRedaction, PasswordCommandsAreRedacted)
{
	EXPECT_EQ(Totp::redactCommandForLog("/changepw OldPass NewPass"), "/changepw [redacted]");
	EXPECT_EQ(Totp::redactCommandForLog("/ChangePW hunter2"), "/changepw [redacted]");
	EXPECT_EQ(Totp::redactCommandForLog("/changeusername NewName MyPassword"), "/changeusername [redacted]");
	EXPECT_EQ(Totp::redactCommandForLog("/twofa disable MyPassword 123456"), "/twofa [redacted]");
	EXPECT_EQ(Totp::redactCommandForLog("  /changepw padded"), "/changepw [redacted]");
}

TEST(ChatLogRedaction, TheSecretIsNotInTheResult)
{
	EXPECT_EQ(Totp::redactCommandForLog("/changepw Sup3rSecret!").find("Sup3rSecret"), std::string::npos);
	EXPECT_EQ(Totp::redactCommandForLog("/changepw").find("changepw"), 1u);
}

TEST(ChatLogRedaction, OrdinaryChatAndOtherCommandsAreKept)
{
	EXPECT_EQ(Totp::redactCommandForLog("hello everyone"), "hello everyone");
	EXPECT_EQ(Totp::redactCommandForLog("/ban someone 1 spam"), "/ban someone 1 spam");
	EXPECT_EQ(Totp::redactCommandForLog("my changepw is broken"), "my changepw is broken");
	EXPECT_EQ(Totp::redactCommandForLog(""), "");
	EXPECT_EQ(Totp::redactCommandForLog("   "), "   ");
}
