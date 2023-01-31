#pragma once
#include <memory>

namespace Network
{
	class Client;
	class PacketHandler;
}

namespace ECS::Singletons
{
	struct NetworkState
	{
	public:
		std::unique_ptr<Network::Client> client;
		std::unique_ptr<Network::PacketHandler> packetHandler;
	};
}