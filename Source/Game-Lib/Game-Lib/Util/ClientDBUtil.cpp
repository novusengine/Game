#include "ClientDBUtil.h"

#include "Game-Lib/Application/EnttRegistries.h"
#include "Game-Lib/ECS/Singletons/ClientDBCollection.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Base/Container/ConcurrentQueue.h>
#include <Base/Memory/FileReader.h>
#include <Base/Util/DebugHandler.h>

#include <FileFormat/Novus/ClientDB/ClientDB.h>

#include <entt/entt.hpp>

#include <execution>
#include <filesystem>

namespace fs = std::filesystem;

namespace Util::ClientDB
{
    struct DiscoveredClientDB
    {
    public:
        u32 hash;
        std::string fileName;
        std::string path;
    };

    void DiscoverAll()
    {
        EnttRegistries* registries = ServiceLocator::GetEnttRegistries();

        fs::path relativeParentPath = "Data/ClientDB";
        fs::path absolutePath = std::filesystem::absolute(relativeParentPath).make_preferred();
        fs::create_directories(absolutePath);

        moodycamel::ConcurrentQueue<DiscoveredClientDB> clientDBPairs;

        std::vector<std::filesystem::path> paths;
        std::filesystem::recursive_directory_iterator dirpos{ absolutePath };
        std::copy(begin(dirpos), end(dirpos), std::back_inserter(paths));

        std::for_each(std::execution::par, std::begin(paths), std::end(paths), [&clientDBPairs](const std::filesystem::path& path)
        {
            if (!path.has_extension() || path.extension().compare(::ClientDB::FILE_EXTENSION) != 0)
                return;

            std::string fileNameWithoutExtension = path.filename().replace_extension("").string();

            DiscoveredClientDB discoveredClientDB =
            {
                .hash = StringUtils::fnv1a_32(fileNameWithoutExtension.c_str(), fileNameWithoutExtension.length()),
                .fileName = fileNameWithoutExtension,
                .path = path.string()
            };

            clientDBPairs.enqueue(discoveredClientDB);
        });

        u32 numTotalClientDBs = static_cast<u32>(clientDBPairs.size_approx());
        u32 numLoadedClientDBs = 0;

        entt::registry::context& ctx = registries->gameRegistry->ctx();
        auto& clientDBCollection = ctx.emplace<ECS::Singletons::ClientDBCollection>();
        clientDBCollection.Reserve(numTotalClientDBs);

        std::shared_ptr<Bytebuffer> buffer = Bytebuffer::Borrow<8388608>();

        u32 numDiscoveredClientDBs = static_cast<u32>(clientDBPairs.size_approx());
        std::vector<DiscoveredClientDB> discoveredClientDBs(numDiscoveredClientDBs);
        u32 numDequeuedPairs = static_cast<u32>(clientDBPairs.try_dequeue_bulk(discoveredClientDBs.begin(), numDiscoveredClientDBs));

        std::sort(discoveredClientDBs.begin(), discoveredClientDBs.end(), [](const DiscoveredClientDB& a, const DiscoveredClientDB& b)
        {
            return a.path < b.path;
        });

        for (const DiscoveredClientDB& discoveredDB : discoveredClientDBs)
        {
            buffer->Reset();

            FileReader reader(discoveredDB.path);
            if (!reader.Open())
            {
                NC_LOG_ERROR("ClientDBLoader : Failed to load '{0}'. Could not read file.", discoveredDB.fileName);
                continue;
            }

            reader.Read(buffer.get(), reader.Length());

            auto hash = static_cast<ECS::Singletons::ClientDBHash>(discoveredDB.hash);
            if (!clientDBCollection.Register(hash, discoveredDB.fileName))
                continue;

            ::ClientDB::Data* db = clientDBCollection.Get(hash);
            if (!db->Read(buffer))
            {
                NC_LOG_ERROR("ClientDBLoader : Failed to load '{0}'. Could not read ClientDB from Buffer.", discoveredDB.fileName);
                continue;
            }

            numLoadedClientDBs++;
        }

        NC_LOG_INFO("Loaded {0}/{1} Client Database Files", numLoadedClientDBs, numTotalClientDBs);
    }
}
