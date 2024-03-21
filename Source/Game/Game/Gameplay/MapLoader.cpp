#include "MapLoader.h"
#include "Game/Application/EnttRegistries.h"
#include "Game/Editor/EditorHandler.h"
#include "Game/Editor/Inspector.h"
#include "Game/ECS/Singletons/ClientDBCollection.h"
#include "Game/ECS/Singletons/MapDB.h"
#include "Game/Rendering/GameRenderer.h"
#include "Game/Rendering/Debug/JoltDebugRenderer.h"
#include "Game/Rendering/Terrain/TerrainLoader.h"
#include "Game/Rendering/Model/ModelLoader.h"
#include "Game/Rendering/Model/ModelRenderer.h"
#include "Game/Rendering/Liquid/LiquidLoader.h"
#include "Game/Util/ServiceLocator.h"

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
    size_t numRequests = _requests.size_approx();
    if (numRequests == 0)
        return;

    LoadDesc request;
    while (_requests.try_dequeue(request)) { } // Empty the queue and only listen to the last request

    // Clear Map
    if (request.internalMapNameHash == std::numeric_limits<u32>().max())
    {
        ClearRenderersForMap();
    }
    else
    {
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& clientDBCollection = registry->ctx().get<ClientDBCollection>();
        auto& mapDB = registry->ctx().get<MapDB>();
        auto maps = clientDBCollection.Get<ClientDB::Definitions::Map>(ClientDBHash::Map);

        const robin_hood::unordered_map<u32, u32>& internalNameHashToID = mapDB.mapInternalNameHashToID;

        if (!internalNameHashToID.contains(request.internalMapNameHash))
            return;

        u32 mapID = internalNameHashToID.at(request.internalMapNameHash);
        if (!maps.Contains(mapID))
            return;

        const ClientDB::Definitions::Map& currentMap = maps.GetByID(mapID);
        
        fs::path relativeParentPath = "Data/Map";
        fs::path absolutePath = std::filesystem::absolute(relativeParentPath).make_preferred();
        
        const std::string& internalMapName = maps.GetString(currentMap.internalName);
        std::string mapFile = ((absolutePath / internalMapName / internalMapName).replace_extension(Map::HEADER_FILE_EXTENSION)).string();
        
        if (!fs::exists(mapFile))
        {
            DebugHandler::PrintError("MapLoader : Failed to find map file '{0}'", mapFile);
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
        
            ClearRenderersForMap();
            _modelLoader->LoadPlacement(mapHeader.placement);
        }
        else
        {
            TerrainLoader::LoadDesc loadDesc;
            loadDesc.loadType = TerrainLoader::LoadType::Full;
            loadDesc.mapName = internalMapName;
        
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

    ServiceLocator::GetGameRenderer()->GetJoltDebugRenderer()->Clear();

    Editor::EditorHandler* editorHandler = ServiceLocator::GetEditorHandler();
    editorHandler->GetInspector()->ClearSelection();
}
