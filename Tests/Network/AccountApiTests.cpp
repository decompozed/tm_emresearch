#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <vector>

#include "Utils/AccountApi.h"

namespace Api = Common::Utils::AccountApi;

TEST(AccountApiNames, UsernameAndNicknameFollowTheContractRegex)
{
	EXPECT_TRUE(Api::validUsername("abc"));
	EXPECT_TRUE(Api::validUsername("Player_01"));
	EXPECT_TRUE(Api::validUsername("sixteen_chars_ok"));
	EXPECT_TRUE(Api::validNickname("N1CK"));

	EXPECT_FALSE(Api::validUsername(""));
	EXPECT_FALSE(Api::validUsername("ab"));
	EXPECT_FALSE(Api::validUsername("seventeen_chars_x"));
	EXPECT_FALSE(Api::validUsername("has space"));
	EXPECT_FALSE(Api::validUsername("dash-name"));
	EXPECT_FALSE(Api::validUsername("dot.name"));
	EXPECT_FALSE(Api::validNickname("quote'name"));
	EXPECT_FALSE(Api::validNickname("a;b"));
}

TEST(AccountApiNames, NonAsciiAndControlCharactersAreRefused)
{
	EXPECT_FALSE(Api::validUsername("caf\xC3\xA9_x"));
	EXPECT_FALSE(Api::validUsername(std::string("abc\0def", 7)));
	EXPECT_FALSE(Api::validUsername("abc\n"));
	EXPECT_FALSE(Api::validUsername("abc\r\ndef"));
}

TEST(AccountApiNames, SqlAndHtmlPayloadsAreNotNames)
{
	EXPECT_FALSE(Api::validUsername("' OR 1=1 --"));
	EXPECT_FALSE(Api::validNickname("<script>x</script>"));
	EXPECT_FALSE(Api::validUsername("admin'--"));
}

TEST(AccountApiPassword, LengthIsCountedInCharactersBetween10And128)
{
	EXPECT_FALSE(Api::validPassword(std::string(9, 'a'), "someone"));
	EXPECT_TRUE(Api::validPassword(std::string(10, 'a'), "someone"));
	EXPECT_TRUE(Api::validPassword(std::string(128, 'a'), "someone"));
	EXPECT_FALSE(Api::validPassword(std::string(129, 'a'), "someone"));
	EXPECT_FALSE(Api::validPassword("", "someone"));
}

TEST(AccountApiPassword, MultiByteCharactersCountOnceEach)
{
	std::string nine;
	for (int i = 0; i < 9; ++i)
	{
		nine += "\xC3\xA9"; // e acute, two bytes
	}
	EXPECT_FALSE(Api::validPassword(nine, "someone"));
	EXPECT_TRUE(Api::validPassword(nine + "\xC3\xA9", "someone"));

	std::string many;
	for (int i = 0; i < 128; ++i)
	{
		many += "\xC3\xA9"; // 256 bytes, 128 characters
	}
	EXPECT_TRUE(Api::validPassword(many, "someone"));
	EXPECT_FALSE(Api::validPassword(many + "\xC3\xA9", "someone"));
}

TEST(AccountApiPassword, CannotEqualTheUsernameIgnoringCase)
{
	EXPECT_FALSE(Api::validPassword("PlayerName1", "playername1"));
	EXPECT_FALSE(Api::validPassword("playername1", "PLAYERNAME1"));
	EXPECT_TRUE(Api::validPassword("playername1!", "playername1"));
	EXPECT_TRUE(Api::validPassword("xplayername1", "playername1"));
}

TEST(AccountApiPassword, NulByteIsRefused)
{
	EXPECT_FALSE(Api::validPassword(std::string("abcdefghij\0k", 12), "someone"));
}

TEST(AccountApiEmail, AcceptsOrdinaryAddresses)
{
	EXPECT_TRUE(Api::validEmail("a@b.c"));
	EXPECT_TRUE(Api::validEmail("first.last+tag@sub.example.org"));
	EXPECT_TRUE(Api::validEmail("x@y.zz"));
}

TEST(AccountApiEmail, RefusesWhatTheContractRegexRefuses)
{
	EXPECT_FALSE(Api::validEmail(""));
	EXPECT_FALSE(Api::validEmail("no-at-sign.example.com"));
	EXPECT_FALSE(Api::validEmail("@example.com"));
	EXPECT_FALSE(Api::validEmail("a@"));
	EXPECT_FALSE(Api::validEmail("a@b"));
	EXPECT_FALSE(Api::validEmail("a@.b"));
	EXPECT_FALSE(Api::validEmail("a@b."));
	EXPECT_FALSE(Api::validEmail("a@@b.c"));
	EXPECT_FALSE(Api::validEmail("a@b@c.d"));
	EXPECT_FALSE(Api::validEmail("a b@c.d"));
	EXPECT_FALSE(Api::validEmail("a@b.c\n"));
	EXPECT_FALSE(Api::validEmail("a@b.c\r\nBcc: victim@example.com"));
	EXPECT_FALSE(Api::validEmail(std::string("a@b.c\0", 6)));
}

