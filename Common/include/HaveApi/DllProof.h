/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#ifndef COMMON_HAVE_API_DLL_PROOF_H
#define COMMON_HAVE_API_DLL_PROOF_H

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <optional>
#include <random>
#include <string>
#include <utility>
#include <vector>
#include <cryptopp/hmac.h>
#include <cryptopp/sha.h>

// have_api DLL check, challenge v2. The server sends a nonce, random regions of the official DLL's code (RVAs inside
// its executable section) and random game addresses where the DLL installs its hooks. The DLL answers
//   proof = HMAC-SHA256(key = SHA-256 of its file,
//                        dllProofLabel + nonce + SHA-256 of each region read from its own memory (relocated slots
//                        zeroed: the load address does not matter) + the first byte of each game address)
// and the server computes the same from the allowed file (game bytes: 0xE9, the hooks' jmp). An answer needs the
// official file, the running code (unpatched) and the hooks in the game: a forged DLL must reproduce all of it.
namespace Common::HaveApi
{
	using Sha256 = std::array<std::uint8_t, 32>;
	inline constexpr char dllProofLabel[] = "have_api dll check v2";
	inline constexpr std::uint8_t hookFirstByte = 0xE9; // SafetyHook's mid hooks start with jmp rel32 (x86)

	// Game addresses the DLL hooks at start-up on every client (hooks/HookTable.cpp, long-standing core hooks).
	inline constexpr std::array<std::uint32_t, 24> dllHookSites = {
		0x595F2A, 0x5A6152, 0x5A0DA2, 0x5A559A, 0x59BB12, 0x646963, 0x649743, 0x653D45,
		0xE312B4, 0xB3483D, 0xAC0D56, 0x4CFE24, 0xB738A9, 0xB7398B, 0x418480, 0x418A50,
		0x518120, 0x787CF6, 0x787D51, 0x787DBD, 0x78BCB5, 0xA10013, 0xA105A1, 0xB992C3,
	};

	struct DllRegion
	{
		std::uint32_t rva{};
		std::uint32_t size{};
	};

	[[nodiscard]] inline Sha256 sha256(const std::uint8_t* data, std::size_t size)
	{
		Sha256 digest{};
		CryptoPP::SHA256().CalculateDigest(digest.data(), data, size);
		return digest;
	}

	[[nodiscard]] inline Sha256 sha256(const std::vector<std::uint8_t>& data)
	{
		return sha256(data.data(), data.size());
	}

	// The DLL file mapped as the loader maps it (sections at their RVAs), its relocated slots and its code ranges.
	struct DllImage
	{
		Sha256 fileHash{};
		std::vector<std::uint8_t> image;
		std::vector<std::uint32_t> relocations; // RVAs of the 4-byte slots the loader rewrites (sorted)
		std::vector<DllRegion> code;            // executable sections (raw part)

		// SHA-256 of [rva, rva + size) with the relocated slots zeroed; nullopt outside the code.
		[[nodiscard]] std::optional<Sha256> regionDigest(const DllRegion& region) const
		{
			const bool inCode = std::ranges::any_of(code, [&](const DllRegion& c)
				{ return region.rva >= c.rva && region.size > 0 && region.rva + region.size <= c.rva + c.size; });
			if (!inCode) return std::nullopt;
			std::vector<std::uint8_t> bytes(image.begin() + region.rva, image.begin() + region.rva + region.size);
			auto first = std::ranges::lower_bound(relocations, region.rva >= 3 ? region.rva - 3 : 0);
			for (auto it = first; it != relocations.end() && *it < region.rva + region.size; ++it)
			{
				for (std::uint32_t k = 0; k < 4; ++k)
				{
					const std::uint32_t at = *it + k;
					if (at >= region.rva && at < region.rva + region.size) bytes[at - region.rva] = 0;
				}
			}
			return sha256(bytes);
		}

