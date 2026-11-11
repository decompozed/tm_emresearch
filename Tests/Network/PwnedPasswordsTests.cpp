#include <gtest/gtest.h>

#include <string>

#include "Utils/PwnedPasswords.h"

namespace Pwned = Common::Utils::PwnedPasswords;

TEST(PwnedPasswords, Sha1IsUpperCaseHexOfTheBytes)
{
	EXPECT_EQ(Pwned::sha1Hex(""), "DA39A3EE5E6B4B0D3255BFEF95601890AFD80709");
	EXPECT_EQ(Pwned::sha1Hex("abc"), "A9993E364706816ABA3E25717850C26C9CD0D89D");
	EXPECT_EQ(Pwned::sha1Hex("password"), "5BAA61E4C9B93F3F0682250B6CF8331B7EE68FD8");
}

TEST(PwnedPasswords, OnlyTheFirstFiveCharactersAreSent)
{
	const std::string hash = Pwned::sha1Hex("password");
	EXPECT_EQ(hash.substr(0, Pwned::prefixLength), "5BAA6");
	EXPECT_EQ(hash.substr(Pwned::prefixLength).size(), Pwned::suffixLength);
}

TEST(PwnedPasswords, CountOfAListedSuffix)
{
	const std::string body =
		"003D68EB55068C33ACE09247EE4C639306B:3\r\n"
		"1E4C9B93F3F0682250B6CF8331B7EE68FD8:10434004\r\n"
		"FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF:0\r\n";
	EXPECT_EQ(Pwned::countInRange(body, "1E4C9B93F3F0682250B6CF8331B7EE68FD8"), 10434004u);
	EXPECT_EQ(Pwned::countInRange(body, "1e4c9b93f3f0682250b6cf8331b7ee68fd8"), 10434004u) << "hex case does not matter";
}

TEST(PwnedPasswords, UnlistedSuffixAndPaddingCountAsNotFound)
{
	const std::string body =
		"003D68EB55068C33ACE09247EE4C639306B:3\n"
		"1E4C9B93F3F0682250B6CF8331B7EE68FD8:0\n"; // a padding line has count 0
	EXPECT_EQ(Pwned::countInRange(body, "1E4C9B93F3F0682250B6CF8331B7EE68FD8"), 0u);
	EXPECT_EQ(Pwned::countInRange(body, "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"), 0u);
}

TEST(PwnedPasswords, SomethingThatIsNotARangeGivesNoAnswer)
{
	EXPECT_FALSE(Pwned::countInRange("", "1E4C9B93F3F0682250B6CF8331B7EE68FD8").has_value());
	EXPECT_FALSE(Pwned::countInRange("<html><body>Service unavailable</body></html>", "1E4C9B93F3F0682250B6CF8331B7EE68FD8").has_value());
	EXPECT_FALSE(Pwned::countInRange("1E4C9B93F3F0682250B6CF8331B7EE68FD8:", "1E4C9B93F3F0682250B6CF8331B7EE68FD8").has_value());
	EXPECT_FALSE(Pwned::countInRange("1E4C9B93:5\r\n", "1E4C9B93F3F0682250B6CF8331B7EE68FD8").has_value()) << "short suffix";
	EXPECT_FALSE(Pwned::countInRange("1E4C9B93F3F0682250B6CF8331B7EE68FD8:-4", "1E4C9B93F3F0682250B6CF8331B7EE68FD8").has_value());
}

TEST(PwnedPasswords, BadLinesAreSkippedNotTrusted)
{
	const std::string body =
		"garbage\r\n"
		"1E4C9B93F3F0682250B6CF8331B7EE68FD8:12abc\r\n"
		"003D68EB55068C33ACE09247EE4C639306B:3";
	EXPECT_EQ(Pwned::countInRange(body, "1E4C9B93F3F0682250B6CF8331B7EE68FD8"), 0u);
	EXPECT_EQ(Pwned::countInRange(body, "003D68EB55068C33ACE09247EE4C639306B"), 3u) << "the last line has no newline";
}
