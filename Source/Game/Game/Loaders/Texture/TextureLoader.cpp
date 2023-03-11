#include "Game/Loaders/LoaderSystem.h"
#include "Game/Util/ServiceLocator.h"
#include "Game/ECS/Singletons/TextureSingleton.h"
#include "Game/Application/EnttRegistries.h"

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

class TextureLoader : Loader
{
public:
    TextureLoader() : Loader("TextureLoader", 10000) { }

    bool Init()
    {
        EnttRegistries* registries = ServiceLocator::GetEnttRegistries();

        entt::registry* registry = registries->gameRegistry;

        entt::registry::context& ctx = registry->ctx();
        ECS::Singletons::TextureSingleton& textureSingleton = ctx.emplace<ECS::Singletons::TextureSingleton>();

        static const fs::path fileExtension = ".dds";
        fs::path relativeParentPath = "Data/Texture";
        fs::path absolutePath = std::filesystem::absolute(relativeParentPath).make_preferred();
        fs::create_directories(absolutePath);

        // Note : This is used because it is more performant than doing fs::relative by up to 3x on 140k textures
        std::string absolutePathStr = absolutePath.string();
        size_t subStrIndex = absolutePathStr.length() + 1; // + 1 here for folder seperator

        moodycamel::ConcurrentQueue<TexturePair> texturePairs;

        std::vector<std::filesystem::path> paths;
        std::filesystem::recursive_directory_iterator dirpos{ absolutePath };
        std::copy(begin(dirpos), end(dirpos), std::back_inserter(paths));

        std::for_each(std::execution::par, std::begin(paths), std::end(paths), [&subStrIndex, &texturePairs](const std::filesystem::path& path)
        {
            if (!path.has_extension() || path.extension().compare(fileExtension) != 0)
                return;

            std::string texturePath = path.string().substr(subStrIndex);

            TexturePair texturePair;
            texturePair.path = texturePath;
            texturePair.hash = StringUtils::fnv1a_32(texturePath.c_str(), texturePath.length());

            texturePairs.enqueue(texturePair);
        });

        textureSingleton.textureHashToPath.reserve(texturePairs.size_approx());
        textureSingleton.textureHashToTextureID.reserve(texturePairs.size_approx());

        TexturePair texturePair;
        while (texturePairs.try_dequeue(texturePair))
        {
            auto itr = textureSingleton.textureHashToPath.find(texturePair.hash);
            if (itr != textureSingleton.textureHashToPath.end())
            {
                DebugHandler::PrintError("Found duplicate texture hash ({0}) for Path ({1})", texturePair.hash, texturePair.path.c_str()); // This error cannot be more specific when loading in parallel unless we copy more data.
            }

            textureSingleton.textureHashToPath[texturePair.hash] = (relativeParentPath / texturePair.path).string();
        }

        DebugHandler::Print("Loaded Texture {0} entries", textureSingleton.textureHashToPath.size());

        return true;
    }
};

TextureLoader textureLoader;