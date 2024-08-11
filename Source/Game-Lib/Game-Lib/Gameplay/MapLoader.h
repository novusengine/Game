#pragma once
#include "Game/Rendering/Terrain/TerrainLoader.h"
#include "Game/Rendering/Model/ModelLoader.h"

#include <Base/Container/ConcurrentQueue.h>

#include <limits>

class TerrainLoader;
class ModelLoader;
class ModelRenderer;
class MapLoader
{
private:
    struct LoadDesc
    {
    public:
        u32 internalMapNameHash = std::numeric_limits<u32>().max();
    };

public:
    MapLoader(TerrainLoader* terrainLoader, ModelLoader* modelLoader, LiquidLoader* liquidLoader) : _terrainLoader(terrainLoader), _modelLoader(modelLoader), _liquidLoader(liquidLoader) { }

    void Update(f32 deltaTime);

    void UnloadMap();
    void LoadMap(u32 mapHash);

    const u32 GetCurrentMapID() { return _currentMapID; }

private:
    void ClearRenderersForMap();

private:
    TerrainLoader* _terrainLoader = nullptr;
    ModelLoader* _modelLoader = nullptr;
    LiquidLoader* _liquidLoader = nullptr;

    u32 _currentMapID = std::numeric_limits<u32>().max();
    moodycamel::ConcurrentQueue<LoadDesc> _requests;
};