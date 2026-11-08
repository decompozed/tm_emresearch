// libFuzzer target for the code that handles text typed by a player before it reaches a log.
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>

#include "Utils/SecurityEvent.h"
#include "Utils/Totp.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
	const std::string input(reinterpret_cast<const char*>(data), size);

	// A log line must never be split or get a field added by what a player typed
	const std::string escaped = Common::Utils::SecurityEvent::escapeJson(input);
	for (std::size_t i = 0; i < escaped.size(); ++i)
	{
		const unsigned char c = static_cast<unsigned char>(escaped[i]);
		if (c < 0x20 || c == 0x7f)
		{
			std::abort();
		}
		if (c == '\\')
		{
			++i; // the character after a backslash is escaped
			if (i == escaped.size())
			{
				std::abort();
			}
		}
		else if (c == '"')
		{
			std::abort();
		}
	}

	// When a command is cleaned, nothing of what followed the command name may survive
	const std::string cleaned = Common::Utils::Totp::redactCommandForLog(input);
	if (cleaned != input)
	{
		constexpr std::string_view suffix = " [redacted]";
		if (!cleaned.ends_with(suffix) || cleaned.size() > 16 + suffix.size())
		{
			std::abort();
		}
	}
	return 0;
}
