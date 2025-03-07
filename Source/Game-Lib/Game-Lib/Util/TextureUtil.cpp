#include "TextureUtil.h"

#include "Game-Lib/Application/EnttRegistries.h"
#include "Game-Lib/ECS/Singletons/Database/TextureSingleton.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Base/Util/DebugHandler.h>
#include <Base/Util/StringUtils.h>
#include <Base/Container/ConcurrentQueue.h>

#include <entt/entt.hpp>

#include <execution>
#include <filesystem>
namespace fs = std::filesystem;

namespace Util::Texture
{
    struct DiscoveredTexture
    {
        u32 hash;
        std::string path;
    };

    bool IsValidExtension(fs::path extension)
    {
        static const fs::path ddsFileExtension = ".dds";
        static const fs::path pngFileExtension = ".png";
        static const fs::path jpgFileExtension = ".jpg";
        static const fs::path jpegFileExtension = ".jpeg";

        return extension.compare(ddsFileExtension) == 0 || extension.compare(pngFileExtension) == 0 || extension.compare(jpgFileExtension) == 0 || extension.compare(jpegFileExtension) == 0;
    }

    void DiscoverAll()
    {
        NC_LOG_INFO("TextureLoader : Scanning for textures");

        EnttRegistries* registries = ServiceLocator::GetEnttRegistries();

        entt::registry* registry = registries->dbRegistry;
        entt::registry::context& ctx = registry->ctx();

        auto& textureSingleton = ctx.emplace<ECS::Singletons::TextureSingleton>();

        static const fs::path fileExtension = ".dds";
        fs::path relativeParentPath = "Data/Texture";
        fs::path absolutePath = std::filesystem::absolute(relativeParentPath).make_preferred();
        fs::create_directories(absolutePath);

        // Note : This is used because it is more performant than doing fs::relative by up to 3x on 140k textures
        std::string absolutePathStr = absolutePath.string();
        size_t subStrIndex = absolutePathStr.length() + 1; // + 1 here for folder seperator

        std::vector<std::filesystem::path> paths;
        std::filesystem::recursive_directory_iterator dirpos{ absolutePath };
        std::copy(begin(dirpos), end(dirpos), std::back_inserter(paths));

        u32 numPaths = static_cast<u32>(paths.size());
        u32 numToReserve = numPaths * moodycamel::ConcurrentQueue<DiscoveredTexture>::BLOCK_SIZE;
        moodycamel::ConcurrentQueue<DiscoveredTexture> texturePairs(numToReserve);

        std::for_each(std::execution::par, std::begin(paths), std::end(paths), [&subStrIndex, &texturePairs](const std::filesystem::path& path)
        {
            if (!path.has_extension() || !IsValidExtension(path.extension()))
                return;

            std::string texturePath = path.string().substr(subStrIndex);
            std::replace(texturePath.begin(), texturePath.end(), '\\', '/');

            DiscoveredTexture texturePair;
            texturePair.path = texturePath;
            texturePair.hash = StringUtils::fnv1a_32(texturePath.c_str(), texturePath.length());

            texturePairs.enqueue(texturePair);
        });

        u32 numTexturePairs = static_cast<u32>(texturePairs.size_approx());

        textureSingleton.textureHashToPath.reserve(numTexturePairs);

        DiscoveredTexture texturePair;
        while (texturePairs.try_dequeue(texturePair))
        {
            if (textureSingleton.textureHashToPath.contains(texturePair.hash))
            {
                const std::string& path = textureSingleton.textureHashToPath[texturePair.hash];
                NC_LOG_ERROR("Found duplicate texture hash ({0}) for Paths (\"{1}\") - (\"{2}\")", texturePair.hash, path.c_str(), texturePair.path.c_str());
            }

            textureSingleton.textureHashToPath[texturePair.hash] = (relativeParentPath / texturePair.path).string();
        }

        NC_LOG_INFO("Loaded Texture {0} entries", textureSingleton.textureHashToPath.size());
    }
}
