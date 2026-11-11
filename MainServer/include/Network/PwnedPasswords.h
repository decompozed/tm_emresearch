#ifndef MAIN_NETWORK_PWNED_PASSWORDS_H
#define MAIN_NETWORK_PWNED_PASSWORDS_H

#include <string>

namespace Main
{
	namespace Network
	{
		namespace PwnedPasswords
		{
			enum class Verdict
			{
				NotFound,    // the password is not in the breach corpus
				Breached,    // it is: the account API refuses it
				Unavailable  // no usable answer: the caller lets the password through and the warning is logged
			};

			// HTTPS GET of the range for the first 5 characters of the SHA-1, with padding so the answer size does not tell the
			// prefix either. Blocking, short timeout. Neither the password nor its hash is ever logged.
			Verdict check(const std::string& password);
		}
	}
}

#endif