		[[nodiscard]] static std::optional<DllImage> load(const std::string& path)
		{
			std::ifstream file(path, std::ios::binary);
			if (!file) return std::nullopt;
			const std::vector<std::uint8_t> raw{ std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
			auto u16 = [&](std::size_t at) { std::uint16_t v{}; if (at + 2 <= raw.size()) std::memcpy(&v, raw.data() + at, 2); return v; };
			auto u32 = [&](std::size_t at) { std::uint32_t v{}; if (at + 4 <= raw.size()) std::memcpy(&v, raw.data() + at, 4); return v; };
			if (raw.size() < 0x40 || u16(0) != 0x5A4D) return std::nullopt;
			const std::size_t nt = u32(0x3C);
			if (u32(nt) != 0x00004550 || u16(nt + 24) != 0x10B) return std::nullopt; // PE32 (the DLL is x86)
			DllImage dll;
			dll.fileHash = sha256(raw);
			const std::size_t optional = nt + 24;
			const std::uint32_t imageSize = u32(optional + 56);
			const std::uint32_t headersSize = u32(optional + 60);
			const std::uint32_t relocRva = u32(optional + 96 + 5 * 8);
			const std::uint32_t relocSize = u32(optional + 96 + 5 * 8 + 4);
			dll.image.assign(imageSize, 0);
			std::copy_n(raw.begin(), std::min<std::size_t>({ headersSize, raw.size(), imageSize }), dll.image.begin());
			const std::size_t sections = optional + u16(nt + 20);
			for (std::uint16_t i = 0; i < u16(nt + 6); ++i)
			{
				const std::size_t s = sections + 40 * i;
				const std::uint32_t virtualSize = u32(s + 8), rva = u32(s + 12), rawSize = u32(s + 16), rawAt = u32(s + 20);
				const std::uint32_t size = std::min(virtualSize ? virtualSize : rawSize, rawSize);
				if (rva + size > imageSize || rawAt + size > raw.size()) return std::nullopt;
				std::copy_n(raw.begin() + rawAt, size, dll.image.begin() + rva);
				if (u32(s + 36) & 0x20000000) dll.code.push_back({ rva, size }); // IMAGE_SCN_MEM_EXECUTE
			}
			for (std::uint32_t at = relocRva; relocSize && at + 8 <= relocRva + relocSize && at + 8 <= imageSize;)
			{
				std::uint32_t page{}, blockSize{};
				std::memcpy(&page, dll.image.data() + at, 4);
				std::memcpy(&blockSize, dll.image.data() + at + 4, 4);
				if (blockSize < 8) break;
				for (std::uint32_t e = at + 8; e + 2 <= at + blockSize; e += 2)
				{
					std::uint16_t entry{};
					std::memcpy(&entry, dll.image.data() + e, 2);
					if ((entry >> 12) == 3) dll.relocations.push_back(page + (entry & 0xFFF)); // IMAGE_REL_BASED_HIGHLOW
				}
				at += blockSize;
			}
			std::ranges::sort(dll.relocations);
			if (dll.code.empty()) return std::nullopt;
			return dll;
		}
	};

	struct DllChallenge
	{
		std::uint32_t nonce{};
		std::vector<DllRegion> regions;
		std::vector<std::uint32_t> probes;
	};

	// 4 regions of 256..2048 bytes inside the code of every allowed build (the server does not know which one the
	// player has: the answer is checked against each), 6 hook sites
	template<typename Random>
	[[nodiscard]] DllChallenge makeChallenge(const std::vector<DllImage>& dlls, Random& random, std::uint32_t nonce)
	{
		DllChallenge challenge{ nonce, {}, {} };
		std::uint32_t begin = 0, end = UINT32_MAX;
		for (const DllImage& dll : dlls)
		{
			begin = std::max(begin, dll.code.front().rva);
			end = std::min(end, dll.code.front().rva + dll.code.front().size);
		}
		std::uniform_int_distribution<std::uint32_t> pickSize(256, 2048);
		for (int i = 0; !dlls.empty() && end > begin + 256 && i < 4; ++i)
		{
			const std::uint32_t size = std::min(pickSize(random), end - begin);
			std::uniform_int_distribution<std::uint32_t> pickStart(begin, end - size);
			challenge.regions.push_back({ pickStart(random), size });
		}
		std::vector<std::uint32_t> sites(dllHookSites.begin(), dllHookSites.end());
		std::ranges::shuffle(sites, random);
		challenge.probes.assign(sites.begin(), sites.begin() + 6);
		return challenge;
	}

	// The proof; regionDigests / probeBytes in the challenge's order.
	[[nodiscard]] inline Sha256 dllProof(const Sha256& fileHash, std::uint32_t nonce, const std::vector<Sha256>& regionDigests,
		const std::vector<std::uint8_t>& probeBytes)
	{
		std::vector<std::uint8_t> message(std::begin(dllProofLabel), std::end(dllProofLabel) - 1);
		for (int i = 0; i < 4; ++i) message.push_back(static_cast<std::uint8_t>(nonce >> (8 * i)));
		for (const Sha256& digest : regionDigests) message.insert(message.end(), digest.begin(), digest.end());
		message.insert(message.end(), probeBytes.begin(), probeBytes.end());
		Sha256 mac{};
		CryptoPP::HMAC<CryptoPP::SHA256> hmac(fileHash.data(), fileHash.size());
		hmac.CalculateDigest(mac.data(), message.data(), message.size());
		return mac;
	}

	// What an allowed DLL answers to `challenge`.
	[[nodiscard]] inline std::optional<Sha256> expectedProof(const DllImage& dll, const DllChallenge& challenge)
	{
		std::vector<Sha256> digests;
		for (const DllRegion& region : challenge.regions)
		{
			const auto digest = dll.regionDigest(region);
			if (!digest) return std::nullopt;
			digests.push_back(*digest);
		}
		return dllProof(dll.fileHash, challenge.nonce, digests, std::vector<std::uint8_t>(challenge.probes.size(), hookFirstByte));
	}
}

#endif
