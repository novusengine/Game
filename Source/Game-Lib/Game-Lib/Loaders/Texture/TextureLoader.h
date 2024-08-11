#pragma once
#include "Game/Application/EnttRegistries.h"
#include "Game/ECS/Singletons/TextureSingleton.h"
#include "Game/Util/ServiceLocator.h"

#include <Base/Util/DebugHandler.h>
#include <Base/Container/ConcurrentQueue.h>

#include <entt/entt.hpp>

#include <execution>
#include <filesystem>
namespace fs = std::filesystem;

struct TexturePair
{
    u32 hash;
    std::string path;
};

class TextureLoader
{
public:
    TextureLoader() = delete;

    static bool Init()
    {
        NC_LOG_INFO("TextureLoader : Scanning for textures");

        EnttRegistries* registries = ServiceLocator::GetEnttRegistries();

        entt::registry* registry = registries->gameRegistry;
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

        moodycamel::ConcurrentQueue<TexturePair> texturePairs(paths.size());

        std::for_each(std::execution::par, std::begin(paths), std::end(paths), [&subStrIndex, &texturePairs](const std::filesystem::path& path)
        {
            if (!path.has_extension() || path.extension().compare(fileExtension) != 0)
                return;

            std::string texturePath = path.string().substr(subStrIndex);
            std::replace(texturePath.begin(), texturePath.end(), '\\', '/');

            TexturePair texturePair;
            texturePair.path = texturePath;
            texturePair.hash = StringUtils::fnv1a_32(texturePath.c_str(), texturePath.length());

            texturePairs.enqueue(texturePair);
        });

        u32 numTexturePairs = static_cast<u32>(texturePairs.size_approx());

        textureSingleton.textureHashToPath.reserve(numTexturePairs);
        textureSingleton.textureHashToTextureID.reserve(numTexturePairs);

        TexturePair texturePair;
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

        return true;
    }
};