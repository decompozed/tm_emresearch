/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */


#include "../include/AuthService.h"
#include <string>
#include "Utils.h"
#include "Utils/SetupParser.h"
#include "Utils/Totp.h"
#include "AuthUtils.h"
#include <libcppotp/auth.h>
#include <openssl/rand.h>
#include <cryptopp/osrng.h>
#include <cryptopp/hex.h>
#include <libbcrypt/include/bcrypt/BCrypt.hpp>

namespace Auth
{
	using namespace Common::Utils;
	namespace Totp = Common::Utils::Totp;

	bool AuthService::validatePassword(const std::string& plain, std::string hash)
	{
		if (BCrypt::validatePassword(plain, hash))
			return true;

		if (hash.substr(0, 4) == "$2b$")
		{
			std::string hash_2a = hash;
			hash_2a[2] = 'a';
			return BCrypt::validatePassword(plain, hash_2a);
		}

		return false;
	}

	Auth::Enums::Login AuthService::authorizeGraded(const Auth::Structures::BasicAccountInfo& ainfo, const std::optional<std::string>& token,
		const std::string& plainPw, const std::string& plainIp, const std::string& plainHwid, std::uint_least16_t port,
		std::shared_ptr<Common::Network::Session> session)
	{
		auto& authSetup = Common::Utils::SetupParser::getInstance().getAuthSetup();
		
		if (ainfo.secret.empty())
		{
			m_persistentDatabase.logGameEvent("AuthGradedLogin",
				"Failed login: missing mandatory secret for graded account " + std::to_string(ainfo.ainfoClient.accountId), "MEDIUM");
			return Auth::Enums::Login::INCORRECT;
		}

		if (authSetup.enhancedSecurity)
		{
			if (port != authSetup.gradedPort)
			{
				m_emailDispatcher.sendAlertAsync("[MEDIUM Alert] TB - Graded Login Wrong Port",
					"High Level Alert: Graded Account(ID: " + std::to_string(ainfo.ainfoClient.accountId) + ") accessed with the wrong port, access was blocked",

					[accountId = ainfo.ainfoClient.accountId, this]() {
						m_persistentDatabase.logGameEvent("EmailGraded",
						"Failed to send admin notification after wrong port at login for graded accountID: " + std::to_string(accountId), "HIGH");
					}
				);

				m_persistentDatabase.logGameEvent("AuthGradedLogin",
					"Failed login: Graded account accessed from invalid auth port, accountID: " + std::to_string(ainfo.ainfoClient.accountId), "HIGH");
				return Auth::Enums::Login::INCORRECT;
			}

			if (plainHwid.empty())
			{
				m_persistentDatabase.logGameEvent("AuthGradedLogin",
					"Failed login: missing HWID for graded account " + std::to_string(ainfo.ainfoClient.accountId), "MEDIUM");
				return Auth::Enums::Login::INCORRECT;
			}

			std::string dbGradedHash, dbGradedSalt;
			if (!m_persistentDatabase.getGradedHwid(ainfo.ainfoClient.accountId, dbGradedHash, dbGradedSalt))
			{
				m_persistentDatabase.logGameEvent("AuthGradedLogin",
					"Failed login: unable to retrieve graded HWID from DB for graded account " + std::to_string(ainfo.ainfoClient.accountId), "MEDIUM");
				return Auth::Enums::Login::INCORRECT;
			}

			if (dbGradedHash.empty() || dbGradedSalt.empty())
			{
				// Register first-time HWID
				dbGradedSalt = generateRandomSalt();
				dbGradedHash = Common::Utils::hashSha256(plainHwid, dbGradedSalt);
				if (!m_persistentDatabase.setGradedHwid(ainfo.ainfoClient.accountId, dbGradedHash, dbGradedSalt))
				{
					m_persistentDatabase.logGameEvent("AuthGradedLogin",
						"Failed login: could not set first-time graded HWID for account " + std::to_string(ainfo.ainfoClient.accountId), "LOW");
					return Auth::Enums::Login::INCORRECT;
				}
				m_persistentDatabase.logGameEvent("AuthGradedLogin",
					"Registered first-time graded HWID for account " + std::to_string(ainfo.ainfoClient.accountId), "LOW");
			}
			else
			{
				if (Common::Utils::hashSha256(plainHwid, dbGradedSalt) != dbGradedHash)
				{
					m_emailDispatcher.sendAlertAsync("[HIGH Alert] TB - Graded Login Wrong HWID",
						"High Level Alert: Graded Account(ID: " + std::to_string(ainfo.ainfoClient.accountId) + ") accessed with the wrong HWID, access was blocked",
						[accountId = ainfo.ainfoClient.accountId, this]() {
							m_persistentDatabase.logGameEvent("EmailGraded", "Failed to send admin notification after wrong HWID at login for graded accountID: "
								+ std::to_string(accountId), "HIGH");
						}
					);

					m_persistentDatabase.logGameEvent("AuthGradedLogin",
						"Failed login: HWID mismatch for graded account " + std::to_string(ainfo.ainfoClient.accountId), "HIGH");
					return Auth::Enums::Login::INCORRECT;
				}
			}

			// Update current HWID
			std::string currentSalt = generateRandomSalt();
			std::string currentHwidHash = Common::Utils::hashSha256(plainHwid, currentSalt);
			if (!m_persistentDatabase.updateCurrentHwid(ainfo.ainfoClient.accountId, currentHwidHash, currentSalt))
			{
				m_persistentDatabase.logGameEvent("AuthGradedLogin",
					"Failed login: could not update current HWID for graded account " + std::to_string(ainfo.ainfoClient.accountId), "LOW");
				return Auth::Enums::Login::INCORRECT;
			}

			// Update IP for main server
			session->setIpFromGraded();
		}

		const bool passwordOk = validatePassword(plainPw, ainfo.hashedPassword);
		// Only a correct password may spend a code, so someone without it cannot burn the owner's current code
		const SecondFactor secondFactor = passwordOk ? verifySecondFactor(ainfo, token) : SecondFactor::Invalid;

		std::optional<std::uint32_t> wrongPasswords{ 0 };
		std::optional<std::uint32_t> wrong2fas{ 0 };
		if (!passwordOk)
		{
			wrongPasswords = m_persistentDatabase.incrementFailedPasswords(ainfo.ainfoClient.accountId);
			m_persistentDatabase.logGameEvent("AuthGradedLogin",
				"Failed login: incorrect password for account " + std::to_string(ainfo.ainfoClient.accountId), "MEDIUM");
		}
		else if (secondFactor == SecondFactor::Invalid)
		{
			wrong2fas = m_persistentDatabase.incrementFailed2fas(ainfo.ainfoClient.accountId);
			m_persistentDatabase.logGameEvent("AuthGradedLogin",
				"Failed login: invalid 2FA token for account " + std::to_string(ainfo.ainfoClient.accountId), "MEDIUM");
		}
		else if (secondFactor == SecondFactor::Replayed)
		{
			// Not a strike: a replayed code reveals nothing to a guesser, and the owner can hit this by reconnecting within 30 s
			m_persistentDatabase.logGameEvent("AuthGradedLogin",
				"Failed login: already used 2FA token replayed for account " + std::to_string(ainfo.ainfoClient.accountId), "HIGH");
		}

		if (!wrongPasswords || !wrong2fas)
		{
			return Auth::Enums::Login::INCORRECT; // the attempt could not be counted: fail closed
		}

		constexpr std::uint32_t MAX_FAILED_ATTEMPTS = 3;
		if (*wrongPasswords >= MAX_FAILED_ATTEMPTS || *wrong2fas >= MAX_FAILED_ATTEMPTS)
		{
			if (!tryLockAccount(ainfo.ainfoClient.accountId, true))
			{
				m_persistentDatabase.logGameEvent("AuthGradedLogin",
					"Account lock attempt failed for account " + std::to_string(ainfo.ainfoClient.accountId), "CRITICAL");

				m_emailDispatcher.sendAlertAsync("[CRITICAL Alert] TB - Graded Account LOCK FAIL!",
					"Critical Level Alert: Graded Account(ID: " + std::to_string(ainfo.ainfoClient.accountId) + 
					") could NOT BE LOCKED after too many wrong login attempts, lock the account manually!",
					[accountId = ainfo.ainfoClient.accountId, this]() {
						m_persistentDatabase.logGameEvent("EmailGraded",
							"Failed to send admin notification after not being able to lock graded account after too many failed login attempts, accountID: "
							+ std::to_string(accountId), "HIGH");
					}
				);

				return Auth::Enums::Login::INCORRECT;
			}

			m_emailDispatcher.sendAlertAsync("[CRITICAL Alert] TB - Graded Account LOCKED!",
				"Critical Level Alert: Graded Account(ID: " + std::to_string(ainfo.ainfoClient.accountId) +
				") was locked due to too many wrong login attempts, see logs in the database",
				[accountId = ainfo.ainfoClient.accountId, this]() {
					m_persistentDatabase.logGameEvent("EmailGraded",
						"Failed to send admin notification after successfully locking graded account after too many failed login attempts, accountID: "
						+ std::to_string(accountId),"HIGH");
				}
			);

			m_persistentDatabase.logGameEvent("AuthGradedLogin",
				"Account locked due to repeated failed login attempts: " + std::to_string(ainfo.ainfoClient.accountId), "CRITICAL");

			m_persistentDatabase.resetFailedLogins(ainfo.ainfoClient.accountId);
			return Auth::Enums::Login::INCORRECT;
		}

		if (!passwordOk || secondFactor != SecondFactor::Valid)
		{
			return Auth::Enums::Login::INCORRECT;
		}

		m_persistentDatabase.resetFailedLogins(ainfo.ainfoClient.accountId);

		if (isSuspended(ainfo))
		{
			m_persistentDatabase.logGameEvent("AuthGradedLogin","Login attempt on suspended account " + std::to_string(ainfo.ainfoClient.accountId),"MEDIUM");
			return Auth::Enums::Login::SUSPENDED;
		}

		return Auth::Enums::Login::SUCCESS;
	}

