#include "MapLoader.h"
#include "Game-Lib/Application/EnttRegistries.h"
#include "Game-Lib/ECS/Components/Events.h"
#include "Game-Lib/ECS/Systems/Editor/EditorTools.h"
#include "Game-Lib/ECS/Singletons/Database/ClientDBSingleton.h"
#include "Game-Lib/ECS/Singletons/Database/MapSingleton.h"
#include "Game-Lib/ECS/Singletons/JoltState.h"
#include "Game-Lib/ECS/Util/EventUtil.h"
#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Rendering/Debug/JoltDebugRenderer.h"
#include "Game-Lib/Rendering/Terrain/TerrainLoader.h"
#include "Game-Lib/Rendering/Model/ModelLoader.h"
#include "Game-Lib/Rendering/Model/ModelRenderer.h"
#include "Game-Lib/Rendering/Liquid/LiquidLoader.h"
#include "Game-Lib/Util/ServiceLocator.h"
#include "Game-Lib/Util/AssetPath.h"

#include <Base/Memory/Bytebuffer.h>
#include <Base/Memory/FileReader.h>

#include <FileFormat/Novus/ClientDB/ClientDB.h>
#include <FileFormat/Novus/Map/Map.h>

#include <Filesystem/PactStorage.h>

#include <MetaGen/Shared/ClientDB/ClientDB.h>

#include <entt/entt.hpp>

#include <memory>
#include <filesystem>
namespace fs = std::filesystem;

using namespace ECS::Singletons;

void MapLoader::Update(f32 deltaTime)
{
    ZoneScoped;

    if (!_loadRequest.isRequest)
        return;

    // Request is being handled this frame
    _loadRequest.isRequest = false;

    // Clear Map
    if (_loadRequest.internalMapNameHash == std::numeric_limits<u32>().max())
    {
        if (_currentMapID == std::numeric_limits<u32>().max())
            return;

        _currentMapID = std::numeric_limits<u32>().max();
        ClearRenderersForMap();

        ECS::Util::EventUtil::PushEvent(ECS::Components::MapLoadedEvent{ _currentMapID });
    }
    else
    {
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->dbRegistry;
        auto& clientDBSingleton = registry->ctx().get<ClientDBSingleton>();
        auto& mapSingleton = registry->ctx().get<MapSingleton>();
        auto* mapStorage = clientDBSingleton.Get(ClientDBHash::Map);

        const robin_hood::unordered_map<u32, u32>& internalNameHashToID = mapSingleton.mapInternalNameHashToID;

        if (!internalNameHashToID.contains(_loadRequest.internalMapNameHash))
            return;

        u32 mapID = internalNameHashToID.at(_loadRequest.internalMapNameHash);
        if (!mapStorage->Has(mapID))
            return;

        const auto& currentMap = mapStorage->Get<MetaGen::Shared::ClientDB::MapRecord>(mapID);
        const std::string& mapInternalName = mapStorage->GetString(currentMap.nameInternal);

        auto* pactStorage = ServiceLocator::GetPactStorage();
        std::string mapHeaderPath = Util::AssetPath::Map(mapInternalName + "/" + mapInternalName + Map::HEADER_FILE_EXTENSION);
        
        PACT::PactFileHandle fileHandle;
        if (pactStorage->ReadFile(mapHeaderPath, fileHandle) != PACT::PactReadResult::Success)
            return;

        Map::MapHeader mapHeader;
        std::shared_ptr<Bytebuffer> buffer = std::make_shared<Bytebuffer>(const_cast<void*>(fileHandle.GetData()), fileHandle.GetSize());
        buffer->writtenData = fileHandle.GetSize();

        if (!Map::MapHeader::Read(buffer, mapHeader))
            return;
        
        if (mapHeader.flags.UseMapObjectAsBase)
        {
            if (!_modelLoader->ContainsDiscoveredModel(mapHeader.placement.nameHash))
                return;

            _currentMapID = mapID;
        
            ClearRenderersForMap();
            ServiceLocator::GetEnttRegistries()->gameRegistry->ctx().get<ECS::Singletons::JoltState>().ResetPhysicsTelemetry(mapInternalName);
            _modelLoader->SetTerrainLoading(true);
            _modelLoader->LoadPlacement(mapHeader.placement);
        }
        else
        {
            _currentMapID = mapID;

            TerrainLoader::LoadDesc loadDesc;
            loadDesc.loadType = TerrainLoader::LoadType::Full;
            loadDesc.mapName = mapInternalName;
        
            _terrainLoader->AddInstance(loadDesc);
        }
    }
}

void MapLoader::UnloadMap()
{
    _loadRequest.isRequest = true;
    _loadRequest.internalMapNameHash = std::numeric_limits<u32>().max();
}

void MapLoader::UnloadMapImmediately()
{
    if (_currentMapID == std::numeric_limits<u32>().max())
        return;

    _currentMapID = std::numeric_limits<u32>().max();
    ClearRenderersForMap();

    _loadRequest.isRequest = false;
    _loadRequest.internalMapNameHash = std::numeric_limits<u32>().max();
}

void MapLoader::LoadMap(u32 mapHash)
{
    _loadRequest.isRequest = true;
    _loadRequest.internalMapNameHash = mapHash;
}

void MapLoader::ClearRenderersForMap()
{
    _terrainLoader->Clear();
    _modelLoader->Clear();
    _liquidLoader->Clear();

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
    ServiceLocator::GetGameRenderer()->GetJoltDebugRenderer()->Clear();

    // Clear any editor selection -- the unloaded map's selected entity no longer exists.
    ECS::Systems::Editor::EditorTools::SetSelectedEntity(*registry, entt::null);
}
