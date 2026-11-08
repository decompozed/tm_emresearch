#include <gtest/gtest.h>

#include <chrono>
#include <string>

#include "Utils/JwtKeys.h"

namespace JwtKeys = Common::Utils::JwtKeys;

namespace
{
	// Fake keys only
	const std::string newKey = "fake-current-key-aaaaaaaaaaaaaaaaaaaaaaaa";
	const std::string oldKey = "fake-previous-key-bbbbbbbbbbbbbbbbbbbbbbb";
	const std::string attackerKey = "fake-forged-key-cccccccccccccccccccccccc";

	std::string tokenSignedWith(const std::string& key)
	{
		return jwt::create()
			.set_type("JWT")
			.set_payload_claim("role", jwt::claim(std::string("5")))
			.set_issued_at(std::chrono::system_clock::now())
			.sign(jwt::algorithm::hs256{ key });
	}

	bool accepted(const std::string& token, const std::string& current, const std::string& previous)
	{
		try
		{
			JwtKeys::verifyHs256(jwt::decode(token), current, previous);
			return true;
		}
		catch (const std::exception&)
		{
			return false;
		}
	}
}

TEST(JwtKeys, TokenSignedWithCurrentKeyIsAccepted)
{
	EXPECT_TRUE(accepted(tokenSignedWith(newKey), newKey, ""));
	EXPECT_TRUE(accepted(tokenSignedWith(newKey), newKey, oldKey));
}

TEST(JwtKeys, WithoutPreviousKeyAnOldTokenIsRefused)
{
	EXPECT_FALSE(accepted(tokenSignedWith(oldKey), newKey, ""));
}

TEST(JwtKeys, OldKeyIsAcceptedDuringTheOverlap)
{
	EXPECT_TRUE(accepted(tokenSignedWith(oldKey), newKey, oldKey));
}

TEST(JwtKeys, OldKeyIsRefusedAfterRemoval)
{
	const std::string token = tokenSignedWith(oldKey);
	ASSERT_TRUE(accepted(token, newKey, oldKey));
	EXPECT_FALSE(accepted(token, newKey, ""));
}

TEST(JwtKeys, ForgedKeyIsAlwaysRefused)
{
	const std::string forged = tokenSignedWith(attackerKey);
	EXPECT_FALSE(accepted(forged, newKey, ""));
	EXPECT_FALSE(accepted(forged, newKey, oldKey));
}

TEST(JwtKeys, EmptyPreviousKeyDoesNotAcceptATokenSignedWithAnEmptyKey)
{
	EXPECT_FALSE(accepted(tokenSignedWith(""), newKey, ""));
}

TEST(JwtKeys, PreviousKeyIsNotTheCurrentKey)
{
	// A token signed with the current key stays valid in the overlap, and once the roles are swapped back without an overlap it is refused
	const std::string token = tokenSignedWith(newKey);
	EXPECT_TRUE(accepted(token, newKey, oldKey));
	EXPECT_FALSE(accepted(token, oldKey, ""));
}

TEST(JwtKeys, TamperedPayloadIsRefusedWithBothKeys)
{
	std::string token = tokenSignedWith(oldKey);
	const auto firstDot = token.find('.');
	token[firstDot + 1] = token[firstDot + 1] == 'A' ? 'B' : 'A';
	EXPECT_FALSE(accepted(token, newKey, oldKey));
}

TEST(JwtKeys, NoneAlgorithmIsRefused)
{
	const std::string unsignedToken = jwt::create().set_type("JWT").set_payload_claim("role", jwt::claim(std::string("5"))).sign(jwt::algorithm::none{});
	EXPECT_FALSE(accepted(unsignedToken, newKey, oldKey));
}
