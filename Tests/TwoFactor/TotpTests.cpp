#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "Utils/Totp.h"

namespace Totp = Common::Utils::Totp;

namespace
{
	// Base32 of the ASCII seed "12345678901234567890" that RFC 6238 Appendix B uses for SHA-1
	const std::string rfcSecret = "GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ";
	const std::string appSecret = "JBSWY3DPEHPK3PXPJBSWY3DPEHPK3PXP";
	constexpr std::time_t now = 1'800'000'015; // the middle of a time step

	std::string codeAt(std::time_t time)
	{
		return Totp::formatCode(*Totp::codeForStep(appSecret, Totp::stepForTime(time)));
	}

	struct RfcVector
	{
		std::time_t time;
		std::uint32_t code;
	};
}

TEST(Totp, MatchesAllRfc6238Sha1VectorsWithEightDigits)
{
	const std::vector<RfcVector> vectors{ { 59, 94287082 }, { 1111111109, 7081804 }, { 1111111111, 14050471 },
		{ 1234567890, 89005924 }, { 2000000000, 69279037 }, { 20000000000, 65353130 } };

	for (const auto& vector : vectors)
	{
		EXPECT_EQ(Totp::codeForStep(rfcSecret, Totp::stepForTime(vector.time), 8), vector.code) << "T=" << vector.time;
	}
}

TEST(Totp, SixDigitCodeIsTheEndOfTheRfcVector)
{
	EXPECT_EQ(Totp::codeForStep(rfcSecret, Totp::stepForTime(59)), 287082u);
}

TEST(Totp, FormatKeepsLeadingZeros)
{
	EXPECT_EQ(Totp::formatCode(81804), "081804");
	EXPECT_EQ(Totp::formatCode(0), "000000");
}

TEST(Totp, AcceptsTheCurrentCode)
{
	const auto result = Totp::matchCode(appSecret, codeAt(now), now, 0);
	EXPECT_EQ(result.status, Totp::MatchStatus::Valid);
	EXPECT_EQ(result.step, Totp::stepForTime(now));
}

TEST(Totp, AcceptsOneStepOfClockDriftInBothDirections)
{
	EXPECT_EQ(Totp::matchCode(appSecret, codeAt(now - 30), now, 0).status, Totp::MatchStatus::Valid);
	EXPECT_EQ(Totp::matchCode(appSecret, codeAt(now + 30), now, 0).status, Totp::MatchStatus::Valid);
}

TEST(Totp, RejectsCodesOutsideTheWindow)
{
	EXPECT_EQ(Totp::matchCode(appSecret, codeAt(now - 60), now, 0).status, Totp::MatchStatus::Invalid);
	EXPECT_EQ(Totp::matchCode(appSecret, codeAt(now + 60), now, 0).status, Totp::MatchStatus::Invalid);
}

TEST(Totp, RejectsAWrongCode)
{
	const std::string right = codeAt(now);
	const std::string wrong = right == "000000" ? "000001" : "000000";
	EXPECT_EQ(Totp::matchCode(appSecret, wrong, now, 0).status, Totp::MatchStatus::Invalid);
}

TEST(Totp, RejectsAReplayOfTheSameCode)
{
	const std::string code = codeAt(now);
	const auto first = Totp::matchCode(appSecret, code, now, 0);
	ASSERT_EQ(first.status, Totp::MatchStatus::Valid);

	EXPECT_EQ(Totp::matchCode(appSecret, code, now, first.step).status, Totp::MatchStatus::Replayed);
	EXPECT_EQ(Totp::matchCode(appSecret, code, now + 14, first.step).status, Totp::MatchStatus::Replayed);
}

TEST(Totp, AnOlderCodeCannotBeUsedAfterANewerOne)
{
	const auto newer = Totp::matchCode(appSecret, codeAt(now), now, 0);
	ASSERT_EQ(newer.status, Totp::MatchStatus::Valid);
	EXPECT_EQ(Totp::matchCode(appSecret, codeAt(now - 30), now, newer.step).status, Totp::MatchStatus::Replayed);
}

TEST(Totp, TheNextCodeWorksAfterAReplayWasRefused)
{
	const auto first = Totp::matchCode(appSecret, codeAt(now), now, 0);
	EXPECT_EQ(Totp::matchCode(appSecret, codeAt(now + 30), now + 30, first.step).status, Totp::MatchStatus::Valid);
}

TEST(Totp, MalformedInputIsInvalidAndDoesNotThrow)
{
	for (const std::string code : { "", "12345", "1234567", "12a456", "+12345", "      " })
	{
		EXPECT_EQ(Totp::matchCode(appSecret, code, now, 0).status, Totp::MatchStatus::Invalid) << code;
	}
	EXPECT_EQ(Totp::matchCode("not base32!", "123456", now, 0).status, Totp::MatchStatus::Invalid);
	EXPECT_EQ(Totp::matchCode("", "123456", now, 0).status, Totp::MatchStatus::Invalid);
}

TEST(Totp, GeneratedSecretsAre160BitBase32AndDiffer)
{
	std::set<std::string> secrets;
	for (int i = 0; i < 200; ++i)
	{
		const std::string secret = Totp::generateSecret();
		EXPECT_EQ(secret.size(), 32u);
		EXPECT_TRUE(Totp::isBase32(secret));
		secrets.insert(secret);
	}
	EXPECT_EQ(secrets.size(), 200u);
}

TEST(Totp, AGeneratedSecretWorksEndToEnd)
{
	const std::string secret = Totp::generateSecret();
	const std::string code = Totp::formatCode(*Totp::codeForStep(secret, Totp::stepForTime(now)));
	EXPECT_EQ(Totp::matchCode(secret, code, now, 0).status, Totp::MatchStatus::Valid);
}

TEST(RecoveryCodes, EightUniqueCodesInTheExpectedFormat)
{
	const auto codes = Totp::generateRecoveryCodes();
	EXPECT_EQ(codes.size(), 8u);
	EXPECT_EQ(std::set<std::string>(codes.begin(), codes.end()).size(), 8u);
	for (const auto& code : codes)
	{
		EXPECT_TRUE(Totp::isRecoveryCode(code)) << code;
		EXPECT_FALSE(Totp::isTotpCode(code)) << code;
	}
}

TEST(RecoveryCodes, FormatsAreNotConfused)
{
	EXPECT_TRUE(Totp::isTotpCode("123456"));
	EXPECT_FALSE(Totp::isRecoveryCode("123456"));
	EXPECT_TRUE(Totp::isRecoveryCode("ABCD2345"));
	EXPECT_FALSE(Totp::isRecoveryCode("ABCD1890")); // 0, 1, 8 and 9 are not base32
	EXPECT_FALSE(Totp::isRecoveryCode("abcd2345")); // the login upper-cases first
}

TEST(ChatLogRedaction, TwoFaCommandsNeverReachTheLogInFull)
{
	EXPECT_EQ(Totp::redactCommandForLog("/twofa disable MyPassword 123456"), "/twofa [redacted]");
	EXPECT_EQ(Totp::redactCommandForLog("/TwoFa setup MyPassword"), "/twofa [redacted]");
}

TEST(ChatLogRedaction, NormalChatIsUntouched)
{
	EXPECT_EQ(Totp::redactCommandForLog("hello everyone"), "hello everyone");
	EXPECT_EQ(Totp::redactCommandForLog("/ban someone"), "/ban someone");
}
