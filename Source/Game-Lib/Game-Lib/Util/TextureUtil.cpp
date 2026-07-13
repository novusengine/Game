#include "TextureUtil.h"

#include "Game-Lib/Application/EnttRegistries.h"
#include "Game-Lib/ECS/Singletons/Database/TextureSingleton.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Base/Util/DebugHandler.h>
#include <Base/Util/StringUtils.h>
#include <Base/Container/ConcurrentQueue.h>

#include <entt/entt.hpp>

#include <xxhash/xxhash64.h>

#include <execution>
#include <filesystem>
namespace fs = std::filesystem;

namespace Util::Texture
{
    void DiscoverAll()
    {
        EnttRegistries* registries = ServiceLocator::GetEnttRegistries();

        entt::registry* registry = registries->dbRegistry;
        entt::registry::context& ctx = registry->ctx();

        auto& textureSingleton = ctx.emplace<ECS::Singletons::TextureSingleton>();
    }
}
