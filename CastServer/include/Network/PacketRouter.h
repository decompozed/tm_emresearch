#ifndef CAST_NETWORK_PACKET_ROUTER_H
#define CAST_NETWORK_PACKET_ROUTER_H

#include <cstdint>
#include <functional>
#include <initializer_list>
#include <memory>
#include "Network/Session.h"

namespace Cast::Network
{
	template<class SessionT>
	class PacketRouter
	{
	public:
		using Packet = Common::Network::UnecryptedPacket;
		using Handler = std::function<void(const Packet&, std::shared_ptr<SessionT>)>;

		void on(std::uint16_t order, Handler handler)
		{
			Common::Network::Session::addCallback<Common::Network::PacketType::UNECRYPTED, SessionT>(order, std::move(handler));
		}

		void on(std::initializer_list<std::uint16_t> orders, const Handler& handler)
		{
			for (const std::uint16_t order : orders)
			{
				on(order, handler);
			}
		}
	};
}

#endif
