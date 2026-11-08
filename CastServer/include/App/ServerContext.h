#ifndef CAST_APP_SERVER_CONTEXT_H
#define CAST_APP_SERVER_CONTEXT_H

#include <cstdint>

namespace Cast
{
	namespace Domain { class RoomsManager; }
	namespace Network { class SessionsManager; class IpcClient; }

	struct ServerContext
	{
		Domain::RoomsManager& rooms;
		Network::SessionsManager& sessions;
		Network::IpcClient& ipc;
		std::uint16_t serverId;
	};
}

#endif