	bool AuthService::isSuspended(const Auth::Structures::BasicAccountInfo& ainfo) const
	{
		const std::uint64_t currentEpoch = static_cast<std::uint64_t>(std::time(nullptr));
		return Common::Utils::datetimeToEpoch(ainfo.suspendedUntil) > currentEpoch;
	}

	// Accounts that turned on 2FA themselves. Unlike staff they are locked out for a short time instead of being demoted.
	Auth::Enums::Login AuthService::authorizeWithSecondFactor(const Auth::Structures::BasicAccountInfo& ainfo, const std::optional<std::string>& token, const std::string& plainPw)
	{
		const std::uint32_t accountId = ainfo.ainfoClient.accountId;
		if (!validatePassword(plainPw, ainfo.hashedPassword))
		{
			return Auth::Enums::Login::INCORRECT;
		}

		const std::uint64_t now = static_cast<std::uint64_t>(std::time(nullptr));
		if (ainfo.twoFactorLockedUntil > now)
		{
			m_persistentDatabase.logGameEvent("AuthLogin2fa", "Failed login: 2FA temporarily locked for account " + std::to_string(accountId), "MEDIUM");
			return Auth::Enums::Login::INCORRECT;
		}

		const SecondFactor secondFactor = verifySecondFactor(ainfo, token);
		if (secondFactor == SecondFactor::Replayed)
		{
			m_persistentDatabase.logGameEvent("AuthLogin2fa", "Failed login: already used 2FA token replayed for account " + std::to_string(accountId), "HIGH");
			return Auth::Enums::Login::INCORRECT;
		}

		if (secondFactor == SecondFactor::Invalid)
		{
			m_persistentDatabase.logGameEvent("AuthLogin2fa", "Failed login: invalid 2FA token for account " + std::to_string(accountId), "MEDIUM");
			const auto wrong2fas = m_persistentDatabase.incrementFailed2fas(accountId);
			if (wrong2fas && *wrong2fas >= Totp::maxWrongCodesPlayer)
			{
				m_persistentDatabase.lockTwoFactor(accountId, now + Totp::playerLockSeconds);
				m_persistentDatabase.logGameEvent("AuthLogin2fa", "2FA locked for 15 minutes after repeated wrong codes, account " + std::to_string(accountId), "HIGH");
			}
			return Auth::Enums::Login::INCORRECT;
		}

		m_persistentDatabase.resetFailedLogins(accountId);

		if (isSuspended(ainfo))
		{
			m_persistentDatabase.logGameEvent("AuthLogin2fa", "Login attempt on suspended account " + std::to_string(accountId), "LOW");
			return Auth::Enums::Login::SUSPENDED;
		}
		return Auth::Enums::Login::SUCCESS;
	}


