#include "TerrainManipulator.h"

#include "Game/Editor/EditorHandler.h"
#include "Game/Editor/TerrainTools.h"
#include "Game/Editor/Viewport.h"
#include "Game/Rendering/Debug/DebugRenderer.h"
#include "Game/Rendering/Terrain/TerrainRenderer.h"
#include "Game/Util/MapUtil.h"
#include "Game/Util/PhysicsUtil.h"
#include "Game/Util/ServiceLocator.h"

#include <Input/InputManager.h>

#include <GLFW/glfw3.h>

TerrainManipulator::TerrainManipulator(TerrainRenderer& terrainRenderer, DebugRenderer& debugRenderer)
    : _terrainRenderer(terrainRenderer)
    , _debugRenderer(debugRenderer)
{
    InputManager* inputManager = ServiceLocator::GetInputManager();
    KeybindGroup* keybindGroup = inputManager->GetKeybindGroupByHash("Imgui"_h);

    /*keybindGroup->AddKeyboardCallback("Manipulate", GLFW_MOUSE_BUTTON_LEFT, KeybindAction::Click, KeybindModifier::Any, [&](i32 key, KeybindAction action, KeybindModifier modifier)
    {
        _isManipulating = action == KeybindAction::Press;
        _isLower = modifier == KeybindModifier::Shift;
        return true;
    });*/
}

TerrainManipulator::~TerrainManipulator()
{

}

void TerrainManipulator::Update(f32 deltaTime)
{
    if (_isManipulating)
    {
        Editor::Viewport* viewport = ServiceLocator::GetEditorHandler()->GetViewport();

        vec3 mouseWorldPosition;
        if (Util::Physics::GetMouseWorldPosition(viewport, mouseWorldPosition))
        {
            _debugLastClickPos = mouseWorldPosition;

            Editor::TerrainTools* terrainTools = ServiceLocator::GetEditorHandler()->GetTerrainTools();
            f32 radius = terrainTools->GetRadius();
            f32 pressure = terrainTools->GetPressure();
            f32 hardness = terrainTools->GetHardness();

            Editor::TerrainTools::HardnessMode hardnessMode = static_cast<Editor::TerrainTools::HardnessMode>(terrainTools->GetHardnessMode());

            std::vector<VertexData> vertexDatas;
            GetVertexDatasAroundWorldPos(mouseWorldPosition, radius, hardness, hardnessMode, vertexDatas);

            Renderer::GPUVector<TerrainRenderer::TerrainVertex>& gpuVertices = _terrainRenderer._vertices;
            std::vector<TerrainRenderer::TerrainVertex>& vertices = gpuVertices.Get();

            Renderer::GPUVector<TerrainRenderer::CellHeightRange>& gpuCellHeightRanges = _terrainRenderer._cellHeightRanges;
            std::vector<TerrainRenderer::CellHeightRange>& cellHeightRanges = gpuCellHeightRanges.Get();

            for (const VertexData& vertexData : vertexDatas)
            {
                // Update height
                f32 height = vertices[vertexData.vertexID].height;

                height += pressure * vertexData.hardness * deltaTime * (_isLower ? -1.0f : 1.0f);
                
                vertices[vertexData.vertexID].height = height;
                gpuVertices.SetDirtyElement(vertexData.vertexID);

                // Update height range for culling
                TerrainRenderer::CellHeightRange& cellHeightRange = cellHeightRanges[vertexData.cellHeightRangeID];
                cellHeightRange.min = glm::min(cellHeightRange.min, height);
                cellHeightRange.max = glm::max(cellHeightRange.max, height);
                gpuCellHeightRanges.SetDirtyElement(vertexData.cellHeightRangeID);
            }
        }
    }
}

// A function to apply brush hardness.
// 'distance' is the distance of the pixel from the brush center.
// 'radius' is the radius of the brush.
// 'hardness' is a value between 0 and 1 representing the hardness of the brush.
f32 CalculateLinearHardness(f32 distance, f32 radius, f32 hardness)
{
    f32 effect = 0.0f;
    if (hardness == 1.0f)
    {
        effect = 1.0f;
    }
    else
    {
        // Calculate the falloff distance based on the hardness
        f32 falloffRange = radius * (1.0f - hardness);
        f32 falloffStart = radius * hardness;

        // Apply a linear falloff
        if (distance <= falloffStart)
        {
            // We are within the "hard" area of the brush, so apply full effect
            effect = 1.0f;
        }
        else
        {
            // Within the "soft" area, the effect diminishes linearly to the edge
            effect = 1.0f - ((distance - falloffStart) / falloffRange);
        }
    }
    return effect;
}

f32 CalculateQuadraticHardness(f32 distance, f32 radius, f32 hardness) 
{
    f32 effect = 0.0f;
    if (hardness == 1.0f) 
    {
        effect = 1.0f;
    }
    else 
    {
        // The falloff starts at the point where hardness ends
        f32 falloffStart = radius * hardness;

        if (distance <= falloffStart) 
        {
            effect = 1.0f;
        }
        else if (distance <= radius) 
        {
            f32 relativeDistance = (distance - falloffStart) / (radius - falloffStart);
            effect = 1.0f - relativeDistance * relativeDistance; // Quadratic falloff
        }
    }
    return effect;
}

f32 CalculateGaussianHardness(f32 distance, f32 radius, f32 hardness) 
{
    // Ensure hardness is in the range [0.01, 1] to prevent division by zero
    hardness = std::max(0.01f, hardness);

    // Standard deviation based on hardness
    f32 sigma = radius * hardness;

    // Gaussian function
    f32 effect = std::exp(-0.5f * std::pow(distance / sigma, 2.0f));

    return effect;
}

