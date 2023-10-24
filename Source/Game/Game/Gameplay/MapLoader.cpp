#include "MapLoader.h"
#include "Game/Application/EnttRegistries.h"
#include "Game/Editor/EditorHandler.h"
#include "Game/Editor/Inspector.h"
#include "Game/ECS/Singletons/MapDB.h"
#include "Game/Rendering/Terrain/TerrainLoader.h"
#include "Game/Rendering/Model/ModelLoader.h"
#include "Game/Rendering/Model/ModelRenderer.h"
#include "Game/Rendering/Water/WaterLoader.h"
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

void MapLoader::Update(f32 deltaTime)
{
    size_t numRequests = _requests.size_approx();
    if (numRequests == 0)
        return;

    LoadDesc request;
    while (_requests.try_dequeue(request)) { } // Empty the queue and only listen to the last request

    // Clear Map
    if (request.mapNameHash == std::numeric_limits<u32>().max())
    {
        ClearRenderersForMap();
    }
    else
    {
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& mapDB = registry->ctx().at<ECS::Singletons::MapDB>();

        const robin_hood::unordered_map<u32, u32>* mapNameHashToIndex = &mapDB.mapNameHashToIndex;

        if (request.isInternalName)
        {
            mapNameHashToIndex = &mapDB.mapInternalNameHashToIndex;
        }

        if (!mapNameHashToIndex->contains(request.mapNameHash))
            return;

        u32 mapIndex = mapNameHashToIndex->at(request.mapNameHash);
        const DB::Client::Definitions::Map& currentMap = mapDB.entries.GetEntryByIndex(mapIndex);

        static const fs::path fileExtension = ".map";
        fs::path relativeParentPath = "Data/Map";
        fs::path absolutePath = std::filesystem::absolute(relativeParentPath).make_preferred();

        const std::string& internalMapName = mapDB.entries.GetString(currentMap.internalName);
        std::string mapFile = ((absolutePath / internalMapName / internalMapName).replace_extension(fileExtension)).string();

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

        Map::Layout mapLayout;
        if (!buffer->Get(mapLayout))
            return;

        if (mapLayout.flags.UseMapObjectAsBase)
        {
            if (!_modelLoader->ContainsDiscoveredModel(mapLayout.placement.nameHash))
                return;

            ClearRenderersForMap();
            _modelLoader->LoadPlacement(mapLayout.placement);
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
    loadDesc.mapNameHash = std::numeric_limits<u32>().max();

    _requests.enqueue(loadDesc);
}

void MapLoader::LoadMap(u32 mapHash)
{
    LoadDesc loadDesc;
    loadDesc.mapNameHash = mapHash;

    _requests.enqueue(loadDesc);
}

void MapLoader::LoadMapWithInternalName(u32 mapHash)
{
    LoadDesc loadDesc;
    loadDesc.isInternalName = true;
    loadDesc.mapNameHash = mapHash;

    _requests.enqueue(loadDesc);
}

void MapLoader::ClearRenderersForMap()
{
    _terrainLoader->Clear();
    _modelLoader->Clear();
    _waterLoader->Clear();

    Editor::EditorHandler* editorHandler = ServiceLocator::GetEditorHandler();
    editorHandler->GetInspector()->ClearSelection();
}
