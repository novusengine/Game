#include "MapLoader.h"
#include "Game-Lib/Application/EnttRegistries.h"
#include "Game-Lib/Editor/EditorHandler.h"
#include "Game-Lib/Editor/Inspector.h"
#include "Game-Lib/ECS/Components/Events.h"
#include "Game-Lib/ECS/Singletons/Database/ClientDBSingleton.h"
#include "Game-Lib/ECS/Singletons/Database/MapSingleton.h"
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
#include <FileFormat/Novus/Map/Map.h>

#include <Meta/Generated/Shared/ClientDB.h>

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

    if (!_loadRequest.isRequest)
        return;

    // Clear Map
    if (_loadRequest.internalMapNameHash == std::numeric_limits<u32>().max())
    {
        if (_currentMapID == std::numeric_limits<u32>().max())
            return;

        _currentMapID = std::numeric_limits<u32>().max();
        ClearRenderersForMap();

        ECS::Util::EventUtil::PushEvent(ECS::Components::MapLoadedEvent{ _currentMapID });

        _loadRequest.isRequest = false;
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

        const auto& currentMap = mapStorage->Get<Generated::MapRecord>(mapID);
        const std::string& mapInternalName = mapStorage->GetString(currentMap.nameInternal);
        
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

        _loadRequest.isRequest = false;
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

    Editor::EditorHandler* editorHandler = ServiceLocator::GetEditorHandler();
    editorHandler->GetInspector()->ClearSelection();
}
