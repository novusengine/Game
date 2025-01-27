#include "MapLoader.h"
#include "Game-Lib/Application/EnttRegistries.h"
#include "Game-Lib/Editor/EditorHandler.h"
#include "Game-Lib/Editor/Inspector.h"
#include "Game-Lib/ECS/Components/Events.h"
#include "Game-Lib/ECS/Singletons/ClientDBCollection.h"
#include "Game-Lib/ECS/Singletons/MapDB.h"
#include "Game-Lib/ECS/Util/EventUtil.h"
#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Rendering/Debug/JoltDebugRenderer.h"
#include "Game-Lib/Rendering/Terrain/TerrainLoader.h"
#include "Game-Lib/Rendering/Model/ModelLoader.h"
#include "Game-Lib/Rendering/Model/ModelRenderer.h"
#include "Game-Lib/Rendering/Liquid/LiquidLoader.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Base/Memory/Bytebuffer.h>
#include <Base/Memory/FileReader.h>

#include <FileFormat/Novus/ClientDB/ClientDB.h>
#include <FileFormat/Novus/ClientDB/Definitions.h>
#include <FileFormat/Novus/Map/Map.h>

#include <entt/entt.hpp>

#include <memory>
#include <filesystem>
namespace fs = std::filesystem;

using namespace ECS::Singletons;

void MapLoader::Update(f32 deltaTime)
{
    bool discoveredModelsCompleteLastFrame = _discoveredModelsCompleteLastFrame;
    bool discoveredModelsCompleteThisFrame = _modelLoader->DiscoveredModelsComplete();
    if (!discoveredModelsCompleteThisFrame)
        return;

    _discoveredModelsCompleteLastFrame = true;
    if (discoveredModelsCompleteThisFrame && !discoveredModelsCompleteLastFrame)
        return;
    
    ZoneScoped;

    size_t numRequests = _requests.size_approx();
    if (numRequests == 0)
        return;

    LoadDesc request;
    while (_requests.try_dequeue(request)) { } // Empty the queue and only listen to the last request

    // Clear Map
    if (request.internalMapNameHash == std::numeric_limits<u32>().max())
    {
        if (_currentMapID == std::numeric_limits<u32>().max())
            return;

        _currentMapID = std::numeric_limits<u32>().max();
        ClearRenderersForMap();
    }
    else
    {
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& clientDBCollection = registry->ctx().get<ClientDBCollection>();
        auto& mapDB = registry->ctx().get<MapDB>();
        auto* mapStorage = clientDBCollection.Get(ClientDBHash::Map);

        const robin_hood::unordered_map<u32, u32>& internalNameHashToID = mapDB.mapInternalNameHashToID;

        if (!internalNameHashToID.contains(request.internalMapNameHash))
            return;

        u32 mapID = internalNameHashToID.at(request.internalMapNameHash);
        if (!mapStorage->Has(mapID))
            return;

        const ClientDB::Definitions::Map& currentMap = mapStorage->Get<ClientDB::Definitions::Map>(mapID);
        const std::string& mapInternalName = mapStorage->GetString(currentMap.internalName);
        
        fs::path relativeParentPath = "Data/Map";
        fs::path absolutePath = std::filesystem::absolute(relativeParentPath).make_preferred();
        std::string mapFile = ((absolutePath / mapInternalName / mapInternalName).replace_extension(Map::HEADER_FILE_EXTENSION)).string();
        
        if (!fs::exists(mapFile))
        {
            NC_LOG_ERROR("MapLoader : Failed to find map file '{0}'", mapFile);
            return;
        }
        
        FileReader fileReader(mapFile);
        if (!fileReader.Open())
            return;
        
        size_t bufferLength = fileReader.Length();
        std::shared_ptr<Bytebuffer> buffer = Bytebuffer::BorrowRuntime(bufferLength);
        
        fileReader.Read(buffer.get(), bufferLength);
        
        Map::MapHeader mapHeader;
        if (!Map::MapHeader::Read(buffer, mapHeader))
            return;
        
        if (mapHeader.flags.UseMapObjectAsBase)
        {
            if (!_modelLoader->ContainsDiscoveredModel(mapHeader.placement.nameHash))
                return;

            _currentMapID = mapID;
        
            ClearRenderersForMap();
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
    LoadDesc loadDesc;
    loadDesc.internalMapNameHash = std::numeric_limits<u32>().max();

    _requests.enqueue(loadDesc);
}

void MapLoader::LoadMap(u32 mapHash)
{
    LoadDesc loadDesc;
    loadDesc.internalMapNameHash = mapHash;

    _requests.enqueue(loadDesc);
}

void MapLoader::ClearRenderersForMap()
{
    _terrainLoader->Clear();
    _modelLoader->Clear();
    _liquidLoader->Clear();

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
    ServiceLocator::GetGameRenderer()->GetJoltDebugRenderer()->Clear();

    Editor::EditorHandler* editorHandler = ServiceLocator::GetEditorHandler();
    editorHandler->GetInspector()->ClearSelection();

    ECS::Util::EventUtil::PushEvent(ECS::Components::MapLoadedEvent{ _currentMapID });
}
