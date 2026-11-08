// libFuzzer target for the code that parses bytes sent by a client.
// clang++ -fsanitize=fuzzer,address,undefined ... see the fuzz job in .github/workflows/ci.yml
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <span>

#include "HaveApi/CastConnect.h"
#include "HaveApi/MessageReader.h"
#include "Protocol/TcpHeader.h"

namespace
{
	struct LargestProbe
	{
		std::uint8_t bytes[256];
	};
}

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
	const std::span<const std::uint8_t> input{ data, size };

	if (const auto message = Common::HaveApi::ReceivedMessage::parse(input))
	{
		(void)message->id();
		const auto payload = message->payload();
		// The payload must stay inside what was received
		if (payload.size() > size || (payload.size() != 0 && (payload.data() < data || payload.data() + payload.size() > data + size)))
		{
			std::abort();
		}
		(void)message->payloadAs<LargestProbe>();
		(void)message->payloadAs<std::uint32_t>();
	}

	if (size >= sizeof(std::uint32_t))
	{
		std::uint32_t raw = 0;
		std::memcpy(&raw, data, sizeof(raw));
		const Common::Protocol::TcpHeader header{ raw };
		// A header that is accepted must have a size the session code can work with
		if (header.isValidMain() && (header.getSize() < 4 || header.getSize() > Common::Constants::maxPacketBytes))
		{
			std::abort();
		}
		(void)header.isValidCast();
	}

	(void)Common::HaveApi::decodeExact<Common::HaveApi::CastConnectCheck>(data, size);
	(void)Common::HaveApi::decodeExact<Common::HaveApi::CastConnectAnswer>(data, size);
	return 0;
}
