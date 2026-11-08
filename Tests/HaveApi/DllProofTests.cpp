#include <gtest/gtest.h>
#include <random>
#include <string>
#include <vector>
#include "HaveApi/DllProof.h"

// The DLL (core/DllProof.cpp, Windows BCrypt) and the server (Crypto++) must compute the same proof (D73); the
// expected value was computed independently (Python hashlib / hmac).
TEST(DllProof, MatchesReferenceVector)
{
	namespace CH = Common::HaveApi;
	const CH::Sha256 key = CH::sha256(std::vector<std::uint8_t>{ 'a', 'b', 'c' });
	const std::string region = "region";
	const CH::Sha256 digest = CH::sha256(reinterpret_cast<const std::uint8_t*>(region.data()), region.size());
	const CH::Sha256 proof = CH::dllProof(key, 0x01020304, { digest }, { 0xE9, 0xE9 });
	const CH::Sha256 expected = { 0x5d, 0x64, 0x3e, 0xe9, 0x1c, 0x70, 0x20, 0xcb, 0xdd, 0x24, 0xd6, 0x52, 0xd0, 0x55, 0x1c, 0x5a,
		0x7f, 0x79, 0xc3, 0xc3, 0x95, 0x2e, 0x36, 0x1c, 0xce, 0xbb, 0xa2, 0xe3, 0x7a, 0x24, 0xba, 0xe1 };
	EXPECT_EQ(proof, expected);
	EXPECT_NE(CH::dllProof(key, 0x01020305, { digest }, { 0xE9, 0xE9 }), expected);
	EXPECT_NE(CH::dllProof(key, 0x01020304, { digest }, { 0xE9, 0x55 }), expected); // a hook missing
}

// The challenge regions stay inside the DLL's code, and their digests do not depend on the relocated slots.
TEST(DllProof, ChallengeOnTheReleasedDll)
{
	namespace CH = Common::HaveApi;
	const auto dll = CH::DllImage::load("../../../Setup/have_api.dll"); // build/Tests/Release -> repository root
	if (!dll) GTEST_SKIP() << "Setup/have_api.dll not present";
	ASSERT_FALSE(dll->code.empty());
	ASSERT_FALSE(dll->relocations.empty());
	std::mt19937 random{ 7 };
	for (int i = 0; i < 50; ++i)
	{
		const CH::DllChallenge challenge = CH::makeChallenge({ *dll }, random, 1000u + static_cast<std::uint32_t>(i));
		ASSERT_EQ(challenge.regions.size(), 4u);
		ASSERT_EQ(challenge.probes.size(), 6u);
		EXPECT_TRUE(CH::expectedProof(*dll, challenge).has_value());
	}
	// a relocated slot zeroed: changing it (another load address) leaves the digest unchanged
	CH::DllImage moved = *dll;
	const std::uint32_t slot = dll->relocations[dll->relocations.size() / 2];
	moved.image[slot] ^= 0x5A;
	const CH::DllRegion around{ slot - 8, 32 };
	if (dll->regionDigest(around))
	{
		EXPECT_EQ(dll->regionDigest(around), moved.regionDigest(around));
	}
}