	Auth::Enums::Login AuthService::authorizeUngraded(const Auth::Structures::BasicAccountInfo& ainfo,const std::optional<std::string>& token,const std::string& plainPw)
	{	
		if (!validatePassword(plainPw, ainfo.hashedPassword))
		{
			return Auth::Enums::Login::INCORRECT;
		}

		std::uint64_t suspendedUntilEpoch = Common::Utils::datetimeToEpoch(ainfo.suspendedUntil);
		std::uint64_t currentEpoch = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
		if (suspendedUntilEpoch > currentEpoch)
		{
			m_persistentDatabase.logGameEvent("AuthUngradedLogin","Login attempt on suspended ungraded account " + std::to_string(ainfo.ainfoClient.accountId),"LOW");
			return Auth::Enums::Login::SUSPENDED;
		}
		return Auth::Enums::Login::SUCCESS;
	}

	std::string AuthService::generateRandomSalt(std::size_t length) const
	{
		CryptoPP::AutoSeededRandomPool rng;
		std::string saltBytes(length, 0);
		rng.GenerateBlock(reinterpret_cast<CryptoPP::byte*>(&saltBytes[0]), saltBytes.size());

		std::string hex;
		CryptoPP::HexEncoder encoder;
		encoder.Attach(new CryptoPP::StringSink(hex));
		encoder.Put(reinterpret_cast<const CryptoPP::byte*>(saltBytes.data()), saltBytes.size());
		encoder.MessageEnd();

		return hex; 
	}

