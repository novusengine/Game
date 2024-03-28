#pragma once
#include "Game/Application/EnttRegistries.h"
#include "Game/ECS/Singletons/ClientDBCollection.h"
#include "Game/Util/ServiceLocator.h"

#include <Base/Container/ConcurrentQueue.h>
#include <Base/Memory/FileReader.h>
#include <Base/Util/DebugHandler.h>

#include <FileFormat/Novus/ClientDB/ClientDB.h>

#include <entt/entt.hpp>

#include <execution>
#include <filesystem>

namespace fs = std::filesystem;
using namespace ECS::Singletons;

struct ClientDBPair
{
public:
    u32 hash;
    std::string fileName;
    std::string path;
};

class ClientDBLoader
{
public:
    ClientDBLoader() = delete;

    static bool Init()
    {
        EnttRegistries* registries = ServiceLocator::GetEnttRegistries();
        
        fs::path relativeParentPath = "Data/ClientDB";
        fs::path absolutePath = std::filesystem::absolute(relativeParentPath).make_preferred();
        fs::create_directories(absolutePath);
        
        moodycamel::ConcurrentQueue<ClientDBPair> clientDBPairs;

        std::vector<std::filesystem::path> paths;
        std::filesystem::recursive_directory_iterator dirpos{ absolutePath };
        std::copy(begin(dirpos), end(dirpos), std::back_inserter(paths));

        std::for_each(std::execution::par, std::begin(paths), std::end(paths), [&clientDBPairs](const std::filesystem::path& path)
        {
            if (!path.has_extension() || path.extension().compare(ClientDB::FILE_EXTENSION) != 0)
                return;

            std::string fileName = path.filename().string();

            ClientDBPair clientDBPair;
            clientDBPair.hash = StringUtils::fnv1a_32(fileName.c_str(), fileName.length());
            clientDBPair.fileName = fileName;
            clientDBPair.path = path.string();

            clientDBPairs.enqueue(clientDBPair);
        });

        u32 numTotalClientDBs = static_cast<u32>(clientDBPairs.size_approx());
        u32 numLoadedClientDBs = 0;

        entt::registry::context& ctx = registries->gameRegistry->ctx();
        auto& clientDBCollection = ctx.emplace<ClientDBCollection>();
        clientDBCollection._dbs.reserve(numTotalClientDBs);

        std::shared_ptr<Bytebuffer> buffer = Bytebuffer::Borrow<8388608>();

        ClientDBPair clientDBPair;
        while (clientDBPairs.try_dequeue(clientDBPair))
        {
            DebugHandler::Print("ClientDBLoader : Loading '{0}'", clientDBPair.fileName);
            buffer->Reset();

            FileReader reader(clientDBPair.path);
            if (!reader.Open())
            {
                DebugHandler::PrintError("ClientDBLoader : Failed to load '{0}'. Could not read file.", clientDBPair.fileName);
                continue;
            }

            reader.Read(buffer.get(), reader.Length());

            auto* rawDB = new ClientDB::StorageRaw(clientDBPair.fileName);
            if (!rawDB->Read(buffer))
            {
                DebugHandler::PrintError("ClientDBLoader : Failed to load '{0}'. Could not read ClientDB from Buffer.", clientDBPair.fileName);
                continue;
            }

            ClientDBHash hash = static_cast<ClientDBHash>(clientDBPair.hash);
            u32 index = static_cast<u32>(clientDBCollection._dbs.size());

            clientDBCollection._dbs.push_back(rawDB);
            clientDBCollection._dbHashToIndex[hash] = index;

            numLoadedClientDBs++;
        }

        DebugHandler::Print("Loaded {0}/{1} Client Database Files", numLoadedClientDBs, numTotalClientDBs);

        return true;
    }
};