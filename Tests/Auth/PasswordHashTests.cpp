#include <gtest/gtest.h>

#include <string>

#include "Utils/PasswordHash.h"

namespace PasswordHash = Common::Utils::PasswordHash;

namespace
{
	const PasswordHash::Pepper pepper1{ 1, std::string(32, 'a') };
	const PasswordHash::Pepper pepper2{ 2, std::string(32, 'b') };

	PasswordHash::Peppers only(const PasswordHash::Pepper& pepper)
	{
		return PasswordHash::Peppers{ pepper, std::nullopt };
	}

	std::string bcryptOf(const std::string& password)
	{
		return BCrypt::generateHash(password, 4); // the lowest cost, these tests only need the format
	}
}

TEST(PasswordHash, StoredValueNamesPepperVersionAlgorithmAndCost)
{
	const auto stored = PasswordHash::hash("correct horse", pepper1);
	ASSERT_TRUE(stored.has_value());
	EXPECT_TRUE(stored->starts_with("p1$argon2id$v=19$m=65536,t=3,p=1$")) << *stored;
}

TEST(PasswordHash, CorrectPasswordIsAcceptedAndWrongOneRefused)
{
	const auto stored = PasswordHash::hash("correct horse", pepper1);
	EXPECT_TRUE(PasswordHash::verify("correct horse", *stored, only(pepper1)));
	EXPECT_FALSE(PasswordHash::verify("correct horsf", *stored, only(pepper1)));
	EXPECT_FALSE(PasswordHash::verify("", *stored, only(pepper1)));
}

TEST(PasswordHash, TheSamePasswordGetsADifferentHashEachTime)
{
	EXPECT_NE(*PasswordHash::hash("same password", pepper1), *PasswordHash::hash("same password", pepper1));
}

TEST(PasswordHash, AStolenDatabaseIsUselessWithoutThePepper)
{
	const auto stored = PasswordHash::hash("correct horse", pepper1);
	const PasswordHash::Pepper guessed{ 1, std::string(32, 'z') };
	EXPECT_FALSE(PasswordHash::verify("correct horse", *stored, only(guessed)));
	EXPECT_FALSE(PasswordHash::verify("correct horse", *stored, PasswordHash::Peppers{}));
}

TEST(PasswordHash, TheHashCannotBeCheckedDirectlyWithThePassword)
{
	const auto stored = PasswordHash::hash("correct horse", pepper1);
	const std::string argon2 = PasswordHash::parse(*stored)->argon2;
	const std::string password = "correct horse";
	EXPECT_NE(crypto_pwhash_str_verify(argon2.c_str(), password.c_str(), password.size()), 0);
}

TEST(PasswordHash, PreviousPepperStillVerifiesAndAsksForAnUpgrade)
{
	const auto stored = PasswordHash::hash("correct horse", pepper1);
	const PasswordHash::Peppers rotated{ pepper2, pepper1 };
	EXPECT_TRUE(PasswordHash::verify("correct horse", *stored, rotated));
	EXPECT_TRUE(PasswordHash::needsUpgrade(*stored, rotated));
}

TEST(PasswordHash, AnUnknownPepperVersionIsRefused)
{
	const auto stored = PasswordHash::hash("correct horse", pepper2);
	EXPECT_FALSE(PasswordHash::verify("correct horse", *stored, only(pepper1)));
}

TEST(PasswordHash, ACurrentHashNeedsNoUpgrade)
{
	EXPECT_FALSE(PasswordHash::needsUpgrade(*PasswordHash::hash("correct horse", pepper1), only(pepper1)));
}

TEST(PasswordHash, WeakerCostParametersAskForAnUpgrade)
{
	ASSERT_TRUE(PasswordHash::ready());
	char weak[crypto_pwhash_STRBYTES];
	ASSERT_EQ(crypto_pwhash_str(weak, "x", 1, 1, 8 * 1024 * 1024), 0);
	EXPECT_TRUE(PasswordHash::needsUpgrade(std::string("p1") + weak, only(pepper1)));
}

TEST(PasswordHash, OldBcryptHashesKeepWorking)
{
	const std::string stored = bcryptOf("old password");
	EXPECT_TRUE(PasswordHash::verify("old password", stored, only(pepper1)));
	EXPECT_FALSE(PasswordHash::verify("wrong password", stored, only(pepper1)));
}

TEST(PasswordHash, BcryptHashesFromOtherLibrariesAreAccepted)
{
	std::string stored = bcryptOf("old password");
	stored[2] = 'b';
	EXPECT_TRUE(PasswordHash::verify("old password", stored, only(pepper1)));
}

TEST(PasswordHash, BcryptIsUpgradedOnlyWhenAPepperIsConfigured)
{
	const std::string stored = bcryptOf("old password");
	EXPECT_TRUE(PasswordHash::needsUpgrade(stored, only(pepper1)));
	EXPECT_FALSE(PasswordHash::needsUpgrade(stored, PasswordHash::Peppers{}));
}

TEST(PasswordHash, EveryCharacterOfALongPasswordCounts)
{
	const std::string first(100, 'x');
	const std::string second = std::string(99, 'x') + "y";
	const auto stored = PasswordHash::hash(first, pepper1);
	EXPECT_TRUE(PasswordHash::verify(first, *stored, only(pepper1)));
	EXPECT_FALSE(PasswordHash::verify(second, *stored, only(pepper1)));
}

TEST(PasswordHash, MalformedStoredValuesAreRefusedWithoutThrowing)
{
	for (const std::string stored : { "", "plaintext", "$argon2id$", "p$argon2id$x", "pX$argon2id$x", "p1$argon2id$garbage", "p1$", "$2a$", "$2a$12$short" })
	{
		EXPECT_FALSE(PasswordHash::verify("password", stored, only(pepper1))) << stored;
		EXPECT_NO_THROW(PasswordHash::needsUpgrade(stored, only(pepper1))) << stored;
	}
}