	bool AuthService::tryLockAccount(std::uint32_t accountId, bool isGraded) 
	{
		constexpr int MAX_RETRIES = 3;
		bool locked = false;
		for (int i = 0; i < MAX_RETRIES; ++i)
		{
			locked = m_persistentDatabase.removeGradeAndSuspend(accountId, isGraded);
			if (locked) break;
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}

		return locked;
	}

	std::uint32_t AuthService::generateAccountKey() const
	{
		std::uint32_t value;
		if (RAND_bytes(reinterpret_cast<unsigned char*>(&value), sizeof(value)) != 1) {
			// entropy source not available => no cryptographically secure random bytes generated
			return 0; // account key 0 is default, main server won't accept it
		}
		return value;
	}

	Auth::Structures::Parsed2faLogin AuthService::parseUsernameAnd2FA(const std::string& rawUsername) const
	{
		Auth::Structures::Parsed2faLogin res{};
		const auto pos = rawUsername.find('+');

		if (pos == std::string::npos)
		{
			res.username = rawUsername;
			return res;
		}

		res.username = rawUsername.substr(0, pos);
		res.token = Totp::toUpper(rawUsername.substr(pos + 1));

		if (!Totp::isTotpCode(*res.token))
		{
			res.token->clear();
		}

		return res;
	}

	AuthService::SecondFactor AuthService::verifySecondFactor(const Auth::Structures::BasicAccountInfo& ainfo, const std::optional<std::string>& token)
	{
		if (ainfo.secret.empty() || !token.has_value() || token->empty())
		{
			return SecondFactor::Invalid;
		}

		const std::uint32_t accountId = ainfo.ainfoClient.accountId;
		const auto secret = Common::Utils::decrypt2FASecret(ainfo.secret, Common::Utils::SetupParser::getInstance().getGeneralSetup().twoFaSecret);
		if (!secret)
		{
			m_persistentDatabase.logGameEvent("AuthLogin2fa", "Stored 2FA secret could not be decrypted for account " + std::to_string(accountId), "HIGH");
			return SecondFactor::Invalid;
		}

		const Totp::MatchResult match = Totp::matchCode(*secret, *token, std::time(nullptr), ainfo.totpLastStep);
		if (match.status == Totp::MatchStatus::Invalid)
		{
			return SecondFactor::Invalid;
		}

		if (match.status == Totp::MatchStatus::Replayed || !m_persistentDatabase.consumeTotpStep(accountId, match.step))
		{
			return SecondFactor::Replayed;
		}
		return SecondFactor::Valid;
	}

	std::expected<Auth::Structures::BasicAccountInfo, Auth::Enums::Login> 
		AuthService::login(const std::string& username, const std::string& password, const std::string& plainIp, std::uint_least16_t port, const std::string& plainHwid,
			std::shared_ptr<Common::Network::Session> session)
	{
		const auto parsed = parseUsernameAnd2FA(username);
		const auto result = m_persistentDatabase.getCompletePlayerInfo(parsed.username);
		if (!result) return std::unexpected(result.error());

		Auth::Structures::BasicAccountInfo userInfo = result.value();
		const bool enhancedSecurity = Common::Utils::SetupParser::getInstance().getAuthSetup().enhancedSecurity;
		Auth::Enums::Login authResult = Auth::Enums::Login::INCORRECT;
		if (enhancedSecurity && userInfo.grade >= 3)
		{
			authResult = authorizeGraded(userInfo, parsed.token, password, plainIp, plainHwid, port, session);
		}
		else if (userInfo.twoFactorEnabled)
		{
			authResult = authorizeWithSecondFactor(userInfo, parsed.token, password);
		}
		else
		{
			authResult = authorizeUngraded(userInfo, parsed.token, password);
		}

		if (authResult != Auth::Enums::SUCCESS)
		{
			m_persistentDatabase.addHash(userInfo.ainfoClient.accountId, 0); // AccountKey=0 signals failure to MainServer
			return std::unexpected(authResult);
		}

		userInfo.ainfoClient.hashKey = generateAccountKey();
		m_persistentDatabase.addHash(userInfo.ainfoClient.accountId, userInfo.ainfoClient.hashKey);

		std::string ipSalt = generateRandomSalt();
		if (!m_persistentDatabase.updateLastLoggedNow(userInfo.ainfoClient.accountId, Common::Utils::hashSha256(plainIp, ipSalt), ipSalt))
		{
			::Utils::Logger::log("Failed login: could not update LastLogged for account " + std::to_string(userInfo.ainfoClient.accountId),
				::Utils::LogType::Error, "AuthSession::onPacket");
			return std::unexpected(Auth::Enums::Login::INCORRECT);
		}

		return userInfo;
	}
}