#include "Game/Loaders/LoaderSystem.h"
#include "Game/Util/ServiceLocator.h"
#include "Game/ECS/Singletons/ClientDBSingleton.h"
#include "Game/Application/EnttRegistries.h"

#include <Base/Container/ConcurrentQueue.h>
#include <Base/Memory/FileReader.h>
#include <Base/Util/DebugHandler.h>

#include <entt/entt.hpp>

#include <execution>
#include <filesystem>
#include <limits>
namespace fs = std::filesystem;

struct ClientDBPair
{
    u32 hash;
    std::string path;
};

class ClientDBLoader : Loader
{
public:
    ClientDBLoader() : Loader("ClientDBLoader", 9999) { }

    bool Init()
    {
        EnttRegistries* registries = ServiceLocator::GetEnttRegistries();
        
        entt::registry* registry = registries->gameRegistry;
        
        entt::registry::context& ctx = registry->ctx();
        ECS::Singletons::ClientDBSingleton& clientDBSingleton = ctx.emplace<ECS::Singletons::ClientDBSingleton>();
        
        fs::path relativeParentPath = "Data/ClientDB";
        fs::path absolutePath = std::filesystem::absolute(relativeParentPath).make_preferred();
        
        if (!fs::is_directory(absolutePath))
        {
            DebugHandler::PrintError("Failed to find 'Texture' folder");
            return false;
        }
        
        static const fs::path fileExtension = ".cdb";
        
        std::vector<std::filesystem::path> paths;
        moodycamel::ConcurrentQueue<ClientDBPair> clientDBPairs;
        
        std::filesystem::recursive_directory_iterator dirpos{ absolutePath };
        std::copy(begin(dirpos), end(dirpos), std::back_inserter(paths));
        
        std::for_each(std::execution::par, std::begin(paths), std::end(paths), [&clientDBPairs](const std::filesystem::path& path)
        {
            if (!path.has_extension() || path.extension().compare(fileExtension) != 0)
                return;
        
            std::string fileName = path.filename().string();

            ClientDBPair clientDBPair;
            clientDBPair.path = path.string();
            clientDBPair.hash = StringUtils::fnv1a_32(fileName.c_str(), fileName.length());
        
            clientDBPairs.enqueue(clientDBPair);
        });

        size_t numClientDBs = 0;
        
        std::shared_ptr<Bytebuffer> buffer = Bytebuffer::Borrow<8388608>();

        ClientDBPair clientDBPair;
        while (clientDBPairs.try_dequeue(clientDBPair))
        {
            // Find appropriate handler for the given hash
            auto itr = clientDBEntries.find(clientDBPair.hash);
            if (itr != clientDBEntries.end())
            {
                buffer->Reset();

                FileReader reader(clientDBPair.path);
                if (reader.Open())
                {
                    reader.Read(buffer.get(), reader.Length());

                    if (itr->second(clientDBSingleton, buffer, clientDBPair))
                    {
                        numClientDBs++;
                        continue;
                    }
                }


                DebugHandler::PrintError("ClientDBLoader : Failed to load '{0}'", clientDBPair.path);
            }
        }
        
        DebugHandler::Print("Loaded {0} Client Database Files", numClientDBs);
        return true;
    }

    bool LoadMapDB(ECS::Singletons::ClientDBSingleton& clientDBSingleton, std::shared_ptr<Bytebuffer>& buffer, const ClientDBPair& pair)
    {
        auto& mapDB = clientDBSingleton.mapDB;
        // Clear, in case we already filled mapDB
        mapDB.data.clear();
        mapDB.stringTable.Clear();
     
        if (!mapDB.Read(buffer))
        {
            return false;
        }

        clientDBSingleton.mapNames.reserve(mapDB.stringTable.GetNumStrings());

        for (u32 i = 0; i < mapDB.data.size(); i++)
        {
            const DB::Client::Definitions::Map& map = mapDB.data[i];
            if (map.name == std::numeric_limits<u32>().max())
            {
                continue;
            }

            clientDBSingleton.mapNames.push_back(mapDB.stringTable.GetString(map.name));
        }

        return true;
    }

    robin_hood::unordered_map<u32, std::function<bool(ECS::Singletons::ClientDBSingleton&, std::shared_ptr<Bytebuffer>&, const ClientDBPair&)>> clientDBEntries =
    {
        { "Map.cdb"_h, std::bind(&ClientDBLoader::LoadMapDB, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3) }
    };
};

ClientDBLoader clientDBLoader;