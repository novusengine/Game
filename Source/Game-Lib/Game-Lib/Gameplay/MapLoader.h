#pragma once
#include "Game-Lib/ECS/Components/Events.h"
#include "Game-Lib/Rendering/Terrain/TerrainLoader.h"
#include "Game-Lib/Rendering/Model/ModelLoader.h"

#include <Base/Container/ConcurrentQueue.h>

#include <limits>

class TerrainLoader;
class ModelLoader;
class ModelRenderer;
namespace ModelRendering { class ModelPlacementLoader; }
class MapLoader
{
private:
    struct LoadDesc
    {
    public:
        bool isRequest = false;
        u32 internalMapNameHash = std::numeric_limits<u32>().max();
    };

public:
    MapLoader(TerrainLoader* terrainLoader, ModelLoader* modelLoader, ModelRendering::ModelPlacementLoader* modelPlacementLoader, LiquidLoader* liquidLoader)
        : _terrainLoader(terrainLoader), _modelLoader(modelLoader), _modelPlacementLoader(modelPlacementLoader), _liquidLoader(liquidLoader) { }

    void Update(f32 deltaTime);

    void UnloadMap();
    void UnloadMapImmediately();
    void LoadMap(u32 mapHash);
    void ReportLoadFailure(u32 mapID, ECS::Components::MapLoadFailureReason reason);
    void CompleteTransitionTrace();

    const u32 GetCurrentMapID() { return _currentMapID; }

private:
    void ClearRenderersForMap();

private:
    TerrainLoader* _terrainLoader = nullptr;
    ModelLoader* _modelLoader = nullptr;
    ModelRendering::ModelPlacementLoader* _modelPlacementLoader = nullptr;
    LiquidLoader* _liquidLoader = nullptr;

    u32 _currentMapID = std::numeric_limits<u32>().max();
    LoadDesc _loadRequest;
    bool _mapLoadTraceActive = false;
};
