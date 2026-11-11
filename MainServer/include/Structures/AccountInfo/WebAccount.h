#ifndef MAIN_WEB_ACCOUNT_H
#define MAIN_WEB_ACCOUNT_H

#include <cstdint>
#include <string>

namespace Main
{
	namespace Structures
	{
		// What the website's account API needs from the Users table
		struct WebAccount
		{
			std::uint32_t accountId{};
			std::string username;
			std::string nickname;
			std::string passwordHash;
			std::string encryptedSecret;
			std::string suspendedUntil;
			std::int32_t grade{};
			bool twoFactorEnabled{};
			std::uint64_t totpLastStep{};
			std::uint64_t twoFactorLockedUntil{};
			std::int64_t level{};
			std::int64_t kills{};
			std::int64_t deaths{};
			std::int64_t wins{};
			std::int64_t loses{};
			std::int64_t playtime{};
			std::uint32_t webSessionVersion{}; // moves on at logout and password change, website tokens carry it as "sv"
		};

		struct WebAccountLookup
		{
			enum class Status { Found, NotFound, Error };
			Status status{ Status::Error };
			WebAccount account;
		};

		enum class WebRegisterResult { Created, UsernameTaken, NicknameTaken, Error };
	}
}

#endif
