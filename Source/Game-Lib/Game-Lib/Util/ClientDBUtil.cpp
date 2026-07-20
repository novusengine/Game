#include "ClientDBUtil.h"

#include "Game-Lib/Application/EnttRegistries.h"
#include "Game-Lib/ECS/Singletons/Database/ClientDBSingleton.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Base/Util/DebugHandler.h>

#include <FileFormat/Novus/ClientDB/ClientDB.h>

#include <Filesystem/PactStorage.h>

#include <entt/entt.hpp>

#include <array>
#include <filesystem>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace Util::ClientDB
{
    void DiscoverAll()
    {
        EnttRegistries* registries = ServiceLocator::GetEnttRegistries();
        entt::registry::context& ctx = registries->dbRegistry->ctx();
        auto& clientDBSingleton = ctx.emplace<ECS::Singletons::ClientDBSingleton>();

        std::vector<std::pair<ClientDBHash, std::string>> clientDBs;
        clientDBs.reserve(ClientDBHashes.size());

        std::unordered_set<ClientDBHash> discoveredHashes;
        discoveredHashes.reserve(ClientDBHashes.size());
        for (const ClientDBDefinition& definition : ClientDBHashes)
        {
            clientDBs.emplace_back(definition.hash, definition.debugName);
            discoveredHashes.insert(definition.hash);
        }

        const u32 numTotalClientDBs = static_cast<u32>(clientDBs.size());
        u32 numLoadedClientDBs = 0;
        clientDBSingleton.Reserve(numTotalClientDBs);

        auto* pactStorage = ServiceLocator::GetPactStorage();

        for (const auto& [dbHash, debugName] : clientDBs)
        {
            PACT::PactFileHandle fileHandle;
            if (pactStorage->ReadFile(static_cast<u64>(dbHash), fileHandle) != PACT::PactReadResult::Success)
            {
                NC_LOG_ERROR("ClientDBLoader : Failed to load '{0}'. Could not read file.", debugName);
                continue;
            }

            if (!clientDBSingleton.Register(dbHash, debugName))
                continue;

            ::ClientDB::Data* db = clientDBSingleton.Get(dbHash);
            std::shared_ptr<Bytebuffer> buffer = std::make_shared<Bytebuffer>(const_cast<void*>(fileHandle.GetData()), fileHandle.GetSize());
            buffer->writtenData = buffer->size;

            if (!db->Read(buffer))
            {
                NC_LOG_ERROR("ClientDBLoader : Failed to load '{0}'. Could not read ClientDB from Buffer.", debugName);
                clientDBSingleton.Remove(dbHash);
                continue;
            }

            numLoadedClientDBs++;
        }

        NC_LOG_INFO("Loaded {0}/{1} Client Database Files", numLoadedClientDBs, numTotalClientDBs);
    }
}