TEST(AccountApiEmail, LengthLimitIs254)
{
	const std::string domain = "@example.com";
	const std::string ok = std::string(254 - domain.size(), 'a') + domain;
	EXPECT_EQ(ok.size(), 254u);
	EXPECT_TRUE(Api::validEmail(ok));
	EXPECT_FALSE(Api::validEmail("a" + ok));
}

TEST(AccountApiCors, OnlyListedOriginsMatchExactly)
{
	const std::vector<std::string> allowed{ "https://toymayhem.example", "http://localhost:5173" };
	EXPECT_TRUE(Api::originAllowed("https://toymayhem.example", allowed));
	EXPECT_TRUE(Api::originAllowed("http://localhost:5173", allowed));

	EXPECT_FALSE(Api::originAllowed("https://evil.example", allowed));
	EXPECT_FALSE(Api::originAllowed("https://toymayhem.example.evil.example", allowed));
	EXPECT_FALSE(Api::originAllowed("https://evil-toymayhem.example", allowed));
	EXPECT_FALSE(Api::originAllowed("http://toymayhem.example", allowed));
	EXPECT_FALSE(Api::originAllowed("https://TOYMAYHEM.example", allowed));
	EXPECT_FALSE(Api::originAllowed("https://toymayhem.example/", allowed));
	EXPECT_FALSE(Api::originAllowed("https://toymayhem.example:8443", allowed));
	EXPECT_FALSE(Api::originAllowed("*", allowed));
}

TEST(AccountApiCors, EmptyAndNullOriginsNeverMatchEvenIfListed)
{
	EXPECT_FALSE(Api::originAllowed("", {}));
	EXPECT_FALSE(Api::originAllowed("", { "" }));
	EXPECT_FALSE(Api::originAllowed("null", { "null" }));
}

TEST(AccountApiClientIp, HeaderIsBelievedOnlyFromTheTunnel)
{
	const std::vector<std::string> proxies{ "172.18.0.5" };
	EXPECT_EQ(Api::clientIp("127.0.0.1", "203.0.113.9", proxies), "203.0.113.9");
	EXPECT_EQ(Api::clientIp("172.18.0.5", "203.0.113.9", proxies), "203.0.113.9");
	EXPECT_EQ(Api::clientIp("198.51.100.7", "203.0.113.9", proxies), "198.51.100.7");
	EXPECT_EQ(Api::clientIp("172.18.0.6", "203.0.113.9", proxies), "172.18.0.6");
}

TEST(AccountApiClientIp, WithoutAConfiguredProxyOnlyLoopbackIsTrusted)
{
	EXPECT_EQ(Api::clientIp("127.0.0.1", "203.0.113.9", {}), "203.0.113.9");
	EXPECT_EQ(Api::clientIp("172.18.0.5", "203.0.113.9", {}), "172.18.0.5");
}

TEST(AccountApiClientIp, MissingOrMalformedHeaderFallsBackToThePeer)
{
	EXPECT_EQ(Api::clientIp("127.0.0.1", "", {}), "127.0.0.1");
	EXPECT_EQ(Api::clientIp("127.0.0.1", "not an ip", {}), "127.0.0.1");
	EXPECT_EQ(Api::clientIp("127.0.0.1", "1.2.3.4, 5.6.7.8", {}), "127.0.0.1");
	EXPECT_EQ(Api::clientIp("127.0.0.1", "999.1.1.1", {}), "127.0.0.1");
	EXPECT_EQ(Api::clientIp("127.0.0.1", "1.2.3", {}), "127.0.0.1");
	EXPECT_EQ(Api::clientIp("127.0.0.1", "1.2.3.4\r\nX: y", {}), "127.0.0.1");
	EXPECT_EQ(Api::clientIp("127.0.0.1", "'; DROP TABLE users;--", {}), "127.0.0.1");
}

