#pragma once
#include <Base/Types.h>
#include <Base/Container/StringTable.h>

namespace ECS::Singletons
{
	struct TextureSingleton
	{
	public:
		TextureSingleton() {}

		std::unordered_map<u32, std::string> textureHashToPath;
		std::unordered_map<u32, u32> textureHashToTextureID;
	};
}