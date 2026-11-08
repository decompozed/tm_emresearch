#ifndef COMMON_NETWORK_IPC_SECURITY_H
#define COMMON_NETWORK_IPC_SECURITY_H

namespace Common::Network::IpcTls { class Context; }

namespace Common::Network::IpcSecurity
{
	bool initialize();

	bool required();

	IpcTls::Context* context();
}

#endif