TEST(AccountApiClientIp, Ipv6IsAcceptedAndLowerCased)
{
	EXPECT_EQ(Api::clientIp("127.0.0.1", "2001:DB8::1", {}), "2001:db8::1");
	EXPECT_EQ(Api::clientIp("127.0.0.1", "::ffff:1.2.3.4", {}), "::ffff:1.2.3.4");
	EXPECT_EQ(Api::clientIp("127.0.0.1", "2001:db8:::1", {}), "127.0.0.1");
	EXPECT_EQ(Api::clientIp("127.0.0.1", "12345::1", {}), "127.0.0.1");
	EXPECT_EQ(Api::clientIp("127.0.0.1", "zz::1", {}), "127.0.0.1");
}

TEST(AccountApiClientIp, PeerTrustCheck)
{
	EXPECT_TRUE(Api::peerIsTrusted("127.0.0.1", {}));
	EXPECT_TRUE(Api::peerIsTrusted("10.0.0.2", { "10.0.0.2" }));
	EXPECT_FALSE(Api::peerIsTrusted("10.0.0.3", { "10.0.0.2" }));
}

namespace
{
	using Clock = Api::RateLimiter::Clock;
	const Clock::time_point start = Clock::time_point{} + std::chrono::hours(1);
}

TEST(AccountApiRateLimiter, AllowsTheLimitThenRefuses)
{
	Api::RateLimiter limiter(5, std::chrono::hours(1));
	for (int i = 0; i < 5; ++i)
	{
		EXPECT_TRUE(limiter.allow("1.2.3.4", start + std::chrono::seconds(i))) << i;
	}
	EXPECT_FALSE(limiter.allow("1.2.3.4", start + std::chrono::seconds(6)));
	EXPECT_FALSE(limiter.allow("1.2.3.4", start + std::chrono::minutes(59)));
}

TEST(AccountApiRateLimiter, KeysDoNotShareABudget)
{
	Api::RateLimiter limiter(1, std::chrono::minutes(15));
	EXPECT_TRUE(limiter.allow("a", start));
	EXPECT_FALSE(limiter.allow("a", start));
	EXPECT_TRUE(limiter.allow("b", start));
}

TEST(AccountApiRateLimiter, WindowSlidesAndHitsExpireOneByOne)
{
	Api::RateLimiter limiter(2, std::chrono::minutes(10));
	EXPECT_TRUE(limiter.allow("k", start));
	EXPECT_TRUE(limiter.allow("k", start + std::chrono::minutes(5)));
	EXPECT_FALSE(limiter.allow("k", start + std::chrono::minutes(9)));
	// the first hit is 10 minutes old now, the second is not
	EXPECT_TRUE(limiter.allow("k", start + std::chrono::minutes(10)));
	EXPECT_FALSE(limiter.allow("k", start + std::chrono::minutes(11)));
	EXPECT_TRUE(limiter.allow("k", start + std::chrono::minutes(15)));
}

TEST(AccountApiRateLimiter, RefusedRequestsDoNotExtendTheBlock)
{
	Api::RateLimiter limiter(1, std::chrono::minutes(10));
	EXPECT_TRUE(limiter.allow("k", start));
	for (int i = 1; i < 10; ++i)
	{
		EXPECT_FALSE(limiter.allow("k", start + std::chrono::minutes(i)));
	}
	EXPECT_TRUE(limiter.allow("k", start + std::chrono::minutes(10)));
}

TEST(AccountApiRateLimiter, NumberOfKeysIsBoundedAndExpiredKeysAreReclaimed)
{
	Api::RateLimiter limiter(1, std::chrono::minutes(10), 3);
	EXPECT_TRUE(limiter.allow("a", start));
	EXPECT_TRUE(limiter.allow("b", start));
	EXPECT_TRUE(limiter.allow("c", start));
	EXPECT_FALSE(limiter.allow("d", start + std::chrono::minutes(1))) << "full table, nothing expired: fail closed";
	EXPECT_EQ(limiter.keyCount(), 3u);
	EXPECT_TRUE(limiter.allow("d", start + std::chrono::minutes(11))) << "old keys are dropped to make room";
	EXPECT_LE(limiter.keyCount(), 3u);
}

TEST(AccountApiRateLimiter, ContractLimits)
{
	// register 5 per hour, login 10 per 15 minutes, password 5 per 15 minutes
	Api::RateLimiter registerLimit(5, std::chrono::hours(1));
	Api::RateLimiter loginLimit(10, std::chrono::minutes(15));
	int registered = 0;
	int loggedIn = 0;
	for (int i = 0; i < 20; ++i)
	{
		registered += registerLimit.allow("ip", start + std::chrono::seconds(i)) ? 1 : 0;
		loggedIn += loginLimit.allow("ip", start + std::chrono::seconds(i)) ? 1 : 0;
	}
	EXPECT_EQ(registered, 5);
	EXPECT_EQ(loggedIn, 10);
}
