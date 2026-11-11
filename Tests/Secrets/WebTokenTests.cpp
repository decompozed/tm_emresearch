#include <gtest/gtest.h>

#include <chrono>
#include <string>

#include "Utils/WebToken.h"

namespace WebToken = Common::Utils::WebToken;

namespace
{
	// Fake keys only
	const std::string key = "fake-current-key-aaaaaaaaaaaaaaaaaaaaaaaa";
	const std::string oldKey = "fake-previous-key-bbbbbbbbbbbbbbbbbbbbbbb";
	const std::string attackerKey = "fake-forged-key-cccccccccccccccccccccccc";

	// What the existing staff tooling signs: a role and no audience
	std::string staffToken(const std::string& signingKey, int role)
	{
		return jwt::create()
			.set_type("JWT")
			.set_payload_claim("role", jwt::claim(picojson::value(static_cast<std::int64_t>(role))))
			.set_issued_at(std::chrono::system_clock::now())
			.set_expires_at(std::chrono::system_clock::now() + std::chrono::hours(1))
			.sign(jwt::algorithm::hs256{ signingKey });
	}

	bool staffAccepts(const std::string& token)
	{
		try
		{
			const auto decoded = jwt::decode(token);
			Common::Utils::JwtKeys::verifyHs256(decoded, key, "");
			return WebToken::staffTokenAllowed(decoded);
		}
		catch (const std::exception&)
		{
			return false;
		}
	}
}

TEST(WebToken, IssuedTokenHasTheContractClaims)
{
	const auto now = std::chrono::system_clock::now();
	const auto decoded = jwt::decode(WebToken::issue(42, 1, key, now));

	EXPECT_EQ(decoded.get_algorithm(), "HS256");
	EXPECT_EQ(decoded.get_issuer(), "toymayhem-api");
	ASSERT_EQ(decoded.get_audience().size(), 1u);
	EXPECT_EQ(*decoded.get_audience().begin(), "toymayhem-web");
	EXPECT_EQ(decoded.get_subject(), "42");
	EXPECT_EQ(decoded.get_payload_claim("role").as_integer(), 1);
	const auto iat = std::chrono::duration_cast<std::chrono::seconds>(decoded.get_issued_at().time_since_epoch()).count();
	const auto exp = std::chrono::duration_cast<std::chrono::seconds>(decoded.get_expires_at().time_since_epoch()).count();
	EXPECT_EQ(exp - iat, 3600);
}

TEST(WebToken, RoundTripGivesAccountIdAndRole)
{
	const auto claims = WebToken::verify(WebToken::issue(1234, 1, key), key, "");
	ASSERT_TRUE(claims.has_value());
	EXPECT_EQ(claims->accountId, 1234u);
	EXPECT_EQ(claims->role, 1);
}

TEST(WebToken, PreviousKeyIsAcceptedDuringARotation)
{
	const std::string token = WebToken::issue(7, 1, oldKey);
	EXPECT_TRUE(WebToken::verify(token, key, oldKey).has_value());
	EXPECT_FALSE(WebToken::verify(token, key, "").has_value());
}

TEST(WebToken, WrongKeyAndTamperingAreRefused)
{
	EXPECT_FALSE(WebToken::verify(WebToken::issue(7, 1, attackerKey), key, oldKey).has_value());

	std::string token = WebToken::issue(7, 1, key);
	// a character inside the signature: the last one carries only 4 bits, and a change in its 2 padding bits decodes the same
	const std::size_t at = token.rfind('.') + 10;
	token[at] = token[at] == 'A' ? 'B' : 'A';
	EXPECT_FALSE(WebToken::verify(token, key, "").has_value());
	EXPECT_FALSE(WebToken::verify("", key, "").has_value());
	EXPECT_FALSE(WebToken::verify("not.a.token", key, "").has_value());
}

TEST(WebToken, ExpiredTokenIsRefused)
{
	const auto twoHoursAgo = std::chrono::system_clock::now() - std::chrono::hours(2);
	EXPECT_FALSE(WebToken::verify(WebToken::issue(7, 1, key, twoHoursAgo), key, "").has_value());
}

