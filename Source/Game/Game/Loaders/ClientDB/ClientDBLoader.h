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
            NC_LOG_INFO("ClientDBLoader : Loading '{0}'", clientDBPair.fileName);
            buffer->Reset();

            FileReader reader(clientDBPair.path);
            if (!reader.Open())
            {
                NC_LOG_ERROR("ClientDBLoader : Failed to load '{0}'. Could not read file.", clientDBPair.fileName);
                continue;
            }

            reader.Read(buffer.get(), reader.Length());

            ClientDBHash hash = static_cast<ClientDBHash>(clientDBPair.hash);
            u32 index = static_cast<u32>(clientDBCollection._dbs.size());

            switch (hash)
            {
                case ClientDBHash::Map:
                {
                    auto* db = new ClientDB::Storage<ClientDB::Definitions::Map>(clientDBPair.fileName);
                    if (!db->Read(buffer))
                    {
                        NC_LOG_ERROR("ClientDBLoader : Failed to load '{0}'. Could not read ClientDB from Buffer.", clientDBPair.fileName);
                        break;
                    }

                    auto* rawDB = reinterpret_cast<ClientDB::Storage<ClientDB::Definitions::Empty>*>(db);
                    clientDBCollection._dbs.push_back(rawDB);
                    clientDBCollection._dbHashToIndex[hash] = index;
                    numLoadedClientDBs++;
                    break;
                }

                case ClientDBHash::LiquidObject:
                {
                    auto* db = new ClientDB::Storage<ClientDB::Definitions::LiquidObject>(clientDBPair.fileName);
                    if (!db->Read(buffer))
                    {
                        NC_LOG_ERROR("ClientDBLoader : Failed to load '{0}'. Could not read ClientDB from Buffer.", clientDBPair.fileName);
                        break;
                    }

                    auto* rawDB = reinterpret_cast<ClientDB::Storage<ClientDB::Definitions::Empty>*>(db);
                    clientDBCollection._dbs.push_back(rawDB);
                    clientDBCollection._dbHashToIndex[hash] = index;
                    numLoadedClientDBs++;
                    break;
                }

                case ClientDBHash::LiquidType:
                {
                    auto* db = new ClientDB::Storage<ClientDB::Definitions::LiquidType>(clientDBPair.fileName);
                    if (!db->Read(buffer))
                    {
                        NC_LOG_ERROR("ClientDBLoader : Failed to load '{0}'. Could not read ClientDB from Buffer.", clientDBPair.fileName);
                        break;
                    }

                    auto* rawDB = reinterpret_cast<ClientDB::Storage<ClientDB::Definitions::Empty>*>(db);
                    clientDBCollection._dbs.push_back(rawDB);
                    clientDBCollection._dbHashToIndex[hash] = index;
                    numLoadedClientDBs++;
                    break;
                }

                case ClientDBHash::LiquidMaterial:
                {
                    auto* db = new ClientDB::Storage<ClientDB::Definitions::LiquidMaterial>(clientDBPair.fileName);
                    if (!db->Read(buffer))
                    {
                        NC_LOG_ERROR("ClientDBLoader : Failed to load '{0}'. Could not read ClientDB from Buffer.", clientDBPair.fileName);
                        break;
                    }

                    auto* rawDB = reinterpret_cast<ClientDB::Storage<ClientDB::Definitions::Empty>*>(db);
                    clientDBCollection._dbs.push_back(rawDB);
                    clientDBCollection._dbHashToIndex[hash] = index;
                    numLoadedClientDBs++;
                    break;
                }

                case ClientDBHash::CinematicCamera:
                {
                    auto* db = new ClientDB::Storage<ClientDB::Definitions::CinematicCamera>(clientDBPair.fileName);
                    if (!db->Read(buffer))
                    {
                        NC_LOG_ERROR("ClientDBLoader : Failed to load '{0}'. Could not read ClientDB from Buffer.", clientDBPair.fileName);
                        break;
                    }

                    auto* rawDB = reinterpret_cast<ClientDB::Storage<ClientDB::Definitions::Empty>*>(db);
                    clientDBCollection._dbs.push_back(rawDB);
                    clientDBCollection._dbHashToIndex[hash] = index;
                    numLoadedClientDBs++;
                    break;
                }

                case ClientDBHash::CinematicSequence:
                {
                    auto* db = new ClientDB::Storage<ClientDB::Definitions::CinematicSequence>(clientDBPair.fileName);
                    if (!db->Read(buffer))
                    {
                        NC_LOG_ERROR("ClientDBLoader : Failed to load '{0}'. Could not read ClientDB from Buffer.", clientDBPair.fileName);
                        break;
                    }

                    auto* rawDB = reinterpret_cast<ClientDB::Storage<ClientDB::Definitions::Empty>*>(db);
                    clientDBCollection._dbs.push_back(rawDB);
                    clientDBCollection._dbHashToIndex[hash] = index;
                    numLoadedClientDBs++;
                    break;
                }

                case ClientDBHash::AnimationData:
                {
                    auto* db = new ClientDB::Storage<ClientDB::Definitions::AnimationData>(clientDBPair.fileName);
                    if (!db->Read(buffer))
                    {
                        NC_LOG_ERROR("ClientDBLoader : Failed to load '{0}'. Could not read ClientDB from Buffer.", clientDBPair.fileName);
                        break;
                    }

                    auto* rawDB = reinterpret_cast<ClientDB::Storage<ClientDB::Definitions::Empty>*>(db);
                    clientDBCollection._dbs.push_back(rawDB);
                    clientDBCollection._dbHashToIndex[hash] = index;
                    numLoadedClientDBs++;
                    break;
                }

                case ClientDBHash::CreatureDisplayInfo:
                {
                    auto* db = new ClientDB::Storage<ClientDB::Definitions::CreatureDisplayInfo>(clientDBPair.fileName);
                    if (!db->Read(buffer))
                    {
                        NC_LOG_ERROR("ClientDBLoader : Failed to load '{0}'. Could not read ClientDB from Buffer.", clientDBPair.fileName);
                        break;
                    }

                    auto* rawDB = reinterpret_cast<ClientDB::Storage<ClientDB::Definitions::Empty>*>(db);
                    clientDBCollection._dbs.push_back(rawDB);
                    clientDBCollection._dbHashToIndex[hash] = index;
                    numLoadedClientDBs++;
                    break;
                }

                case ClientDBHash::CreatureDisplayInfoExtra:
                {
                    auto* db = new ClientDB::Storage<ClientDB::Definitions::CreatureDisplayInfoExtra>(clientDBPair.fileName);
                    if (!db->Read(buffer))
                    {
                        NC_LOG_ERROR("ClientDBLoader : Failed to load '{0}'. Could not read ClientDB from Buffer.", clientDBPair.fileName);
                        break;
                    }

                    auto* rawDB = reinterpret_cast<ClientDB::Storage<ClientDB::Definitions::Empty>*>(db);
                    clientDBCollection._dbs.push_back(rawDB);
                    clientDBCollection._dbHashToIndex[hash] = index;
                    numLoadedClientDBs++;
                    break;
                }

                case ClientDBHash::CreatureModelData:
                {
                    auto* db = new ClientDB::Storage<ClientDB::Definitions::CreatureModelData>(clientDBPair.fileName);
                    if (!db->Read(buffer))
                    {
                        NC_LOG_ERROR("ClientDBLoader : Failed to load '{0}'. Could not read ClientDB from Buffer.", clientDBPair.fileName);
                        break;
                    }

                    auto* rawDB = reinterpret_cast<ClientDB::Storage<ClientDB::Definitions::Empty>*>(db);
                    clientDBCollection._dbs.push_back(rawDB);
                    clientDBCollection._dbHashToIndex[hash] = index;
                    numLoadedClientDBs++;
                    break;
                }

                case ClientDBHash::CharSection:
                {
                    auto* db = new ClientDB::Storage<ClientDB::Definitions::CharSection>(clientDBPair.fileName);
                    if (!db->Read(buffer))
                    {
                        NC_LOG_ERROR("ClientDBLoader : Failed to load '{0}'. Could not read ClientDB from Buffer.", clientDBPair.fileName);
                        break;
                    }

                    auto* rawDB = reinterpret_cast<ClientDB::Storage<ClientDB::Definitions::Empty>*>(db);
                    clientDBCollection._dbs.push_back(rawDB);
                    clientDBCollection._dbHashToIndex[hash] = index;
                    numLoadedClientDBs++;
                    break;
                }

                case ClientDBHash::CameraSave:
                {
                    auto* db = new ClientDB::Storage<ClientDB::Definitions::CameraSave>(clientDBPair.fileName);
                    if (!db->Read(buffer))
                    {
                        NC_LOG_ERROR("ClientDBLoader : Failed to load '{0}'. Could not read ClientDB from Buffer.", clientDBPair.fileName);
                        break;
                    }

                    auto* rawDB = reinterpret_cast<ClientDB::Storage<ClientDB::Definitions::Empty>*>(db);
                    clientDBCollection._dbs.push_back(rawDB);
                    clientDBCollection._dbHashToIndex[hash] = index;
                    numLoadedClientDBs++;
                    break;
                }

                case ClientDBHash::Cursor:
                {
                    auto* db = new ClientDB::Storage<ClientDB::Definitions::Cursor>(clientDBPair.fileName);
                    if (!db->Read(buffer))
                    {
                        NC_LOG_ERROR("ClientDBLoader : Failed to load '{0}'. Could not read ClientDB from Buffer.", clientDBPair.fileName);
                        break;
                    }

                    auto* rawDB = reinterpret_cast<ClientDB::Storage<ClientDB::Definitions::Empty>*>(db);
                    clientDBCollection._dbs.push_back(rawDB);
                    clientDBCollection._dbHashToIndex[hash] = index;
                    numLoadedClientDBs++;
                    break;
                }

                default:
                {
                    NC_LOG_ERROR("ClientDBLoader : Failed to load '{0}'. Missing Read Implementation.", clientDBPair.fileName);
                    break;
                }
            }

        }

        NC_LOG_INFO("Loaded {0}/{1} Client Database Files", numLoadedClientDBs, numTotalClientDBs);

        return true;
    }
};