f32 CalculateExponentialHardness(f32 distance, f32 radius, f32 hardness) 
{
    f32 effect = 0.0f;
    if (hardness == 1.0f) 
    {
        effect = 1.0f;
    }
    else 
    {
        f32 falloffStart = radius * hardness;
        if (distance <= falloffStart) 
        {
            effect = 1.0f;
        }
        else if (distance <= radius) 
        {
            f32 relativeDistance = (distance - falloffStart) / (radius - falloffStart);
            effect = std::exp(-relativeDistance * relativeDistance * relativeDistance); // Exponential falloff
        }
    }
    return effect;
}

f32 CalculateSmoothstepHardness(f32 distance, f32 radius, f32 hardness) 
{
    f32 effect = 0.0f;
    if (hardness == 1.0f) 
    {
        effect = 1.0f;
    }
    else {
        f32 falloffStart = radius * hardness;
        if (distance <= falloffStart) 
        {
            effect = 1.0f;
        }
        else if (distance <= radius) 
        {
            effect = 1.0f - glm::smoothstep(falloffStart, radius, distance); // Smooth step falloff
        }
    }
    return effect;
}

void TerrainManipulator::GetVertexDatasAroundWorldPos(const vec3& worldPos, f32 radius, f32 hardness, Editor::TerrainTools::HardnessMode hardnessMode, std::vector<VertexData>& outVertexDatas)
{
    vec2 chunkGlobalPos = Util::Map::WorldPositionToChunkGlobalPos(worldPos);

    vec2 startChunkGlobalPos = chunkGlobalPos - vec2(radius, radius);
    vec2 endChunkGlobalPos = chunkGlobalPos + vec2(radius, radius);

    ivec2 startCellIndices = static_cast<ivec2>(glm::floor(Util::Map::GetCellIndicesFromAdtPosition(startChunkGlobalPos)));
    ivec2 endCellIndices = static_cast<ivec2>(glm::floor(Util::Map::GetCellIndicesFromAdtPosition(endChunkGlobalPos)));

    vec2 worldPos2D = vec2(worldPos.x, worldPos.z);

    for (i32 y = startCellIndices.y; y <= endCellIndices.y; y++)
    {
        for (i32 x = startCellIndices.x; x <= endCellIndices.x; x++)
        {
            ivec2 globalCellIndices = ivec2(x, y);

            ivec2 numCellsPerStride = ivec2(Terrain::CHUNK_NUM_CELLS_PER_STRIDE, Terrain::CHUNK_NUM_CELLS_PER_STRIDE);

            ivec2 chunkIndices = globalCellIndices / numCellsPerStride;
            ivec2 cellIndices = globalCellIndices - (chunkIndices * numCellsPerStride);

            u32 chunkID = (chunkIndices.x * Terrain::CHUNK_NUM_PER_MAP_STRIDE) + chunkIndices.y;
            u32 cellID = (cellIndices.y * Terrain::CHUNK_NUM_CELLS_PER_STRIDE) + cellIndices.x;

            u32 packedChunkCellID = (chunkID << 16) | (cellID & 0xffff);

            if (!_terrainRenderer._packedChunkCellIDToGlobalCellID.contains(packedChunkCellID))
            {
                NC_LOG_ERROR("Shit is bad yo");
            }

            u32 globalCellIndex = _terrainRenderer._packedChunkCellIDToGlobalCellID[packedChunkCellID];
            u32 chunkIndex = globalCellIndex / Terrain::CHUNK_NUM_CELLS;

            u32 vertexOffset = globalCellIndex * Terrain::CELL_NUM_VERTICES;

            for (u32 i = 0; i < Terrain::CELL_NUM_VERTICES; i++)
            {
                vec2 pos = Util::Map::GetGlobalVertexPosition(chunkID, cellID, i);

                f32 distance = glm::distance(pos, worldPos2D);
                if (distance < radius)
                {
                    VertexData& vertexData = outVertexDatas.emplace_back();
                    vertexData.vertexID = vertexOffset + i;
                    vertexData.chunkID = chunkID;
                    vertexData.localCellID = cellID;
                    vertexData.cellHeightRangeID = (chunkIndex * Terrain::CHUNK_NUM_CELLS) + cellID;

                    switch (hardnessMode)
                    {
                        case Editor::TerrainTools::HardnessMode::LINEAR:
                        {
                            vertexData.hardness = CalculateLinearHardness(distance, radius, hardness);
                            break;
                        }
                        case Editor::TerrainTools::HardnessMode::QUADRATIC:
                        {
                            vertexData.hardness = CalculateQuadraticHardness(distance, radius, hardness);
                            break;
                        }
                        case Editor::TerrainTools::HardnessMode::GAUSSIAN:
                        {
                            vertexData.hardness = CalculateGaussianHardness(distance, radius, hardness);
                            break;
                        }
                        case Editor::TerrainTools::HardnessMode::EXPONENTIAL:
                        {
                            vertexData.hardness = CalculateExponentialHardness(distance, radius, hardness);
                            break;
                        }
                        case Editor::TerrainTools::HardnessMode::SMOOTHSTEP:
                        {
                            vertexData.hardness = CalculateSmoothstepHardness(distance, radius, hardness);
                            break;
                        }
                    }
                }
            }
        }
    }
}
