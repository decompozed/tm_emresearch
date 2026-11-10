#ifndef MAIN_NETWORK_TURNSTILE_H
#define MAIN_NETWORK_TURNSTILE_H

#include <string>

namespace Main
{
	namespace Network
	{
		namespace Turnstile
		{
			enum class Verdict
			{
				Passed,      // Cloudflare says the token is valid
				Rejected,    // Cloudflare says it is not (or the token is empty or too long)
				Unreachable  // no usable answer: treated as a failure by the caller, the player can try again
			};

			// HTTPS POST of secret, response and remoteip to the Cloudflare siteverify endpoint. Blocking, with a short timeout.
			// The secret and the token are never logged.
			Verdict verify(const std::string& secret, const std::string& token, const std::string& remoteIp);
		}
	}
}

#endif