TEST(WebToken, StaffTokenIsNotAWebsiteToken)
{
	EXPECT_FALSE(WebToken::verify(staffToken(key, 9), key, "").has_value());
}

TEST(WebToken, TokenForAnotherIssuerOrAudienceIsRefused)
{
	const auto now = std::chrono::system_clock::now();
	const auto make = [&](const std::string& iss, const std::string& aud, bool withExp, const std::string& sub) {
		auto builder = jwt::create().set_issuer(iss).set_audience(aud).set_subject(sub)
			.set_payload_claim("role", jwt::claim(picojson::value(static_cast<std::int64_t>(1)))).set_issued_at(now);
		if (withExp)
		{
			builder.set_expires_at(now + std::chrono::hours(1));
		}
		return builder.sign(jwt::algorithm::hs256{ key });
	};

	EXPECT_TRUE(WebToken::verify(make("toymayhem-api", "toymayhem-web", true, "5"), key, "").has_value());
	EXPECT_FALSE(WebToken::verify(make("someone-else", "toymayhem-web", true, "5"), key, "").has_value());
	EXPECT_FALSE(WebToken::verify(make("toymayhem-api", "toymayhem-admin", true, "5"), key, "").has_value());
	EXPECT_FALSE(WebToken::verify(make("toymayhem-api", "toymayhem-web", false, "5"), key, "").has_value()) << "a token that never expires";
	EXPECT_FALSE(WebToken::verify(make("toymayhem-api", "toymayhem-web", true, "abc"), key, "").has_value());
	EXPECT_FALSE(WebToken::verify(make("toymayhem-api", "toymayhem-web", true, "0"), key, "").has_value());
	EXPECT_FALSE(WebToken::verify(make("toymayhem-api", "toymayhem-web", true, "99999999999"), key, "").has_value());
	EXPECT_FALSE(WebToken::verify(make("toymayhem-api", "toymayhem-web", true, "-1"), key, "").has_value());
}

TEST(WebToken, UnsignedTokenIsRefused)
{
	const auto now = std::chrono::system_clock::now();
	const std::string unsignedToken = jwt::create().set_issuer("toymayhem-api").set_audience(std::string{ "toymayhem-web" }).set_subject("5")
		.set_payload_claim("role", jwt::claim(picojson::value(static_cast<std::int64_t>(1)))).set_expires_at(now + std::chrono::hours(1))
		.sign(jwt::algorithm::none{});
	EXPECT_FALSE(WebToken::verify(unsignedToken, key, "").has_value());
}

TEST(WebToken, StaffEndpointsRefuseWebsiteTokensWhateverTheRole)
{
	for (const int role : { 1, 3, 4, 7, 8, 9, 1000 })
	{
		EXPECT_FALSE(staffAccepts(WebToken::issue(1, role, key))) << "role " << role;
	}
}

TEST(WebToken, StaffEndpointsStillAcceptTokensWithoutTheWebsiteAudience)
{
	EXPECT_TRUE(staffAccepts(staffToken(key, 9)));
	EXPECT_TRUE(staffAccepts(staffToken(key, 3)));
}

TEST(WebToken, StaffEndpointsRefuseTheWebsiteAudienceInAnArray)
{
	const auto now = std::chrono::system_clock::now();
	const std::string token = jwt::create().set_audience(picojson::array{ picojson::value(std::string{ "something" }), picojson::value(std::string{ "toymayhem-web" }) })
		.set_payload_claim("role", jwt::claim(picojson::value(static_cast<std::int64_t>(9)))).set_expires_at(now + std::chrono::hours(1))
		.sign(jwt::algorithm::hs256{ key });
	EXPECT_FALSE(staffAccepts(token));
}

TEST(WebToken, StaffEndpointsDoNotAcceptAForgedWebsiteTokenEither)
{
	EXPECT_FALSE(staffAccepts(WebToken::issue(1, 9, attackerKey)));
}
