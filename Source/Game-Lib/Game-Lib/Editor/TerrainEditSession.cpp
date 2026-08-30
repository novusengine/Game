#include "TerrainEditSession.h"

#include "Game-Lib/Application/EnttRegistries.h"
#include "Game-Lib/ECS/Components/Camera.h"
#include "Game-Lib/ECS/Singletons/ActiveCamera.h"
#include "Game-Lib/ECS/Singletons/UISingleton.h"
#include "Game-Lib/ECS/Util/Transforms.h"
#include "Game-Lib/Rendering/Debug/DebugRenderer.h"
#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Rendering/Terrain/TerrainRenderer.h"
#include "Game-Lib/Util/AssetPath.h"
#include "Game-Lib/Util/AssetWriter.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Base/CVarSystem/CVarSystem.h>
#include <FileFormat/Novus/Map/MapChunk.h>
#include <Filesystem/PactStorage.h>
#include <Input/InputSystem.h>
#include <Renderer/Renderer.h>

#include <enkiTS/TaskScheduler.h>
#include <entt/entt.hpp>
#include <gli/gli.hpp>
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <tracy/Tracy.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <limits>

namespace Editor
{
    namespace
    {
        constexpr f32 MAX_RAY_DISTANCE = 20000.0f;
        constexpr f32 MIN_BRUSH_RADIUS = 0.25f;
        constexpr f32 MAX_BRUSH_RADIUS = 500.0f;
        constexpr f32 MAX_BRUSH_STRENGTH = 1000.0f;
        constexpr f32 MAX_EDIT_DELTA_TIME = 0.1f;
        constexpr f32 HEIGHT_EPSILON = 0.00001f;
        constexpr u32 MAX_DABS_PER_SAMPLE = 64;
        constexpr u32 ALPHA_MAP_RESOLUTION = 64;
        constexpr u32 ALPHA_MAP_CHANNEL_COUNT = 4;
        constexpr u32 ALPHA_MAP_TEXEL_COUNT = ALPHA_MAP_RESOLUTION * ALPHA_MAP_RESOLUTION;
        constexpr u32 ALPHA_MAP_CELL_BYTE_SIZE = ALPHA_MAP_TEXEL_COUNT * ALPHA_MAP_CHANNEL_COUNT;

        u32 GetLayerCount(const u64* layers)
        {
            u32 count = 0;
            while (count < Map::CellsData::CELL_LAYER_COUNT && layers[count] != 0 && layers[count] != Terrain::TEXTURE_ID_INVALID)
                count++;

            return count;
        }

        std::array<u8, Map::CellsData::CELL_LAYER_COUNT> DecodeLayerWeights(const u8* texel)
        {
            const u32 blendedWeight = static_cast<u32>(texel[0]) + texel[1] + texel[2];
            if (blendedWeight <= 255)
            {
                return {
                    static_cast<u8>(255 - blendedWeight),
                    texel[0],
                    texel[1],
                    texel[2]
                };
            }

            std::array<u8, Map::CellsData::CELL_LAYER_COUNT> weights = { 0, 0, 0, 0 };
            u32 assignedWeight = 0;
            u32 lastWeightedLayer = 1;
            for (u32 layerIndex = 1; layerIndex < Map::CellsData::CELL_LAYER_COUNT; layerIndex++)
            {
                const u32 scaledWeight = static_cast<u32>(texel[layerIndex - 1]) * 255u / blendedWeight;
                weights[layerIndex] = static_cast<u8>(scaledWeight);
                assignedWeight += scaledWeight;
                if (texel[layerIndex - 1] != 0)
                    lastWeightedLayer = layerIndex;
            }
            weights[lastWeightedLayer] = static_cast<u8>(weights[lastWeightedLayer] + (255u - assignedWeight));
            return weights;
        }

        u8 DecodeLayerWeight(const u8* texel, u32 layerIndex)
        {
            const u32 blendedWeight = static_cast<u32>(texel[0]) + texel[1] + texel[2];
            if (blendedWeight <= 255)
                return layerIndex == 0 ? static_cast<u8>(255 - blendedWeight) : texel[layerIndex - 1];

            return DecodeLayerWeights(texel)[layerIndex];
        }

        void EncodeLayerWeights(const std::array<u8, Map::CellsData::CELL_LAYER_COUNT>& weights, u8* texel)
        {
            texel[0] = weights[1];
            texel[1] = weights[2];
            texel[2] = weights[3];
            texel[3] = 255;
        }

        std::array<u8, 4> DecodeRGB565(u16 packedColor)
        {
            const u32 red = (packedColor >> 11) & 0x1f;
            const u32 green = (packedColor >> 5) & 0x3f;
            const u32 blue = packedColor & 0x1f;
            return {
                static_cast<u8>((red << 3) | (red >> 2)),
                static_cast<u8>((green << 2) | (green >> 4)),
                static_cast<u8>((blue << 3) | (blue >> 2)),
                255
            };
        }

        bool DecodeAlphaMapTexture(const gli::texture3d& source, std::vector<u8>& outRGBA)
        {
            if (source.empty() || source.extent().x != ALPHA_MAP_RESOLUTION || source.extent().y != ALPHA_MAP_RESOLUTION || source.extent().z != Terrain::CHUNK_NUM_CELLS)
                return false;

            if (source.format() == gli::FORMAT_RGBA8_UNORM_PACK8 || source.format() == gli::FORMAT_BGRA8_UNORM_PACK8)
            {
                if (source.size() != outRGBA.size())
                    return false;

                std::memcpy(outRGBA.data(), source.data(), outRGBA.size());
                if (source.format() == gli::FORMAT_BGRA8_UNORM_PACK8)
                {
                    for (size_t byteOffset = 0; byteOffset < outRGBA.size(); byteOffset += ALPHA_MAP_CHANNEL_COUNT)
                        std::swap(outRGBA[byteOffset], outRGBA[byteOffset + 2]);
                }

                return true;
            }

            if (source.format() != gli::FORMAT_RGB_DXT1_UNORM_BLOCK8 && source.format() != gli::FORMAT_RGBA_DXT1_UNORM_BLOCK8)
                return false;

            constexpr u32 BLOCK_SIZE = 4;
            constexpr u32 BLOCKS_PER_ROW = ALPHA_MAP_RESOLUTION / BLOCK_SIZE;
            constexpr u32 BLOCKS_PER_LAYER = BLOCKS_PER_ROW * BLOCKS_PER_ROW;
            constexpr u32 BLOCK_BYTE_SIZE = 8;
            for (u32 layer = 0; layer < Terrain::CHUNK_NUM_CELLS; layer++)
            {
                const u8* blocks = source.data<u8>(0, 0, 0) + static_cast<size_t>(layer) * BLOCKS_PER_LAYER * BLOCK_BYTE_SIZE;
                for (u32 blockY = 0; blockY < BLOCKS_PER_ROW; blockY++)
                {
                    for (u32 blockX = 0; blockX < BLOCKS_PER_ROW; blockX++)
                    {
                        const u8* block = blocks + static_cast<size_t>(blockX + blockY * BLOCKS_PER_ROW) * BLOCK_BYTE_SIZE;
                        const u16 packedColor0 = static_cast<u16>(block[0]) | static_cast<u16>(block[1] << 8);
                        const u16 packedColor1 = static_cast<u16>(block[2]) | static_cast<u16>(block[3] << 8);
                        std::array<std::array<u8, 4>, 4> colors = { DecodeRGB565(packedColor0), DecodeRGB565(packedColor1), {}, {} };
                        if (packedColor0 > packedColor1 || source.format() == gli::FORMAT_RGB_DXT1_UNORM_BLOCK8)
                        {
                            for (u32 channel = 0; channel < 3; channel++)
                            {
                                colors[2][channel] = static_cast<u8>((2u * colors[0][channel] + colors[1][channel]) / 3u);
                                colors[3][channel] = static_cast<u8>((colors[0][channel] + 2u * colors[1][channel]) / 3u);
                            }
                            colors[2][3] = 255;
                            colors[3][3] = 255;
                        }
                        else
                        {
                            for (u32 channel = 0; channel < 3; channel++)
                                colors[2][channel] = static_cast<u8>((colors[0][channel] + colors[1][channel]) / 2u);
                            colors[2][3] = 255;
                            colors[3] = { 0, 0, 0, 0 };
                        }

                        const u32 colorIndices = static_cast<u32>(block[4]) |
                            (static_cast<u32>(block[5]) << 8) |
                            (static_cast<u32>(block[6]) << 16) |
                            (static_cast<u32>(block[7]) << 24);
                        for (u32 pixelY = 0; pixelY < BLOCK_SIZE; pixelY++)
                        {
                            for (u32 pixelX = 0; pixelX < BLOCK_SIZE; pixelX++)
                            {
                                const u32 pixelInBlock = pixelX + pixelY * BLOCK_SIZE;
                                const u32 colorIndex = (colorIndices >> (pixelInBlock * 2)) & 0x3;
                                const u32 texelX = blockX * BLOCK_SIZE + pixelX;
                                const u32 texelY = blockY * BLOCK_SIZE + pixelY;
                                const size_t destination = static_cast<size_t>(layer) * ALPHA_MAP_CELL_BYTE_SIZE + static_cast<size_t>(texelX + texelY * ALPHA_MAP_RESOLUTION) * ALPHA_MAP_CHANNEL_COUNT;
                                std::copy(colors[colorIndex].begin(), colors[colorIndex].end(), outRGBA.begin() + destination);
                                outRGBA[destination + 3] = 255;
                            }
                        }
                    }
                }
            }

            return true;
        }

        struct Ray
        {
        public:
            vec3 origin = vec3(0.0f);
            vec3 direction = vec3(0.0f);
            f32 length = 0.0f;
        };

        vec3 UnprojectNDC(const vec3& ndc, const mat4x4& clipToWorld)
        {
            const vec4 world = clipToWorld * vec4(ndc, 1.0f);
            return vec3(world) / world.w;
        }

        bool IntersectRayAABB(const Ray& ray, const vec3& boundsMin, const vec3& boundsMax, f32& outNearDistance, f32& outFarDistance)
        {
            f32 nearDistance = 0.0f;
            f32 farDistance = ray.length;
            for (u32 axis = 0; axis < 3; axis++)
            {
                if (glm::abs(ray.direction[axis]) <= std::numeric_limits<f32>::epsilon())
                {
                    if (ray.origin[axis] < boundsMin[axis] || ray.origin[axis] > boundsMax[axis])
                        return false;

                    continue;
                }

                const f32 inverseDirection = 1.0f / ray.direction[axis];
                f32 distance0 = (boundsMin[axis] - ray.origin[axis]) * inverseDirection;
                f32 distance1 = (boundsMax[axis] - ray.origin[axis]) * inverseDirection;
                if (distance0 > distance1)
                    std::swap(distance0, distance1);

                nearDistance = glm::max(nearDistance, distance0);
                farDistance = glm::min(farDistance, distance1);
                if (nearDistance > farDistance)
                    return false;
            }

            outNearDistance = nearDistance;
            outFarDistance = farDistance;
            return true;
        }

        bool MakeCursorRay(Ray& outRay)
        {
            InputSystem* inputSystem = ServiceLocator::GetInputSystem();
            GameRenderer* gameRenderer = ServiceLocator::GetGameRenderer();
            EnttRegistries* registries = ServiceLocator::GetEnttRegistries();
            if (!inputSystem || !gameRenderer || !registries || !registries->gameRegistry || !registries->uiRegistry)
                return false;

            if (inputSystem->IsMouseCaptured() && !gameRenderer->IsPointerCaptureActive())
                return false;

            entt::registry& registry = *registries->gameRegistry;
            auto* ui = registries->uiRegistry->ctx().find<ECS::Singletons::UISingleton>();
            if (ui && !ui->allHoveredEntities.empty())
                return false;

            auto* activeCamera = registry.ctx().find<ECS::Singletons::ActiveCamera>();
            if (!activeCamera || activeCamera->entity == entt::null || !registry.valid(activeCamera->entity))
                return false;

            const ECS::Components::Camera* camera = registry.try_get<ECS::Components::Camera>(activeCamera->entity);
            if (!camera)
                return false;

            const vec2 renderSize = gameRenderer->GetRenderer()->GetRenderSize();
            if (renderSize.x <= 0.0f || renderSize.y <= 0.0f)
                return false;

            const vec2 mousePosition = inputSystem->GetMousePosition();
            const vec2 ndcPosition = {
                (2.0f * mousePosition.x) / renderSize.x - 1.0f,
                1.0f - (2.0f * mousePosition.y) / renderSize.y
            };

            const vec3 nearPosition = UnprojectNDC(vec3(ndcPosition, 1.0f), camera->clipToWorld);
            const vec3 farPosition = UnprojectNDC(vec3(ndcPosition, 0.0f), camera->clipToWorld);
            const vec3 direction = farPosition - nearPosition;
            const f32 length = glm::length(direction);
            if (!std::isfinite(length) || length <= std::numeric_limits<f32>::epsilon())
                return false;

            outRay = { .origin = nearPosition, .direction = direction / length, .length = glm::min(length, MAX_RAY_DISTANCE) };
            return true;
        }

        bool BarycentricHeight(const vec2& point, const std::array<vec2, 3>& positions, const std::array<f32, 3>& heights, f32& outHeight)
        {
            const vec2 edge0 = positions[1] - positions[0];
            const vec2 edge1 = positions[2] - positions[0];
            const vec2 relative = point - positions[0];
            const f32 denominator = edge0.x * edge1.y - edge1.x * edge0.y;
            if (glm::abs(denominator) <= std::numeric_limits<f32>::epsilon())
                return false;

            const f32 weight1 = (relative.x * edge1.y - edge1.x * relative.y) / denominator;
            const f32 weight2 = (edge0.x * relative.y - relative.x * edge0.y) / denominator;
            const f32 weight0 = 1.0f - weight1 - weight2;
            constexpr f32 EDGE_TOLERANCE = -0.0001f;
            if (weight0 < EDGE_TOLERANCE || weight1 < EDGE_TOLERANCE || weight2 < EDGE_TOLERANCE)
                return false;

            outHeight = heights[0] * weight0 + heights[1] * weight1 + heights[2] * weight2;
            return true;
        }
    }

    TerrainEditSession::TerrainEditSession(TerrainLoader& terrainLoader, TerrainRenderer& terrainRenderer, DebugRenderer& debugRenderer)
        : _terrainLoader(terrainLoader)
        , _terrainRenderer(terrainRenderer)
        , _debugRenderer(debugRenderer)
    {
        _loadedChunks.reserve(128);
        _dirtyChunks.reserve(128);
        _unsavedCreatedChunks.reserve(16);
        _physicsDirtyChunks.reserve(128);
        _editablePaintChunkIDs.reserve(16);
        _editableChunkScratch.reserve(8);
        _chunkCellScratch.reserve(8);
        _paintCellScratch.reserve(256);
        _blockedPaintCellScratch.reserve(256);
        _changedCellScratch.reserve(256);
        _affectedCellScratch.reserve(512);
        _transactionChunkScratch.reserve(8);
        _candidateScratch.reserve(32768);
        _newHeightScratch.reserve(32768);
        _sharedVertexHeightScratch.reserve(4096);
        _sharedVertexColorScratch.reserve(4096);
        _paintCellWorkScratch.reserve(256);
        _paintBeforeScratch.reserve(256 * ALPHA_MAP_CELL_BYTE_SIZE);
    }

    void TerrainEditSession::Update(f32)
    {
        const std::string& currentMapName = _terrainLoader.GetCurrentMapInternalName();
        if (currentMapName != _mapName)
            ResetForMapChange(currentMapName);

        RefreshLoadedChunks();
        if (_enabled && (_mapName.empty() || _terrainLoader.IsLoading()))
            SetEnabled(false);

        _hasCursorHit = _enabled && CalculateCursorHit(_cursorPosition);
        if (!_hasCursorHit)
            return;

        constexpr i32 PREVIEW_SEGMENTS = 64;
        vec3 previousPoint;
        bool hasPreviousPoint = false;
        for (i32 segment = 0; segment <= PREVIEW_SEGMENTS; segment++)
        {
            const f32 angle = glm::two_pi<f32>() * static_cast<f32>(segment) / static_cast<f32>(PREVIEW_SEGMENTS);
            vec3 point = _cursorPosition + vec3(glm::cos(angle) * _previewRadius, 0.0f, glm::sin(angle) * _previewRadius);
            f32 height = 0.0f;
            if (!SampleHeight(vec2(point.x, point.z), height))
            {
                hasPreviousPoint = false;
                continue;
            }

            point.y = height + 0.05f;
            if (hasPreviousPoint)
                _debugRenderer.DrawLine3D(previousPoint, point, Color::White);

            previousPoint = point;
            hasPreviousPoint = true;
        }
    }

    bool TerrainEditSession::SetEnabled(bool enabled)
    {
        if (enabled && (_terrainLoader.GetCurrentMapInternalName().empty() || _terrainLoader.IsLoading()))
            return false;

        if (!enabled && _strokeActive)
            CancelStroke();

        _enabled = enabled;
        _hasCursorHit = false;
        _hasLastSample = false;
        return true;
    }

    void TerrainEditSession::SetPreviewRadius(f32 radius)
    {
        if (std::isfinite(radius))
            _previewRadius = glm::clamp(radius, MIN_BRUSH_RADIUS, MAX_BRUSH_RADIUS);
    }

    bool TerrainEditSession::BeginStroke(const std::string& name)
    {
        if (!_enabled || !_hasCursorHit || _strokeActive)
            return false;

        _activeTransaction = {};
        _activeTransaction.name = name.empty() ? "Sculpt Terrain" : name;
        _activeTransaction.deltaLookup.reserve(2048);
        _activeTransaction.colorDeltas.reserve(2048);
        _activeTransaction.colorDeltaLookup.reserve(2048);
        _activeTransaction.textureCellDeltas.reserve(64);
        _activeTransaction.textureCellDeltaLookup.reserve(64);
        _activeTransaction.cellLayerDeltaLookup.reserve(32);
        _activeTransaction.chunkAlphaMapDeltaLookup.reserve(8);
        _activeTransaction.affectedChunkIDs.reserve(8);
        _blockedPaintCellScratch.clear();
        _vertexColorBlendScratch.clear();
        _vertexColorBlendScratch.reserve(2048);
        _strokeActive = true;
        _hasLastSample = false;
        return true;
    }

    bool TerrainEditSession::ApplyStrokeSample(TerrainSculptOperation operation, const vec3& position, f32 radius, f32 strength, f32 hardness, f32 deltaTime, f32 targetHeight)
    {
        if (!_strokeActive || !std::isfinite(radius) || !std::isfinite(strength) || !std::isfinite(hardness) || !std::isfinite(deltaTime) || !std::isfinite(targetHeight))
            return false;

        radius = glm::clamp(radius, MIN_BRUSH_RADIUS, MAX_BRUSH_RADIUS);
        strength = glm::clamp(strength, -MAX_BRUSH_STRENGTH, MAX_BRUSH_STRENGTH);
        hardness = glm::clamp(hardness, 0.0f, 1.0f);
        deltaTime = glm::clamp(deltaTime, 0.0f, MAX_EDIT_DELTA_TIME);
        _previewRadius = radius;

        const f32 spacing = glm::max(radius * 0.12f, Terrain::PATCH_HALF_SIZE);
        const f32 sampleDistance = _hasLastSample ? glm::distance(vec2(_lastSamplePosition.x, _lastSamplePosition.z), vec2(position.x, position.z)) : 0.0f;
        const u32 requestedDabCount = _hasLastSample ? std::max(1u, static_cast<u32>(std::ceil(sampleDistance / spacing))) : 1u;
        const u32 dabCount = glm::min(requestedDabCount, MAX_DABS_PER_SAMPLE);
        const f32 dabDeltaTime = deltaTime / static_cast<f32>(dabCount);

        _changedCellScratch.clear();
        bool changed = false;
        for (u32 dabIndex = 1; dabIndex <= dabCount; dabIndex++)
        {
            const f32 progress = static_cast<f32>(dabIndex) / static_cast<f32>(dabCount);
            const vec3 dabPosition = _hasLastSample ? glm::mix(_lastSamplePosition, position, progress) : position;
            changed |= ApplyDab(operation, dabPosition, radius, strength, hardness, dabDeltaTime, targetHeight, _changedCellScratch);
        }

        if (changed)
            RefreshDerivedTerrain(_changedCellScratch, &_activeTransaction.affectedChunkIDs);

        _lastSamplePosition = position;
        _hasLastSample = true;
        return changed;
    }

    bool TerrainEditSession::SetPaintTexture(const std::string& virtualPath)
    {
        std::string normalizedPath = virtualPath;
        std::replace(normalizedPath.begin(), normalizedPath.end(), '\\', '/');
        std::transform(normalizedPath.begin(), normalizedPath.end(), normalizedPath.begin(), [](unsigned char character)
        {
            return static_cast<char>(std::tolower(character));
        });

        while (!normalizedPath.empty() && normalizedPath.front() == '/')
            normalizedPath.erase(normalizedPath.begin());

        const bool supportedTextureFormat = normalizedPath.ends_with(".dds") || normalizedPath.ends_with(".png");
        if (!normalizedPath.starts_with("texture/") || !supportedTextureFormat || normalizedPath.find("..") != std::string::npos)
            return false;

        PACT::PactStorage* pactStorage = ServiceLocator::GetPactStorage();
        if (!pactStorage || !pactStorage->FileExists(normalizedPath))
            return false;

        _paintTexturePath = std::move(normalizedPath);
        _paintTextureHash = Util::AssetPath::Hash(_paintTexturePath);
        return true;
    }

    bool TerrainEditSession::SetPaintTargetLayer(u32 layerIndex)
    {
        if (_strokeActive || layerIndex > Map::CellsData::CELL_LAYER_COUNT)
            return false;

        _paintTargetLayerIndex = layerIndex;
        return true;
    }

    bool TerrainEditSession::ApplyPaintSample(const vec3& position, f32 radius, f32 pressure, f32 hardness, f32 deltaTime, f32 targetOpacity)
    {
        ZoneScopedN("Terrain Paint Sample");

        if (!_strokeActive || _paintTextureHash == Terrain::TEXTURE_ID_INVALID || !std::isfinite(radius) || !std::isfinite(pressure) || !std::isfinite(hardness) || !std::isfinite(deltaTime) || !std::isfinite(targetOpacity))
            return false;

        radius = glm::clamp(radius, MIN_BRUSH_RADIUS, MAX_BRUSH_RADIUS);
        pressure = glm::clamp(pressure, 0.0f, 1.0f);
        hardness = glm::clamp(hardness, 0.0f, 1.0f);
        deltaTime = glm::clamp(deltaTime, 0.0f, MAX_EDIT_DELTA_TIME);
        targetOpacity = glm::clamp(targetOpacity, 0.0f, 1.0f);
        _previewRadius = radius;
        _blockedPaintCellScratch.clear();
        if (pressure <= 0.0f || deltaTime <= 0.0f)
        {
            _lastSamplePosition = position;
            _hasLastSample = true;
            return false;
        }

        const f32 spacing = glm::max(radius * 0.4f, Terrain::CELL_SIZE / static_cast<f32>(ALPHA_MAP_RESOLUTION));
        const f32 sampleDistance = _hasLastSample ? glm::distance(vec2(_lastSamplePosition.x, _lastSamplePosition.z), vec2(position.x, position.z)) : 0.0f;
        const u32 requestedDabCount = _hasLastSample ? std::max(1u, static_cast<u32>(std::ceil(sampleDistance / spacing))) : 1u;
        const u32 dabCount = glm::min(requestedDabCount, MAX_DABS_PER_SAMPLE);
        const f32 dabDeltaTime = deltaTime / static_cast<f32>(dabCount);
        const bool snapToEndpoint = pressure >= 1.0f - std::numeric_limits<f32>::epsilon() &&
            (targetOpacity <= std::numeric_limits<f32>::epsilon() || targetOpacity >= 1.0f - std::numeric_limits<f32>::epsilon());
        TracyPlot("Terrain Paint Radius", static_cast<i64>(radius));
        TracyPlot("Terrain Paint Requested Dabs", static_cast<i64>(requestedDabCount));
        TracyPlot("Terrain Paint Dabs", static_cast<i64>(dabCount));

        {
            ZoneScopedN("Terrain Paint Blend LUT");
            const f32 blendIndexScale = static_cast<f32>(_paintBlendScratch.size() - 1);
            for (size_t blendIndex = 0; blendIndex < _paintBlendScratch.size(); blendIndex++)
            {
                const f32 normalizedDistance = std::sqrt(static_cast<f32>(blendIndex) / blendIndexScale);
                const f32 falloff = CalculateFalloff(normalizedDistance * radius, radius, hardness);
                _paintBlendScratch[blendIndex] = snapToEndpoint && falloff >= 1.0f - std::numeric_limits<f32>::epsilon()
                    ? 1.0f
                    : 1.0f - std::exp(-pressure * dabDeltaTime * 10.0f * falloff);
            }
        }

        _paintCellScratch.clear();
        bool changed = false;
        for (u32 dabIndex = 1; dabIndex <= dabCount; dabIndex++)
        {
            const f32 progress = static_cast<f32>(dabIndex) / static_cast<f32>(dabCount);
            const vec3 dabPosition = _hasLastSample ? glm::mix(_lastSamplePosition, position, progress) : position;
            changed |= ApplyPaintDab(dabPosition, radius, targetOpacity, _paintCellScratch);
        }
        TracyPlot("Terrain Paint Changed Cells", static_cast<i64>(_paintCellScratch.size()));
        TracyPlot("Terrain Paint Blocked Cells", static_cast<i64>(_blockedPaintCellScratch.size()));

        if (changed)
        {
            UploadPaintChanges(_paintCellScratch);
            for (const auto& change : _paintCellScratch)
                _activeTransaction.affectedChunkIDs.insert(change.first >> 8);
        }

        _lastSamplePosition = position;
        _hasLastSample = true;
        return changed;
    }

    bool TerrainEditSession::ApplyVertexColorSample(const vec3& position, f32 radius, f32 flow, f32 hardness, f32 deltaTime, const vec3& targetColor)
    {
        ZoneScopedN("Terrain Vertex Color Sample");

        if (!_strokeActive || !std::isfinite(radius) || !std::isfinite(flow) || !std::isfinite(hardness) || !std::isfinite(deltaTime) || !std::isfinite(targetColor.x) || !std::isfinite(targetColor.y) || !std::isfinite(targetColor.z))
        {
            return false;
        }

        radius = glm::clamp(radius, MIN_BRUSH_RADIUS, MAX_BRUSH_RADIUS);
        flow = glm::clamp(flow, 0.0f, 1.0f);
        hardness = glm::clamp(hardness, 0.0f, 1.0f);
        deltaTime = glm::clamp(deltaTime, 0.0f, MAX_EDIT_DELTA_TIME);
        const vec3 clampedTargetColor = glm::clamp(targetColor, vec3(0.0f), vec3(1.0f));
        _previewRadius = radius;
        if (flow <= 0.0f || deltaTime <= 0.0f)
        {
            _lastSamplePosition = position;
            _hasLastSample = true;
            return false;
        }

        const f32 spacing = glm::max(radius * 0.12f, Terrain::PATCH_HALF_SIZE);
        const f32 sampleDistance = _hasLastSample ? glm::distance(vec2(_lastSamplePosition.x, _lastSamplePosition.z), vec2(position.x, position.z)) : 0.0f;
        const u32 requestedDabCount = _hasLastSample ? std::max(1u, static_cast<u32>(std::ceil(sampleDistance / spacing))) : 1u;
        const u32 dabCount = glm::min(requestedDabCount, MAX_DABS_PER_SAMPLE);
        const f32 dabDeltaTime = deltaTime / static_cast<f32>(dabCount);

        _changedCellScratch.clear();
        bool changed = false;
        for (u32 dabIndex = 1; dabIndex <= dabCount; dabIndex++)
        {
            const f32 progress = static_cast<f32>(dabIndex) / static_cast<f32>(dabCount);
            const vec3 dabPosition = _hasLastSample ? glm::mix(_lastSamplePosition, position, progress) : position;
            changed |= ApplyVertexColorDab(dabPosition, radius, flow, hardness, dabDeltaTime, clampedTargetColor, _changedCellScratch);
        }

        if (changed)
        {
            UploadChangedVertexCells(_changedCellScratch);
            for (u32 packedCell : _changedCellScratch)
                _activeTransaction.affectedChunkIDs.insert(packedCell >> 8);
        }

        _lastSamplePosition = position;
        _hasLastSample = true;
        return changed;
    }

    void TerrainEditSession::GetCursorTextureLayers(std::vector<TextureLayerState>& outLayers)
    {
        outLayers.clear();
        if (!_hasCursorHit)
            return;

        u32 chunkID = Terrain::CHUNK_INVALID_ID;
        u16 cellID = 0;
        if (!GetCellAtWorldPosition(vec2(_cursorPosition.x, _cursorPosition.z), chunkID, cellID))
            return;

        auto chunkItr = _loadedChunks.find(chunkID);
        if (chunkItr == _loadedChunks.end() || !chunkItr->second.chunk)
            return;

        EditableAlphaMap* alphaMap = GetOrCreateAlphaMap(chunkID);
        if (!alphaMap)
            return;

        const u64* layers = chunkItr->second.chunk->cellsData.layerTextureIDs[cellID];
        const u32 layerCount = GetLayerCount(layers);
        std::array<u64, Map::CellsData::CELL_LAYER_COUNT> weightSums = {};
        const size_t cellOffset = static_cast<size_t>(cellID) * ALPHA_MAP_CELL_BYTE_SIZE;
        for (u32 texelID = 0; texelID < ALPHA_MAP_TEXEL_COUNT; texelID++)
        {
            const auto weights = DecodeLayerWeights(alphaMap->rgba.data() + cellOffset + static_cast<size_t>(texelID) * ALPHA_MAP_CHANNEL_COUNT);
            for (u32 layerIndex = 0; layerIndex < layerCount; layerIndex++)
                weightSums[layerIndex] += weights[layerIndex];
        }

        PACT::PactStorage* pactStorage = ServiceLocator::GetPactStorage();
        outLayers.reserve(layerCount);
        for (u32 layerIndex = 0; layerIndex < layerCount; layerIndex++)
        {
            std::string path;
            if (pactStorage)
                pactStorage->GetFilePath(layers[layerIndex], path);

            outLayers.push_back({ .path = std::move(path), .textureHash = layers[layerIndex], .layerIndex = layerIndex, .averageWeight = static_cast<f32>(weightSums[layerIndex]) / static_cast<f32>(ALPHA_MAP_TEXEL_COUNT * 255u) });
        }
    }

    bool TerrainEditSession::CommitStroke()
    {
        ZoneScopedN("Terrain Edit Commit Stroke");

        if (!_strokeActive)
            return false;

        _strokeActive = false;
        _hasLastSample = false;
        if (_activeTransaction.deltas.empty() && _activeTransaction.colorDeltas.empty() && _activeTransaction.textureCellDeltas.empty() && _activeTransaction.cellLayerDeltas.empty())
        {
            _activeTransaction = {};
            return false;
        }

        for (TextureCellDelta& delta : _activeTransaction.textureCellDeltas)
        {
            auto alphaItr = _editableAlphaMaps.find(delta.chunkID);
            if (alphaItr == _editableAlphaMaps.end())
            {
                delta.after = delta.before;
                continue;
            }

            const size_t cellOffset = static_cast<size_t>(delta.cellID) * ALPHA_MAP_CELL_BYTE_SIZE;
            const u8* cellData = alphaItr->second.rgba.data() + cellOffset;
            delta.after.assign(cellData, cellData + ALPHA_MAP_CELL_BYTE_SIZE);
        }

        MarkTransactionChunksEdited(_activeTransaction);
        const size_t transactionSize = CalculateTransactionSize(_activeTransaction);
        _historyBytes += transactionSize;
        TracyPlot("Terrain Paint History Cells", static_cast<i64>(_activeTransaction.textureCellDeltas.size()));
        TracyPlot("Terrain Paint Transaction Bytes", static_cast<i64>(transactionSize));
        TracyPlot("Terrain Edit History Bytes", static_cast<i64>(_historyBytes));
        _undoHistory.push_back(std::move(_activeTransaction));
        _activeTransaction = {};
        for (const Transaction& transaction : _redoHistory)
            _historyBytes -= std::min(_historyBytes, CalculateTransactionSize(transaction));
        _redoHistory.clear();
        EnforceHistoryBudget();
        return true;
    }

    bool TerrainEditSession::CancelStroke()
    {
        if (!_strokeActive)
            return false;

        ApplyTransaction(_activeTransaction, false);
        _activeTransaction = {};
        _strokeActive = false;
        _hasLastSample = false;
        return true;
    }

    bool TerrainEditSession::Undo()
    {
        if (_strokeActive || _undoHistory.empty())
            return false;

        Transaction transaction = std::move(_undoHistory.back());
        _undoHistory.pop_back();
        ApplyTransaction(transaction, false);
        MarkTransactionChunksEdited(transaction);
        _redoHistory.push_back(std::move(transaction));
        return true;
    }

    bool TerrainEditSession::Redo()
    {
        if (_strokeActive || _redoHistory.empty())
            return false;

        Transaction transaction = std::move(_redoHistory.back());
        _redoHistory.pop_back();
        ApplyTransaction(transaction, true);
        MarkTransactionChunksEdited(transaction);
        _undoHistory.push_back(std::move(transaction));
        return true;
    }

    bool TerrainEditSession::Save()
    {
        if (_strokeActive)
            return false;

        if (_dirtyChunks.empty())
            return _terrainLoader.SaveMapHeader();

        std::vector<u32> chunkIDs(_dirtyChunks.begin(), _dirtyChunks.end());
        std::sort(chunkIDs.begin(), chunkIDs.end());

        robin_hood::unordered_set<u32> alphaReadyChunkIDs;
        const bool savedAllAlphaMaps = SaveAlphaMaps(chunkIDs, alphaReadyChunkIDs);

        std::vector<u32> chunkSaveCandidates;
        chunkSaveCandidates.reserve(chunkIDs.size());
        for (u32 chunkID : chunkIDs)
        {
            auto alphaItr = _editableAlphaMaps.find(chunkID);
            if (alphaItr == _editableAlphaMaps.end() || !alphaItr->second.dirty || alphaReadyChunkIDs.contains(chunkID))
                chunkSaveCandidates.push_back(chunkID);
        }

        std::vector<u32> savedChunkIDs;
        const bool savedAllChunks = _terrainLoader.SaveEditableChunks(chunkSaveCandidates, _physicsDirtyChunks, savedChunkIDs);
        for (u32 chunkID : savedChunkIDs)
        {
            _dirtyChunks.erase(chunkID);
            _unsavedCreatedChunks.erase(chunkID);
            _physicsDirtyChunks.erase(chunkID);
            auto alphaItr = _editableAlphaMaps.find(chunkID);
            if (alphaItr != _editableAlphaMaps.end())
                alphaItr->second.dirty = false;
        }

        const bool savedChunks = savedAllAlphaMaps && savedAllChunks && _dirtyChunks.empty();
        return savedChunks && _terrainLoader.SaveMapHeader();
    }

    void TerrainEditSession::GetChunkLayout(TerrainLoader::ChunkLayoutState& outState) const
    {
        _terrainLoader.GetChunkLayout(outState);
    }

    bool TerrainEditSession::AddChunk(u32 chunkID)
    {
        if (_strokeActive)
            return false;

        bool created = false;
        if (!_terrainLoader.AddChunk(chunkID, created))
            return false;

        if (created)
        {
            _dirtyChunks.insert(chunkID);
            _unsavedCreatedChunks.insert(chunkID);
        }

        return true;
    }

    bool TerrainEditSession::RemoveChunk(u32 chunkID)
    {
        const bool discardingUnsavedCreation = _unsavedCreatedChunks.contains(chunkID);
        if (_strokeActive || (_dirtyChunks.contains(chunkID) && !discardingUnsavedCreation))
            return false;

        if (!_terrainLoader.RemoveChunk(chunkID))
            return false;

        _dirtyChunks.erase(chunkID);
        _unsavedCreatedChunks.erase(chunkID);
        _editablePaintChunkIDs.erase(chunkID);
        _editableAlphaMaps.erase(chunkID);
        _physicsDirtyChunks.erase(chunkID);
        _undoHistory.clear();
        _redoHistory.clear();
        _historyBytes = 0;
        return true;
    }

    bool TerrainEditSession::ResetChunk(u32 chunkID)
    {
        if (_strokeActive || _dirtyChunks.contains(chunkID) || !_terrainLoader.ResetChunk(chunkID))
            return false;

        _editablePaintChunkIDs.erase(chunkID);
        _editableAlphaMaps.erase(chunkID);
        _undoHistory.clear();
        _redoHistory.clear();
        _historyBytes = 0;
        _dirtyChunks.insert(chunkID);
        return true;
    }

    bool TerrainEditSession::GoToChunk(u32 chunkID)
    {
        auto chunkItr = _loadedChunks.find(chunkID);
        if (chunkItr == _loadedChunks.end() || !chunkItr->second.chunk)
            return false;

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& activeCamera = registry->ctx().get<ECS::Singletons::ActiveCamera>();
        if (activeCamera.entity == entt::null || !registry->all_of<ECS::Components::Transform>(activeCamera.entity))
            return false;

        const u32 chunkX = chunkID % Terrain::CHUNK_NUM_PER_MAP_STRIDE;
        const u32 chunkY = chunkID / Terrain::CHUNK_NUM_PER_MAP_STRIDE;
        const f32 centerX = -Terrain::MAP_HALF_SIZE + (static_cast<f32>(chunkX) + 0.5f) * Terrain::CHUNK_SIZE;
        const f32 centerZ = Terrain::MAP_HALF_SIZE - (static_cast<f32>(chunkY) + 0.5f) * Terrain::CHUNK_SIZE;
        const f32 height = chunkItr->second.chunk->heightHeader.gridMaxHeight;
        ECS::TransformSystem::Get(*registry).SetWorldPosition(activeCamera.entity, vec3(centerX, height, centerZ));
        return true;
    }

    bool TerrainEditSession::PreviewHeightFieldImport(const std::string& path, TerrainHeightFieldManifest& outManifest, std::string& outError) const
    {
        if (_mapName.empty() || _terrainLoader.IsLoading())
        {
            outError = "Terrain chunk layout is not available.";
            return false;
        }

        return LoadTerrainHeightFieldManifest(path, outManifest, outError);
    }

    bool TerrainEditSession::ImportHeightField(const std::string& path, f32 minimumHeight, f32 maximumHeight, u32& outImportedChunkCount, std::string& outError)
    {
        outImportedChunkCount = 0;
        if (_strokeActive || _mapName.empty() || _terrainLoader.IsLoading())
        {
            outError = "Terrain cannot be replaced while it is unavailable or a brush stroke is active.";
            return false;
        }

        TerrainHeightFieldData heightField;
        if (!LoadTerrainHeightField(path, heightField, outError))
            return false;

        std::vector<GeneratedTerrainChunk> generatedChunks;
        if (!GenerateTerrainHeightFieldChunks(heightField, minimumHeight, maximumHeight, generatedChunks, outError))
            return false;

        robin_hood::unordered_set<u32> importedChunkIDs;
        importedChunkIDs.reserve(generatedChunks.size());
        for (GeneratedTerrainChunk& generatedChunk : generatedChunks)
        {
            if (!_terrainLoader.ReplaceGeneratedChunk(generatedChunk.chunkID, std::move(generatedChunk.chunk)))
            {
                outError = "Failed to attach generated terrain chunk " + std::to_string(generatedChunk.chunkID) + ". The terrain may be partially replaced; reload the map before retrying.";
                return false;
            }
            importedChunkIDs.insert(generatedChunk.chunkID);
            _dirtyChunks.insert(generatedChunk.chunkID);
            _unsavedCreatedChunks.insert(generatedChunk.chunkID);
        }

        TerrainLoader::ChunkLayoutState previousLayout;
        _terrainLoader.GetChunkLayout(previousLayout);
        for (u32 chunkID : previousLayout.occupiedChunkIDs)
        {
            if (!importedChunkIDs.contains(chunkID) && !_terrainLoader.RemoveChunk(chunkID))
            {
                outError = "Failed to remove an old terrain chunk. The terrain may be partially replaced; reload the map before retrying.";
                return false;
            }
        }

        _dirtyChunks = importedChunkIDs;
        _unsavedCreatedChunks = importedChunkIDs;
        _physicsDirtyChunks.clear();
        _editablePaintChunkIDs.clear();
        _editableChunkScratch.clear();
        _editableAlphaMaps.clear();
        _chunkCellScratch.clear();
        _paintCellScratch.clear();
        _blockedPaintCellScratch.clear();
        _changedCellScratch.clear();
        _affectedCellScratch.clear();
        _transactionChunkScratch.clear();
        _undoHistory.clear();
        _redoHistory.clear();
        _activeTransaction = {};
        _historyBytes = 0;
        _hasLastSample = false;
        RefreshLoadedChunks();

        outImportedChunkCount = static_cast<u32>(importedChunkIDs.size());
        return true;
    }

    TerrainEditSession::State TerrainEditSession::GetState() const
    {
        return {
            .available = !_mapName.empty() && !_terrainLoader.IsLoading() && !_loadedChunks.empty(),
            .layoutAvailable = !_mapName.empty() && !_terrainLoader.IsLoading(),
            .enabled = _enabled,
            .strokeActive = _strokeActive,
            .cursorHit = _hasCursorHit,
            .canUndo = !_undoHistory.empty() && !_strokeActive,
            .canRedo = !_redoHistory.empty() && !_strokeActive,
            .dirtyChunkCount = static_cast<u32>(_dirtyChunks.size()),
            .blockedPaintCellCount = static_cast<u32>(_blockedPaintCellScratch.size()),
            .layoutGeneration = _terrainLoader.GetContentGeneration(),
            .topologyDirty = _terrainLoader.IsMapHeaderDirty(),
            .cursorPosition = _cursorPosition
        };
    }

    void TerrainEditSession::RefreshLoadedChunks()
    {
        const u64 contentGeneration = _terrainLoader.GetContentGeneration();
        if (contentGeneration == _observedContentGeneration)
            return;

        std::vector<TerrainLoader::LoadedChunkView> chunks;
        _terrainLoader.GetLoadedChunks(chunks);
        _loadedChunks.clear();
        _loadedChunks.reserve(chunks.size());
        for (const TerrainLoader::LoadedChunkView& chunk : chunks)
            _loadedChunks[chunk.chunkID] = chunk;
        _editablePaintChunkIDs.clear();

        RefreshLoadedBounds();
        _observedContentGeneration = contentGeneration;
    }

    void TerrainEditSession::RefreshLoadedBounds()
    {
        _hasLoadedBounds = false;
        for (const auto& [chunkID, loadedChunk] : _loadedChunks)
        {
            const TerrainLoader::LoadedChunkView& chunk = loadedChunk;
            if (!chunk.chunk)
                continue;

            const u32 chunkX = chunkID % Terrain::CHUNK_NUM_PER_MAP_STRIDE;
            const u32 chunkY = chunkID / Terrain::CHUNK_NUM_PER_MAP_STRIDE;
            const f32 chunkMinX = -Terrain::MAP_HALF_SIZE + static_cast<f32>(chunkX) * Terrain::CHUNK_SIZE;
            const f32 chunkMaxZ = Terrain::MAP_HALF_SIZE - static_cast<f32>(chunkY) * Terrain::CHUNK_SIZE;
            const vec3 chunkBoundsMin(chunkMinX, chunk.chunk->heightHeader.gridMinHeight, chunkMaxZ - Terrain::CHUNK_SIZE);
            const vec3 chunkBoundsMax(chunkMinX + Terrain::CHUNK_SIZE, chunk.chunk->heightHeader.gridMaxHeight, chunkMaxZ);
            if (!_hasLoadedBounds)
            {
                _loadedBoundsMin = chunkBoundsMin;
                _loadedBoundsMax = chunkBoundsMax;
                _hasLoadedBounds = true;
            }
            else
            {
                _loadedBoundsMin = glm::min(_loadedBoundsMin, chunkBoundsMin);
                _loadedBoundsMax = glm::max(_loadedBoundsMax, chunkBoundsMax);
            }
        }
    }

    void TerrainEditSession::ResetForMapChange(const std::string& mapName)
    {
        if (_strokeActive)
            CancelStroke();

        _enabled = false;
        _mapName = mapName;
        _observedContentGeneration = 0;
        _loadedChunks.clear();
        _editablePaintChunkIDs.clear();
        _editableChunkScratch.clear();
        _editableAlphaMaps.clear();
        _chunkCellScratch.clear();
        _paintCellScratch.clear();
        _changedCellScratch.clear();
        _affectedCellScratch.clear();
        _transactionChunkScratch.clear();
        _dirtyChunks.clear();
        _unsavedCreatedChunks.clear();
        _physicsDirtyChunks.clear();
        _undoHistory.clear();
        _redoHistory.clear();
        _activeTransaction = {};
        _paintTextureHash = Terrain::TEXTURE_ID_INVALID;
        _paintTexturePath.clear();
        _blockedPaintCellScratch.clear();
        _historyBytes = 0;
        _hasCursorHit = false;
        _hasLastSample = false;
        _hasLoadedBounds = false;
    }

    bool TerrainEditSession::CalculateCursorHit(vec3& outPosition) const
    {
        Ray ray;
        if (!MakeCursorRay(ray) || !_hasLoadedBounds)
            return false;

        const f32 step = Terrain::PATCH_HALF_SIZE;
        f32 nearDistance = 0.0f;
        f32 farDistance = 0.0f;
        if (!IntersectRayAABB(ray, _loadedBoundsMin, _loadedBoundsMax, nearDistance, farDistance))
            return false;

        nearDistance = glm::max(nearDistance - step, 0.0f);
        farDistance = glm::min(farDistance + step, ray.length);
        f32 previousDistance = 0.0f;
        f32 previousDifference = 0.0f;
        bool hadPreviousSample = false;

        for (f32 distance = nearDistance; distance <= farDistance; distance += step)
        {
            const vec3 point = ray.origin + ray.direction * distance;
            f32 terrainHeight = 0.0f;
            if (!SampleHeight(vec2(point.x, point.z), terrainHeight))
            {
                hadPreviousSample = false;
                continue;
            }

            const f32 difference = point.y - terrainHeight;
            if (hadPreviousSample && previousDifference > 0.0f && difference <= 0.0f)
            {
                f32 low = previousDistance;
                f32 high = distance;
                for (u32 iteration = 0; iteration < 8; iteration++)
                {
                    const f32 middle = (low + high) * 0.5f;
                    const vec3 middlePoint = ray.origin + ray.direction * middle;
                    f32 middleHeight = 0.0f;
                    if (!SampleHeight(vec2(middlePoint.x, middlePoint.z), middleHeight) || middlePoint.y > middleHeight)
                        low = middle;
                    else
                        high = middle;
                }

                outPosition = ray.origin + ray.direction * high;
                if (SampleHeight(vec2(outPosition.x, outPosition.z), terrainHeight))
                    outPosition.y = terrainHeight;
                return true;
            }

            previousDistance = distance;
            previousDifference = difference;
            hadPreviousSample = true;
        }

        return false;
    }

    bool TerrainEditSession::SampleHeight(const vec2& worldPosition, f32& outHeight) const
    {
        const vec2 mapPosition = vec2(Terrain::MAP_HALF_SIZE + worldPosition.x, Terrain::MAP_HALF_SIZE - worldPosition.y);
        if (mapPosition.x < 0.0f || mapPosition.y < 0.0f || mapPosition.x >= Terrain::MAP_SIZE || mapPosition.y >= Terrain::MAP_SIZE)
            return false;

        const ivec2 globalCell = ivec2(glm::floor(mapPosition / Terrain::CELL_SIZE));
        const ivec2 chunkPosition = globalCell / static_cast<i32>(Terrain::CHUNK_NUM_CELLS_PER_STRIDE);
        const ivec2 localCell = globalCell - chunkPosition * static_cast<i32>(Terrain::CHUNK_NUM_CELLS_PER_STRIDE);
        const u32 chunkID = static_cast<u32>(chunkPosition.x + chunkPosition.y * static_cast<i32>(Terrain::CHUNK_NUM_PER_MAP_STRIDE));
        const u16 cellID = static_cast<u16>(localCell.x + localCell.y * static_cast<i32>(Terrain::CHUNK_NUM_CELLS_PER_STRIDE));

        auto chunkItr = _loadedChunks.find(chunkID);
        if (chunkItr == _loadedChunks.end() || !chunkItr->second.chunk)
            return false;

        const vec2 localPosition = mapPosition - vec2(globalCell) * Terrain::CELL_SIZE;
        const ivec2 patchPosition = glm::clamp(ivec2(glm::floor(localPosition / Terrain::PATCH_SIZE)), ivec2(0), ivec2(Terrain::CELL_NUM_PATCHES_PER_STRIDE - 1));
        const u16 topLeft = static_cast<u16>(patchPosition.x + patchPosition.y * Terrain::CELL_GRID_ROW_SIZE);
        const std::array<u16, 5> vertexIDs = {
            topLeft,
            static_cast<u16>(topLeft + 1),
            static_cast<u16>(topLeft + Terrain::CELL_GRID_ROW_SIZE),
            static_cast<u16>(topLeft + Terrain::CELL_GRID_ROW_SIZE + 1),
            static_cast<u16>(topLeft + Terrain::CELL_OUTER_GRID_STRIDE)
        };
        constexpr std::array<std::array<u32, 3>, 4> TRIANGLES = {{
            {{ 4, 1, 0 }},
            {{ 4, 0, 2 }},
            {{ 4, 2, 3 }},
            {{ 4, 3, 1 }}
        }};

        const Map::Chunk& chunk = *chunkItr->second.chunk;
        for (const std::array<u32, 3>& triangle : TRIANGLES)
        {
            std::array<vec2, 3> positions;
            std::array<f32, 3> heights;
            for (u32 index = 0; index < 3; index++)
            {
                const u16 vertexID = vertexIDs[triangle[index]];
                positions[index] = GetVertexWorldPosition(chunkID, cellID, vertexID);
                heights[index] = chunk.cellsData.heightField[cellID][vertexID];
            }

            if (BarycentricHeight(worldPosition, positions, heights, outHeight))
                return true;
        }

        return false;
    }

    bool TerrainEditSession::ApplyDab(TerrainSculptOperation operation, const vec3& position, f32 radius, f32 strength, f32 hardness, f32 deltaTime, f32 targetHeight, robin_hood::unordered_set<u32>& outChangedCells)
    {
        GatherVertices(vec2(position.x, position.z), radius, _candidateScratch);
        if (_candidateScratch.empty())
            return false;

        _editableChunkScratch.clear();
        _newHeightScratch.resize(_candidateScratch.size());
        std::fill(_newHeightScratch.begin(), _newHeightScratch.end(), std::numeric_limits<f32>::quiet_NaN());

        for (u32 index = 0; index < _candidateScratch.size(); index++)
        {
            const VertexCandidate& candidate = _candidateScratch[index];
            auto editableItr = _editableChunkScratch.find(candidate.address.chunkID);
            if (editableItr == _editableChunkScratch.end())
            {
                TerrainLoader::LoadedChunkView editableChunk;
                if (!_terrainLoader.GetEditableChunk(candidate.address.chunkID, editableChunk))
                    continue;

                _loadedChunks[editableChunk.chunkID] = editableChunk;
                editableItr = _editableChunkScratch.emplace(editableChunk.chunkID, editableChunk).first;
            }

            Map::Chunk& chunk = *editableItr->second.chunk;
            const f32 oldHeight = chunk.cellsData.heightField[candidate.address.cellID][candidate.address.vertexID];
            const f32 falloff = CalculateFalloff(candidate.distance, radius, hardness);
            f32 newHeight = oldHeight;

            switch (operation)
            {
                case TerrainSculptOperation::AddHeight:
                    newHeight = oldHeight + strength * deltaTime * falloff;
                    break;
                case TerrainSculptOperation::Flatten:
                {
                    const f32 blend = 1.0f - std::exp(-glm::abs(strength) * deltaTime * falloff);
                    newHeight = glm::mix(oldHeight, targetHeight, blend);
                    break;
                }
                case TerrainSculptOperation::Smooth:
                {
                    const u16 packedX = candidate.address.vertexID % Terrain::CELL_GRID_ROW_SIZE;
                    const u16 packedY = candidate.address.vertexID / Terrain::CELL_GRID_ROW_SIZE;
                    const bool innerVertex = packedX >= Terrain::CELL_OUTER_GRID_STRIDE;
                    f32 smoothedHeight = oldHeight;
                    if (innerVertex)
                    {
                        const u16 patchX = packedX - Terrain::CELL_OUTER_GRID_STRIDE;
                        const u16 topLeft = patchX + packedY * Terrain::CELL_GRID_ROW_SIZE;
                        const std::array<u16, 4> cornerVertexIDs = {
                            topLeft,
                            static_cast<u16>(topLeft + 1),
                            static_cast<u16>(topLeft + Terrain::CELL_GRID_ROW_SIZE),
                            static_cast<u16>(topLeft + Terrain::CELL_GRID_ROW_SIZE + 1)
                        };

                        f32 cornerHeightSum = 0.0f;
                        for (u16 cornerVertexID : cornerVertexIDs)
                        {
                            cornerHeightSum += chunk.cellsData.heightField[candidate.address.cellID][cornerVertexID];
                        }
                        smoothedHeight = cornerHeightSum / static_cast<f32>(cornerVertexIDs.size());
                    }
                    else
                    {
                        f32 heightSum = oldHeight;
                        u32 sampleCount = 1;
                        constexpr f32 SAMPLE_OFFSET = Terrain::PATCH_SIZE;
                        const std::array<vec2, 4> offsets = {
                            vec2(-SAMPLE_OFFSET, 0.0f), vec2(SAMPLE_OFFSET, 0.0f), vec2(0.0f, -SAMPLE_OFFSET), vec2(0.0f, SAMPLE_OFFSET)
                        };
                        for (const vec2& offset : offsets)
                        {
                            f32 neighborHeight = 0.0f;
                            if (SampleHeight(candidate.position + offset, neighborHeight))
                            {
                                heightSum += neighborHeight;
                                sampleCount++;
                            }
                        }
                        smoothedHeight = heightSum / static_cast<f32>(sampleCount);
                    }

                    const f32 blend = 1.0f - std::exp(-glm::abs(strength) * deltaTime * falloff);
                    newHeight = glm::mix(oldHeight, smoothedHeight, blend);
                    break;
                }
            }

            _newHeightScratch[index] = newHeight;
        }

        bool changed = false;
        for (u32 index = 0; index < _candidateScratch.size(); index++)
        {
            const VertexCandidate& candidate = _candidateScratch[index];
            if (!std::isfinite(_newHeightScratch[index]))
                continue;

            auto editableItr = _editableChunkScratch.find(candidate.address.chunkID);
            if (editableItr == _editableChunkScratch.end())
                continue;

            Map::Chunk& chunk = *editableItr->second.chunk;
            f32& height = chunk.cellsData.heightField[candidate.address.cellID][candidate.address.vertexID];
            if (glm::abs(_newHeightScratch[index] - height) <= HEIGHT_EPSILON)
                continue;

            RecordBeforeChange(candidate.address, height);
            height = _newHeightScratch[index];
            _activeTransaction.deltas[_activeTransaction.deltaLookup[PackVertexAddress(candidate.address)]].after = height;
            outChangedCells.insert(PackCellAddress(candidate.address.chunkID, candidate.address.cellID));
            changed = true;
        }

        if (changed)
            SynchronizeSharedOuterVertices(outChangedCells);

        return changed;
    }

    bool TerrainEditSession::ApplyVertexColorDab(const vec3& position, f32 radius, f32 flow, f32 hardness, f32 deltaTime, const vec3& targetColor, robin_hood::unordered_set<u32>& outChangedCells)
    {
        GatherVertices(vec2(position.x, position.z), radius, _candidateScratch);
        if (_candidateScratch.empty())
            return false;

        _editableChunkScratch.clear();
        const std::array<f32, 3> target = { targetColor.r * 255.0f, targetColor.g * 255.0f, targetColor.b * 255.0f };
        bool changed = false;
        for (const VertexCandidate& candidate : _candidateScratch)
        {
            auto editableItr = _editableChunkScratch.find(candidate.address.chunkID);
            if (editableItr == _editableChunkScratch.end())
            {
                TerrainLoader::LoadedChunkView editableChunk;
                if (!_terrainLoader.GetEditableChunk(candidate.address.chunkID, editableChunk))
                    continue;

                _loadedChunks[editableChunk.chunkID] = editableChunk;
                editableItr = _editableChunkScratch.emplace(editableChunk.chunkID, editableChunk).first;
            }

            u8* color = editableItr->second.chunk->cellsData.colors[candidate.address.cellID][candidate.address.vertexID];
            const u64 vertexKey = PackVertexAddress(candidate.address);
            auto [blendItr, inserted] = _vertexColorBlendScratch.try_emplace(vertexKey);
            if (inserted)
            {
                for (u32 channel = 0; channel < blendItr->second.size(); channel++)
                    blendItr->second[channel] = static_cast<f32>(color[channel]);
            }

            const f32 falloff = CalculateFalloff(candidate.distance, radius, hardness);
            const f32 blend = 1.0f - std::exp(-flow * deltaTime * 10.0f * falloff);
            std::array<u8, 3> newColor;
            bool vertexChanged = false;
            for (u32 channel = 0; channel < newColor.size(); channel++)
            {
                f32& blendedColor = blendItr->second[channel];
                blendedColor = glm::mix(blendedColor, target[channel], blend);
                newColor[channel] = static_cast<u8>(glm::round(blendedColor));
                vertexChanged |= newColor[channel] != color[channel];
            }
            if (!vertexChanged)
                continue;

            RecordVertexColorBeforeChange(candidate.address, color);
            std::copy(newColor.begin(), newColor.end(), color);
            _activeTransaction.colorDeltas[_activeTransaction.colorDeltaLookup[PackVertexAddress(candidate.address)]].after = newColor;
            outChangedCells.insert(PackCellAddress(candidate.address.chunkID, candidate.address.cellID));
            changed = true;
        }

        if (changed)
            SynchronizeSharedVertexColors(outChangedCells);
        return changed;
    }

    bool TerrainEditSession::ApplyPaintDab(const vec3& position, f32 radius, f32 targetOpacity, robin_hood::unordered_map<u32, PaintCellChange>& outChangedCells)
    {
        ZoneScopedN("Terrain Paint Dab");

        const vec2 center(position.x, position.z);
        const vec2 centerInMap(Terrain::MAP_HALF_SIZE + center.x, Terrain::MAP_HALF_SIZE - center.y);
        const i32 mapCellStride = static_cast<i32>(Terrain::CHUNK_NUM_PER_MAP_STRIDE * Terrain::CHUNK_NUM_CELLS_PER_STRIDE);
        const ivec2 minCell = glm::clamp(ivec2(glm::floor((centerInMap - vec2(radius)) / Terrain::CELL_SIZE)), ivec2(0), ivec2(mapCellStride - 1));
        const ivec2 maxCell = glm::clamp(ivec2(glm::floor((centerInMap + vec2(radius)) / Terrain::CELL_SIZE)), ivec2(0), ivec2(mapCellStride - 1));
        const f32 targetWeight = targetOpacity * 255.0f;
        const f32 texelSize = Terrain::CELL_SIZE / static_cast<f32>(ALPHA_MAP_RESOLUTION);
        const f32 radiusSquared = radius * radius;
        const f32 blendIndexScale = static_cast<f32>(_paintBlendScratch.size() - 1);
        _paintCellWorkScratch.clear();
        _paintBeforeScratch.clear();

        {
            ZoneScopedN("Terrain Paint Gather Cells");

            for (i32 globalCellY = minCell.y; globalCellY <= maxCell.y; globalCellY++)
            {
                for (i32 globalCellX = minCell.x; globalCellX <= maxCell.x; globalCellX++)
                {
                    const u32 chunkID = static_cast<u32>((globalCellX / static_cast<i32>(Terrain::CHUNK_NUM_CELLS_PER_STRIDE)) + (globalCellY / static_cast<i32>(Terrain::CHUNK_NUM_CELLS_PER_STRIDE)) * static_cast<i32>(Terrain::CHUNK_NUM_PER_MAP_STRIDE));
                    auto loadedChunkItr = _loadedChunks.find(chunkID);
                    if (loadedChunkItr == _loadedChunks.end())
                        continue;

                    const u16 cellID = static_cast<u16>((globalCellX % static_cast<i32>(Terrain::CHUNK_NUM_CELLS_PER_STRIDE)) + (globalCellY % static_cast<i32>(Terrain::CHUNK_NUM_CELLS_PER_STRIDE)) * static_cast<i32>(Terrain::CHUNK_NUM_CELLS_PER_STRIDE));
                    const f32 cellMinWorldX = -Terrain::MAP_HALF_SIZE + static_cast<f32>(globalCellX) * Terrain::CELL_SIZE;
                    const f32 cellMaxWorldZ = Terrain::MAP_HALF_SIZE - static_cast<f32>(globalCellY) * Terrain::CELL_SIZE;

                    const i32 minTexelX = glm::max(0, static_cast<i32>(std::ceil((center.x - radius - cellMinWorldX) / texelSize - 0.5f)));
                    const i32 maxTexelX = glm::min(static_cast<i32>(ALPHA_MAP_RESOLUTION) - 1, static_cast<i32>(std::floor((center.x + radius - cellMinWorldX) / texelSize - 0.5f)));
                    const i32 minTexelY = glm::max(0, static_cast<i32>(std::ceil((cellMaxWorldZ - center.y - radius) / texelSize - 0.5f)));
                    const i32 maxTexelY = glm::min(static_cast<i32>(ALPHA_MAP_RESOLUTION) - 1, static_cast<i32>(std::floor((cellMaxWorldZ - center.y + radius) / texelSize - 0.5f)));
                    if (minTexelX > maxTexelX || minTexelY > maxTexelY)
                        continue;

                    const i32 closestTexelX = glm::clamp(static_cast<i32>(std::round((center.x - cellMinWorldX) / texelSize - 0.5f)), minTexelX, maxTexelX);
                    const i32 closestTexelY = glm::clamp(static_cast<i32>(std::round((cellMaxWorldZ - center.y) / texelSize - 0.5f)), minTexelY, maxTexelY);
                    const vec2 closestTexelPosition(cellMinWorldX + (static_cast<f32>(closestTexelX) + 0.5f) * texelSize, cellMaxWorldZ - (static_cast<f32>(closestTexelY) + 0.5f) * texelSize);
                    const vec2 closestTexelOffset = closestTexelPosition - center;
                    if (glm::dot(closestTexelOffset, closestTexelOffset) > radiusSquared)
                        continue;

                    TerrainLoader::LoadedChunkView editableChunk = loadedChunkItr->second;
                    if (!_editablePaintChunkIDs.contains(chunkID))
                    {
                        if (!_terrainLoader.GetEditableChunk(chunkID, editableChunk))
                            continue;

                        _loadedChunks[chunkID] = editableChunk;
                        _editablePaintChunkIDs.insert(chunkID);
                    }

                    Map::Chunk& chunk = *editableChunk.chunk;
                    EditableAlphaMap* alphaMap = GetOrCreateAlphaMap(chunkID);
                    if (!alphaMap)
                        continue;

                    const std::array<u64, Map::CellsData::CELL_LAYER_COUNT> layersBefore = {
                        chunk.cellsData.layerTextureIDs[cellID][0],
                        chunk.cellsData.layerTextureIDs[cellID][1],
                        chunk.cellsData.layerTextureIDs[cellID][2],
                        chunk.cellsData.layerTextureIDs[cellID][3]
                    };

                    const bool textureAlreadyPresent = std::find(layersBefore.begin(), layersBefore.end(), _paintTextureHash) != layersBefore.end();
                    if (targetOpacity <= 0.0f && !textureAlreadyPresent)
                        continue;

                    u32 selectedLayerIndex = 0;
                    bool alphaMapRepacked = false;
                    if (!PrepareCellForTexture(chunk, *alphaMap, chunkID, cellID, selectedLayerIndex, alphaMapRepacked))
                        continue;

                    const u32 layerCount = GetLayerCount(chunk.cellsData.layerTextureIDs[cellID]);
                    const size_t cellOffset = static_cast<size_t>(cellID) * ALPHA_MAP_CELL_BYTE_SIZE;
                    const bool layersChanged = !std::equal(layersBefore.begin(), layersBefore.end(), chunk.cellsData.layerTextureIDs[cellID]);
                    const bool textureCellRecorded = _activeTransaction.textureCellDeltaLookup.contains(PackCellAddress(chunkID, cellID));
                    const size_t beforeOffset = textureCellRecorded ? std::numeric_limits<size_t>::max() : _paintBeforeScratch.size();
                    if (!textureCellRecorded)
                    {
                        const u8* cellData = alphaMap->rgba.data() + cellOffset;
                        _paintBeforeScratch.insert(_paintBeforeScratch.end(), cellData, cellData + ALPHA_MAP_CELL_BYTE_SIZE);
                    }

                    PaintCellWork work = {
                        .chunk = &chunk,
                        .chunkID = chunkID,
                        .cellID = cellID,
                        .selectedLayerIndex = selectedLayerIndex,
                        .layerCount = layerCount,
                        .minTexelX = minTexelX,
                        .maxTexelX = maxTexelX,
                        .minTexelY = minTexelY,
                        .maxTexelY = maxTexelY,
                        .cellMinWorldX = cellMinWorldX,
                        .cellMaxWorldZ = cellMaxWorldZ,
                        .beforeOffset = beforeOffset,
                        .textureCellRecorded = textureCellRecorded,
                        .layersChanged = layersChanged
                    };
                    if (alphaMapRepacked)
                    {
                        work.change.minTexelX = 0;
                        work.change.minTexelY = 0;
                        work.change.maxTexelX = static_cast<u16>(ALPHA_MAP_RESOLUTION);
                        work.change.maxTexelY = static_cast<u16>(ALPHA_MAP_RESOLUTION);
                        work.change.alphaChanged = true;
                    }

                    _paintCellWorkScratch.push_back(std::move(work));
                }
            }
        }

        TracyPlot("Terrain Paint Candidate Cells", static_cast<i64>(_paintCellWorkScratch.size()));

        for (PaintCellWork& work : _paintCellWorkScratch)
        {
            auto alphaItr = _editableAlphaMaps.find(work.chunkID);
            if (alphaItr != _editableAlphaMaps.end())
                work.alphaMap = &alphaItr->second;
        }

        const auto processCells = [this, center, radiusSquared, texelSize, targetWeight, blendIndexScale](enki::TaskSetPartition range, u32)
        {
            ZoneScopedN("Terrain Paint Cell Batch");

            for (u32 workIndex = range.start; workIndex < range.end; workIndex++)
            {
                PaintCellWork& work = _paintCellWorkScratch[workIndex];
                if (!work.alphaMap)
                    continue;

                const size_t cellOffset = static_cast<size_t>(work.cellID) * ALPHA_MAP_CELL_BYTE_SIZE;
                for (i32 texelY = work.minTexelY; texelY <= work.maxTexelY; texelY++)
                {
                    const f32 worldZ = work.cellMaxWorldZ - (static_cast<f32>(texelY) + 0.5f) * texelSize;
                    for (i32 texelX = work.minTexelX; texelX <= work.maxTexelX; texelX++)
                    {
                        const f32 worldX = work.cellMinWorldX + (static_cast<f32>(texelX) + 0.5f) * texelSize;
                        const vec2 toTexel = vec2(worldX, worldZ) - center;
                        const f32 distanceSquared = glm::dot(toTexel, toTexel);
                        if (distanceSquared > radiusSquared)
                            continue;

                        const size_t blendIndex = static_cast<size_t>(glm::round(distanceSquared / radiusSquared * blendIndexScale));
                        const f32 blend = _paintBlendScratch[std::min(blendIndex, _paintBlendScratch.size() - 1)];
                        if (blend <= 0.0f)
                            continue;

                        const u16 texelID = static_cast<u16>(texelX + texelY * static_cast<i32>(ALPHA_MAP_RESOLUTION));
                        u8* texel = work.alphaMap->rgba.data() + cellOffset + static_cast<size_t>(texelID) * ALPHA_MAP_CHANNEL_COUNT;
                        const u32 currentWeight = DecodeLayerWeight(texel, work.selectedLayerIndex);
                        u32 newSelectedWeight = static_cast<u32>(glm::round(glm::mix(static_cast<f32>(currentWeight), targetWeight, blend)));
                        newSelectedWeight = glm::min(newSelectedWeight, 255u);
                        if (newSelectedWeight == currentWeight)
                            continue;

                        std::array<u8, Map::CellsData::CELL_LAYER_COUNT> weights = DecodeLayerWeights(texel);
                        const u32 oldOtherWeight = 255u - currentWeight;
                        const u32 newOtherWeight = 255u - newSelectedWeight;
                        if (oldOtherWeight == 0)
                        {
                            u32 fallbackLayer = Map::CellsData::CELL_LAYER_COUNT;
                            for (u32 layerIndex = 0; layerIndex < work.layerCount; layerIndex++)
                            {
                                if (layerIndex != work.selectedLayerIndex)
                                {
                                    fallbackLayer = layerIndex;
                                    break;
                                }
                            }

                            if (fallbackLayer == Map::CellsData::CELL_LAYER_COUNT)
                                continue;

                            weights.fill(0);
                            weights[fallbackLayer] = static_cast<u8>(newOtherWeight);
                        }
                        else
                        {
                            u32 assignedWeight = 0;
                            u32 lastOtherLayer = Map::CellsData::CELL_LAYER_COUNT;
                            for (u32 layerIndex = 0; layerIndex < Map::CellsData::CELL_LAYER_COUNT; layerIndex++)
                            {
                                if (layerIndex == work.selectedLayerIndex)
                                    continue;

                                const u32 scaledWeight = static_cast<u32>(weights[layerIndex]) * newOtherWeight / oldOtherWeight;
                                weights[layerIndex] = static_cast<u8>(scaledWeight);
                                assignedWeight += scaledWeight;
                                if (layerIndex < work.layerCount)
                                    lastOtherLayer = layerIndex;
                            }

                            if (lastOtherLayer != Map::CellsData::CELL_LAYER_COUNT)
                                weights[lastOtherLayer] = static_cast<u8>(weights[lastOtherLayer] + (newOtherWeight - assignedWeight));
                        }

                        weights[work.selectedLayerIndex] = static_cast<u8>(newSelectedWeight);
                        EncodeLayerWeights(weights, texel);
                        work.change.minTexelX = glm::min(work.change.minTexelX, static_cast<u16>(texelX));
                        work.change.minTexelY = glm::min(work.change.minTexelY, static_cast<u16>(texelY));
                        work.change.maxTexelX = glm::max(work.change.maxTexelX, static_cast<u16>(texelX + 1));
                        work.change.maxTexelY = glm::max(work.change.maxTexelY, static_cast<u16>(texelY + 1));
                        work.change.alphaChanged = true;
                    }
                }
            }
        };

        if (!_paintCellWorkScratch.empty())
        {
            enki::TaskScheduler* taskScheduler = ServiceLocator::GetTaskScheduler();
            if (taskScheduler && _paintCellWorkScratch.size() > 1)
            {
                enki::TaskSet paintCellsTask(static_cast<u32>(_paintCellWorkScratch.size()), processCells);
                taskScheduler->AddTaskSetToPipe(&paintCellsTask);
                taskScheduler->WaitforTask(&paintCellsTask);
            }
            else
            {
                const enki::TaskSetPartition range = { 0, static_cast<u32>(_paintCellWorkScratch.size()) };
                processCells(range, 0);
            }
        }

        // Exact endpoint strokes can leave assigned layers with no contribution. Compact them here
        // so the cell's layer table remains an accurate description of its blend map.
        const bool targetsEndpoint = targetWeight <= std::numeric_limits<f32>::epsilon() || targetWeight >= 255.0f - std::numeric_limits<f32>::epsilon();
        if (targetsEndpoint)
        {
            for (PaintCellWork& work : _paintCellWorkScratch)
            {
                if (!work.alphaMap)
                    continue;

                const size_t cellOffset = static_cast<size_t>(work.cellID) * ALPHA_MAP_CELL_BYTE_SIZE;
                u8 layerUsageMask = 0;
                for (u32 texelID = 0; texelID < ALPHA_MAP_TEXEL_COUNT; texelID++)
                {
                    const u8* texel = work.alphaMap->rgba.data() + cellOffset + static_cast<size_t>(texelID) * ALPHA_MAP_CHANNEL_COUNT;
                    const auto weights = DecodeLayerWeights(texel);
                    for (u32 layerIndex = 0; layerIndex < work.layerCount; layerIndex++)
                    {
                        if (weights[layerIndex] != 0)
                            layerUsageMask |= static_cast<u8>(1u << layerIndex);
                    }

                    if (layerUsageMask == static_cast<u8>((1u << work.layerCount) - 1u))
                        break;
                }

                if (layerUsageMask == static_cast<u8>((1u << work.layerCount) - 1u))
                    continue;

                RecordCellLayersBeforeChange(work.chunkID, work.cellID, work.chunk->cellsData.layerTextureIDs[work.cellID]);
                for (u32 layerIndex = work.layerCount; layerIndex > 0; layerIndex--)
                {
                    const u32 candidateLayer = layerIndex - 1;
                    if ((layerUsageMask & static_cast<u8>(1u << candidateLayer)) == 0)
                        RemoveTextureLayer(*work.chunk, *work.alphaMap, work.cellID, candidateLayer);
                }

                work.layersChanged = true;
                work.change.minTexelX = 0;
                work.change.minTexelY = 0;
                work.change.maxTexelX = static_cast<u16>(ALPHA_MAP_RESOLUTION);
                work.change.maxTexelY = static_cast<u16>(ALPHA_MAP_RESOLUTION);
                work.change.alphaChanged = true;
            }
        }

        bool changed = false;
        for (PaintCellWork& work : _paintCellWorkScratch)
        {
            if (!work.layersChanged && !work.change.alphaChanged)
                continue;

            if (work.change.alphaChanged && !work.textureCellRecorded)
                RecordTextureCellBeforeChange(work.chunkID, work.cellID, _paintBeforeScratch.data() + work.beforeOffset);

            RecordChunkAlphaMapBeforeChange(work.chunkID, work.chunk->chunkAlphaMapTextureHash);
            work.chunk->chunkAlphaMapTextureHash = Util::AssetPath::Hash(work.alphaMap->virtualPath);
            _activeTransaction.chunkAlphaMapDeltas[_activeTransaction.chunkAlphaMapDeltaLookup[work.chunkID]].after = work.chunk->chunkAlphaMapTextureHash;
            work.alphaMap->dirty = true;
            auto layerDeltaItr = _activeTransaction.cellLayerDeltaLookup.find(PackCellAddress(work.chunkID, work.cellID));
            if (layerDeltaItr != _activeTransaction.cellLayerDeltaLookup.end())
            {
                CellLayerDelta& delta = _activeTransaction.cellLayerDeltas[layerDeltaItr->second];
                std::copy(std::begin(work.chunk->cellsData.layerTextureIDs[work.cellID]), std::end(work.chunk->cellsData.layerTextureIDs[work.cellID]), delta.after.begin());
            }

            PaintCellChange& accumulatedChange = outChangedCells[PackCellAddress(work.chunkID, work.cellID)];
            accumulatedChange.layersChanged |= work.layersChanged;
            if (work.change.alphaChanged)
            {
                accumulatedChange.minTexelX = glm::min(accumulatedChange.minTexelX, work.change.minTexelX);
                accumulatedChange.minTexelY = glm::min(accumulatedChange.minTexelY, work.change.minTexelY);
                accumulatedChange.maxTexelX = glm::max(accumulatedChange.maxTexelX, work.change.maxTexelX);
                accumulatedChange.maxTexelY = glm::max(accumulatedChange.maxTexelY, work.change.maxTexelY);
                accumulatedChange.alphaChanged = true;
            }
            changed = true;
        }

        return changed;
    }

    bool TerrainEditSession::GetCellAtWorldPosition(const vec2& worldPosition, u32& outChunkID, u16& outCellID, vec2* outLocalPosition) const
    {
        const vec2 mapPosition(Terrain::MAP_HALF_SIZE + worldPosition.x, Terrain::MAP_HALF_SIZE - worldPosition.y);
        if (mapPosition.x < 0.0f || mapPosition.y < 0.0f || mapPosition.x >= Terrain::MAP_SIZE || mapPosition.y >= Terrain::MAP_SIZE)
            return false;

        const ivec2 globalCell = ivec2(glm::floor(mapPosition / Terrain::CELL_SIZE));
        const ivec2 chunk = globalCell / static_cast<i32>(Terrain::CHUNK_NUM_CELLS_PER_STRIDE);
        outChunkID = static_cast<u32>(chunk.x + chunk.y * static_cast<i32>(Terrain::CHUNK_NUM_PER_MAP_STRIDE));
        if (!_loadedChunks.contains(outChunkID))
            return false;

        const ivec2 localCell = globalCell - chunk * static_cast<i32>(Terrain::CHUNK_NUM_CELLS_PER_STRIDE);
        outCellID = static_cast<u16>(localCell.x + localCell.y * static_cast<i32>(Terrain::CHUNK_NUM_CELLS_PER_STRIDE));
        if (outLocalPosition)
            *outLocalPosition = mapPosition - vec2(globalCell) * Terrain::CELL_SIZE;

        return true;
    }

    TerrainEditSession::EditableAlphaMap* TerrainEditSession::GetOrCreateAlphaMap(u32 chunkID)
    {
        auto existingItr = _editableAlphaMaps.find(chunkID);
        if (existingItr != _editableAlphaMaps.end())
            return &existingItr->second;

        ZoneScopedN("Terrain Paint Load Editable Alpha Map");

        auto chunkItr = _loadedChunks.find(chunkID);
        if (chunkItr == _loadedChunks.end() || !chunkItr->second.chunk || _mapName.empty())
        {
            return nullptr;
        }

        const u32 chunkX = chunkID % Terrain::CHUNK_NUM_PER_MAP_STRIDE;
        const u32 chunkY = chunkID / Terrain::CHUNK_NUM_PER_MAP_STRIDE;
        EditableAlphaMap alphaMap;
        alphaMap.virtualPath = Util::AssetPath::Texture("blendmaps/" + _mapName + "/" + _mapName + "_" + std::to_string(chunkX) + "_" + std::to_string(chunkY) + ".dds");
        alphaMap.rgba.resize(Terrain::CHUNK_ALPHAMAP_TOTAL_BYTE_SIZE, 0);
        for (size_t byteOffset = 3; byteOffset < alphaMap.rgba.size(); byteOffset += ALPHA_MAP_CHANNEL_COUNT)
            alphaMap.rgba[byteOffset] = 255;

        const u64 sourceHash = chunkItr->second.chunk->chunkAlphaMapTextureHash;
        if (sourceHash != 0 && sourceHash != Terrain::TEXTURE_ID_INVALID)
        {
            PACT::PactFileHandle fileHandle;
            PACT::PactStorage* pactStorage = ServiceLocator::GetPactStorage();
            if (!pactStorage || pactStorage->ReadFile(sourceHash, fileHandle) != PACT::PactReadResult::Success)
                return nullptr;

            const gli::texture sourceTexture = gli::load(static_cast<const char*>(fileHandle.GetData()), fileHandle.GetSize());
            if (sourceTexture.empty() || sourceTexture.target() != gli::TARGET_3D)
                return nullptr;

            const gli::texture3d source(sourceTexture);
            if (!DecodeAlphaMapTexture(source, alphaMap.rgba))
                return nullptr;
        }

        return &_editableAlphaMaps.emplace(chunkID, std::move(alphaMap)).first->second;
    }

    bool TerrainEditSession::EnsureAlphaMapRenderable(u32 chunkID, EditableAlphaMap& alphaMap)
    {
        if (alphaMap.textureID != Renderer::TextureID::Invalid())
            return true;

        ZoneScopedN("Terrain Paint Create Editable Alpha Texture");

        auto chunkItr = _loadedChunks.find(chunkID);
        if (chunkItr == _loadedChunks.end() || !chunkItr->second.chunk)
            return false;

        const u64 alphaMapHash = Util::AssetPath::Hash(alphaMap.virtualPath);
        if (!_terrainRenderer.CreateEditableAlphaMap(chunkItr->second.rendererChunkIndex, alphaMapHash, alphaMap.rgba, alphaMap.textureID))
            return false;

        return true;
    }

    bool TerrainEditSession::PrepareCellForTexture(Map::Chunk& chunk, EditableAlphaMap& alphaMap, u32 chunkID, u16 cellID, u32& outLayerIndex, bool& outAlphaMapRepacked)
    {
        outAlphaMapRepacked = false;
        u64* layers = chunk.cellsData.layerTextureIDs[cellID];
        u32 layerCount = GetLayerCount(layers);
        if (_paintTargetLayerIndex < Map::CellsData::CELL_LAYER_COUNT)
        {
            if (_paintTargetLayerIndex > layerCount)
            {
                _blockedPaintCellScratch.insert(PackCellAddress(chunkID, cellID));
                return false;
            }

            outLayerIndex = _paintTargetLayerIndex;
            bool layerAssignmentChanged = false;
            if (_paintTargetLayerIndex < layerCount)
            {
                if (layers[_paintTargetLayerIndex] != _paintTextureHash)
                {
                    RecordCellLayersBeforeChange(chunkID, cellID, layers);
                    layers[_paintTargetLayerIndex] = _paintTextureHash;
                    layerAssignmentChanged = true;
                }
            }
            else
            {
                RecordCellLayersBeforeChange(chunkID, cellID, layers);
                layers[_paintTargetLayerIndex] = _paintTextureHash;
                layerAssignmentChanged = true;
                for (u32 layerIndex = layerCount + 1; layerIndex < Map::CellsData::CELL_LAYER_COUNT; layerIndex++)
                {
                    layers[layerIndex] = Terrain::TEXTURE_ID_INVALID;
                }
            }

            if (layerAssignmentChanged && SanitizeTextureLayerWeights(alphaMap, chunkID, cellID, layerCount))
                outAlphaMapRepacked = true;

            return true;
        }

        for (u32 layerIndex = 0; layerIndex < layerCount; layerIndex++)
        {
            if (layers[layerIndex] == _paintTextureHash)
            {
                outLayerIndex = layerIndex;
                return true;
            }
        }

        if (layerCount == Map::CellsData::CELL_LAYER_COUNT)
        {
            const size_t cellOffset = static_cast<size_t>(cellID) * ALPHA_MAP_CELL_BYTE_SIZE;
            u8 layerUsageMask = 0;
            for (u32 texelID = 0; texelID < ALPHA_MAP_TEXEL_COUNT && layerUsageMask != 0xf; texelID++)
            {
                const u8* texel = alphaMap.rgba.data() + cellOffset + static_cast<size_t>(texelID) * ALPHA_MAP_CHANNEL_COUNT;
                const auto weights = DecodeLayerWeights(texel);
                for (u32 layerIndex = 0; layerIndex < Map::CellsData::CELL_LAYER_COUNT; layerIndex++)
                {
                    if (weights[layerIndex] != 0)
                        layerUsageMask |= static_cast<u8>(1u << layerIndex);
                }
            }

            u32 zeroWeightLayer = Map::CellsData::CELL_LAYER_COUNT;
            for (u32 layerIndex = 0; layerIndex < layerCount; layerIndex++)
            {
                if ((layerUsageMask & static_cast<u8>(1u << layerIndex)) == 0)
                {
                    zeroWeightLayer = layerIndex;
                    break;
                }
            }

            if (zeroWeightLayer == Map::CellsData::CELL_LAYER_COUNT)
            {
                _blockedPaintCellScratch.insert(PackCellAddress(chunkID, cellID));
                return false;
            }

            RecordCellLayersBeforeChange(chunkID, cellID, layers);
            RecordTextureCellBeforeChange(chunkID, cellID, alphaMap.rgba.data() + cellOffset);
            RemoveTextureLayer(chunk, alphaMap, cellID, zeroWeightLayer);
            outAlphaMapRepacked = true;
            layerCount--;
        }

        RecordCellLayersBeforeChange(chunkID, cellID, layers);
        layers[layerCount] = _paintTextureHash;
        for (u32 layerIndex = layerCount + 1; layerIndex < Map::CellsData::CELL_LAYER_COUNT; layerIndex++)
        {
            layers[layerIndex] = Terrain::TEXTURE_ID_INVALID;
        }

        outLayerIndex = layerCount;
        return true;
    }

    bool TerrainEditSession::SanitizeTextureLayerWeights(EditableAlphaMap& alphaMap, u32 chunkID, u16 cellID, u32 layerCount)
    {
        const size_t cellOffset = static_cast<size_t>(cellID) * ALPHA_MAP_CELL_BYTE_SIZE;
        bool changed = false;
        for (u32 texelID = 0; texelID < ALPHA_MAP_TEXEL_COUNT; texelID++)
        {
            u8* texel = alphaMap.rgba.data() + cellOffset + static_cast<size_t>(texelID) * ALPHA_MAP_CHANNEL_COUNT;
            auto weights = DecodeLayerWeights(texel);
            for (u32 layerIndex = layerCount; layerIndex < Map::CellsData::CELL_LAYER_COUNT; layerIndex++)
            {
                weights[layerIndex] = 0;
            }

            const std::array<u8, 3> encodedWeights = { weights[1], weights[2], weights[3] };
            if (std::equal(encodedWeights.begin(), encodedWeights.end(), texel))
                continue;

            if (!changed)
                RecordTextureCellBeforeChange(chunkID, cellID, alphaMap.rgba.data() + cellOffset);

            EncodeLayerWeights(weights, texel);
            changed = true;
        }

        return changed;
    }

    void TerrainEditSession::RemoveTextureLayer(Map::Chunk& chunk, EditableAlphaMap& alphaMap, u16 cellID, u32 layerIndex)
    {
        ZoneScopedN("Terrain Paint Repack Texture Layer");

        u64* layers = chunk.cellsData.layerTextureIDs[cellID];
        const u32 layerCount = GetLayerCount(layers);
        const size_t cellOffset = static_cast<size_t>(cellID) * ALPHA_MAP_CELL_BYTE_SIZE;
        for (u32 texelID = 0; texelID < ALPHA_MAP_TEXEL_COUNT; texelID++)
        {
            u8* texel = alphaMap.rgba.data() + cellOffset + static_cast<size_t>(texelID) * ALPHA_MAP_CHANNEL_COUNT;
            auto weights = DecodeLayerWeights(texel);
            for (u32 shiftedLayer = layerIndex; shiftedLayer + 1 < layerCount; shiftedLayer++)
                weights[shiftedLayer] = weights[shiftedLayer + 1];
            weights[layerCount - 1] = 0;
            EncodeLayerWeights(weights, texel);
        }

        for (u32 shiftedLayer = layerIndex; shiftedLayer + 1 < layerCount; shiftedLayer++)
            layers[shiftedLayer] = layers[shiftedLayer + 1];
        layers[layerCount - 1] = Terrain::TEXTURE_ID_INVALID;
    }

    void TerrainEditSession::UploadPaintChanges(const robin_hood::unordered_map<u32, PaintCellChange>& changedCells)
    {
        ZoneScopedN("Terrain Paint Upload Changes");

        size_t uploadedBytes = 0;
        size_t updatedLayerCellCount = 0;
        for (auto& [chunkID, cellIDs] : _chunkCellScratch)
            cellIDs.clear();

        for (const auto& change : changedCells)
            _chunkCellScratch[change.first >> 8].push_back(static_cast<u16>(change.first & 0xff));

        for (auto& [chunkID, cellIDs] : _chunkCellScratch)
        {
            if (cellIDs.empty())
                continue;

            auto chunkItr = _loadedChunks.find(chunkID);
            auto alphaItr = _editableAlphaMaps.find(chunkID);
            if (chunkItr == _loadedChunks.end() || !chunkItr->second.chunk || alphaItr == _editableAlphaMaps.end())
                continue;

            _layerCellScratch.clear();
            for (u16 cellID : cellIDs)
            {
                const auto changeItr = changedCells.find(PackCellAddress(chunkID, cellID));
                if (changeItr != changedCells.end() && changeItr->second.layersChanged)
                    _layerCellScratch.push_back(cellID);
            }

            if (!_layerCellScratch.empty())
            {
                _terrainRenderer.UpdateChunkTextureLayers(chunkItr->second.rendererChunkIndex, *chunkItr->second.chunk, _layerCellScratch);
                updatedLayerCellCount += _layerCellScratch.size();
            }

            EditableAlphaMap& alphaMap = alphaItr->second;
            if (!EnsureAlphaMapRenderable(chunkID, alphaMap))
                continue;

            for (u16 cellID : cellIDs)
            {
                const auto changeItr = changedCells.find(PackCellAddress(chunkID, cellID));
                if (changeItr == changedCells.end() || !changeItr->second.alphaChanged)
                    continue;

                const PaintCellChange& change = changeItr->second;
                const u32 width = change.maxTexelX - change.minTexelX;
                const u32 height = change.maxTexelY - change.minTexelY;
                const size_t uploadRowSize = static_cast<size_t>(width) * ALPHA_MAP_CHANNEL_COUNT;
                _alphaUploadScratch.resize(uploadRowSize * height);

                const size_t cellOffset = static_cast<size_t>(cellID) * ALPHA_MAP_CELL_BYTE_SIZE;
                for (u32 row = 0; row < height; row++)
                {
                    const size_t sourceOffset = cellOffset + (static_cast<size_t>(change.minTexelY + row) * ALPHA_MAP_RESOLUTION + change.minTexelX) * ALPHA_MAP_CHANNEL_COUNT;
                    std::memcpy(_alphaUploadScratch.data() + static_cast<size_t>(row) * uploadRowSize, alphaMap.rgba.data() + sourceOffset, uploadRowSize);
                }

                _terrainRenderer.UploadEditableAlphaMapRegion(alphaMap.textureID, cellID, uvec2(change.minTexelX, change.minTexelY), uvec2(width, height), _alphaUploadScratch);
                uploadedBytes += _alphaUploadScratch.size();
            }
        }

        TracyPlot("Terrain Paint Alpha Upload Bytes", static_cast<i64>(uploadedBytes));
        TracyPlot("Terrain Paint Layer Cells Updated", static_cast<i64>(updatedLayerCellCount));
    }

    void TerrainEditSession::UploadChangedAlphaCells(const robin_hood::unordered_set<u32>& changedCells)
    {
        for (auto& [chunkID, cellIDs] : _chunkCellScratch)
            cellIDs.clear();

        for (u32 packedCell : changedCells)
            _chunkCellScratch[packedCell >> 8].push_back(static_cast<u16>(packedCell & 0xff));

        for (auto& [chunkID, cellIDs] : _chunkCellScratch)
        {
            if (cellIDs.empty())
                continue;

            auto chunkItr = _loadedChunks.find(chunkID);
            auto alphaItr = _editableAlphaMaps.find(chunkID);
            if (chunkItr == _loadedChunks.end() || !chunkItr->second.chunk || alphaItr == _editableAlphaMaps.end())
                continue;

            EditableAlphaMap& alphaMap = alphaItr->second;
            if (!EnsureAlphaMapRenderable(chunkID, alphaMap))
                continue;

            std::sort(cellIDs.begin(), cellIDs.end());
            cellIDs.erase(std::unique(cellIDs.begin(), cellIDs.end()), cellIDs.end());
            _terrainRenderer.UpdateChunkTextureLayers(chunkItr->second.rendererChunkIndex, *chunkItr->second.chunk, cellIDs);
            for (u16 cellID : cellIDs)
            {
                const size_t cellOffset = static_cast<size_t>(cellID) * ALPHA_MAP_CELL_BYTE_SIZE;
                const std::span<const u8> cellData(alphaMap.rgba.data() + cellOffset, ALPHA_MAP_CELL_BYTE_SIZE);
                _terrainRenderer.UploadEditableAlphaMapRegion(alphaMap.textureID, cellID, uvec2(0), uvec2(ALPHA_MAP_RESOLUTION), cellData);
            }
        }
    }

    void TerrainEditSession::UploadChangedVertexCells(const robin_hood::unordered_set<u32>& changedCells)
    {
        for (auto& [chunkID, cellIDs] : _chunkCellScratch)
            cellIDs.clear();

        for (u32 packedCell : changedCells)
            _chunkCellScratch[packedCell >> 8].push_back(static_cast<u16>(packedCell & 0xff));

        for (auto& [chunkID, cellIDs] : _chunkCellScratch)
        {
            if (cellIDs.empty())
                continue;

            auto chunkItr = _loadedChunks.find(chunkID);
            if (chunkItr == _loadedChunks.end() || !chunkItr->second.chunk)
                continue;

            std::sort(cellIDs.begin(), cellIDs.end());
            cellIDs.erase(std::unique(cellIDs.begin(), cellIDs.end()), cellIDs.end());
            _terrainRenderer.UpdateChunkCells(chunkItr->second.rendererChunkIndex, *chunkItr->second.chunk, cellIDs);
        }
    }

    bool TerrainEditSession::SaveAlphaMaps(const std::vector<u32>& chunkIDs, robin_hood::unordered_set<u32>& outSavedChunkIDs)
    {
        outSavedChunkIDs.clear();
        Util::AssetWriter* assetWriter = ServiceLocator::GetAssetWriter();
        if (!assetWriter)
            return false;

        bool savedAll = true;
        for (u32 chunkID : chunkIDs)
        {
            auto alphaItr = _editableAlphaMaps.find(chunkID);
            if (alphaItr == _editableAlphaMaps.end() || !alphaItr->second.dirty)
                continue;

            EditableAlphaMap& alphaMap = alphaItr->second;
            auto chunkItr = _loadedChunks.find(chunkID);
            if (chunkItr != _loadedChunks.end() && chunkItr->second.chunk && (chunkItr->second.chunk->chunkAlphaMapTextureHash == 0 || chunkItr->second.chunk->chunkAlphaMapTextureHash == Terrain::TEXTURE_ID_INVALID))
            {
                outSavedChunkIDs.insert(chunkID);
                continue;
            }

            gli::texture3d texture(gli::FORMAT_RGBA8_UNORM_PACK8, gli::extent3d(ALPHA_MAP_RESOLUTION, ALPHA_MAP_RESOLUTION, Terrain::CHUNK_NUM_CELLS), 1);
            if (texture.empty() || texture.size() != alphaMap.rgba.size())
            {
                savedAll = false;
                continue;
            }

            std::memcpy(texture.data(), alphaMap.rgba.data(), alphaMap.rgba.size());
            std::vector<char> encodedData;
            if (!gli::save_dds(texture, encodedData) || encodedData.empty() || !assetWriter->WriteBytes(alphaMap.virtualPath, encodedData.data(), encodedData.size(), Util::AssetWriteTarget::PactOverlay))
            {
                savedAll = false;
                continue;
            }

            outSavedChunkIDs.insert(chunkID);
        }

        return savedAll;
    }

    void TerrainEditSession::GatherVertices(const vec2& center, f32 radius, std::vector<VertexCandidate>& outCandidates) const
    {
        const vec2 centerInMap = vec2(Terrain::MAP_HALF_SIZE + center.x, Terrain::MAP_HALF_SIZE - center.y);
        const ivec2 minCell = glm::clamp(ivec2(glm::floor((centerInMap - vec2(radius)) / Terrain::CELL_SIZE)), ivec2(0), ivec2(Terrain::CHUNK_NUM_PER_MAP_STRIDE * Terrain::CHUNK_NUM_CELLS_PER_STRIDE - 1));
        const ivec2 maxCell = glm::clamp(ivec2(glm::floor((centerInMap + vec2(radius)) / Terrain::CELL_SIZE)), ivec2(0), ivec2(Terrain::CHUNK_NUM_PER_MAP_STRIDE * Terrain::CHUNK_NUM_CELLS_PER_STRIDE - 1));
        const f32 radiusSquared = radius * radius;

        outCandidates.clear();
        for (i32 cellY = minCell.y; cellY <= maxCell.y; cellY++)
        {
            for (i32 cellX = minCell.x; cellX <= maxCell.x; cellX++)
            {
                const i32 chunkX = cellX / static_cast<i32>(Terrain::CHUNK_NUM_CELLS_PER_STRIDE);
                const i32 chunkY = cellY / static_cast<i32>(Terrain::CHUNK_NUM_CELLS_PER_STRIDE);
                const u32 chunkID = static_cast<u32>(chunkX + chunkY * static_cast<i32>(Terrain::CHUNK_NUM_PER_MAP_STRIDE));
                if (!_loadedChunks.contains(chunkID))
                    continue;

                const u16 localCellX = static_cast<u16>(cellX % static_cast<i32>(Terrain::CHUNK_NUM_CELLS_PER_STRIDE));
                const u16 localCellY = static_cast<u16>(cellY % static_cast<i32>(Terrain::CHUNK_NUM_CELLS_PER_STRIDE));
                const u16 cellID = static_cast<u16>(localCellX + localCellY * Terrain::CHUNK_NUM_CELLS_PER_STRIDE);

                for (u16 vertexID = 0; vertexID < Terrain::CELL_NUM_VERTICES; vertexID++)
                {
                    const vec2 vertexPosition = GetVertexWorldPosition(chunkID, cellID, vertexID);
                    const vec2 offset = center - vertexPosition;
                    const f32 distanceSquared = glm::dot(offset, offset);
                    if (distanceSquared > radiusSquared)
                        continue;

                    outCandidates.push_back({ .address = { .chunkID = chunkID, .cellID = cellID, .vertexID = vertexID }, .position = vertexPosition, .distance = glm::sqrt(distanceSquared) });
                }
            }
        }
    }

    void TerrainEditSession::SynchronizeSharedOuterVertices(robin_hood::unordered_set<u32>& outChangedCells)
    {
        constexpr i32 MAP_CELL_STRIDE = Terrain::CHUNK_NUM_PER_MAP_STRIDE * Terrain::CHUNK_NUM_CELLS_PER_STRIDE;
        _sharedVertexHeightScratch.clear();
        for (u32 candidateIndex = 0; candidateIndex < _candidateScratch.size(); candidateIndex++)
        {
            const VertexCandidate& candidate = _candidateScratch[candidateIndex];
            if (!std::isfinite(_newHeightScratch[candidateIndex]) || !_editableChunkScratch.contains(candidate.address.chunkID))
                continue;

            const u16 packedX = candidate.address.vertexID % Terrain::CELL_GRID_ROW_SIZE;
            const u16 packedY = candidate.address.vertexID / Terrain::CELL_GRID_ROW_SIZE;
            const bool outerVertex = packedX < Terrain::CELL_OUTER_GRID_STRIDE;
            const bool sharedVertex = outerVertex && (packedX == 0 || packedX == Terrain::CELL_NUM_PATCHES_PER_STRIDE || packedY == 0 || packedY == Terrain::CELL_NUM_PATCHES_PER_STRIDE);
            if (!sharedVertex)
                continue;

            const i32 chunkX = static_cast<i32>(candidate.address.chunkID % Terrain::CHUNK_NUM_PER_MAP_STRIDE);
            const i32 chunkY = static_cast<i32>(candidate.address.chunkID / Terrain::CHUNK_NUM_PER_MAP_STRIDE);
            const i32 cellX = candidate.address.cellID % Terrain::CHUNK_NUM_CELLS_PER_STRIDE;
            const i32 cellY = candidate.address.cellID / Terrain::CHUNK_NUM_CELLS_PER_STRIDE;
            // Adjacent cells store independent copies of their outer edge vertices. Collapse those
            // copies to one map-wide coordinate before propagating the resulting height below.
            const u16 globalVertexX = static_cast<u16>((chunkX * Terrain::CHUNK_NUM_CELLS_PER_STRIDE + cellX) * Terrain::CELL_NUM_PATCHES_PER_STRIDE + packedX);
            const u16 globalVertexY = static_cast<u16>((chunkY * Terrain::CHUNK_NUM_CELLS_PER_STRIDE + cellY) * Terrain::CELL_NUM_PATCHES_PER_STRIDE + packedY);
            const u32 vertexKey = static_cast<u32>(globalVertexX) | (static_cast<u32>(globalVertexY) << 16);
            SharedVertexHeight& height = _sharedVertexHeightScratch[vertexKey];
            height.sum += _newHeightScratch[candidateIndex];
            height.count++;
        }

        for (const auto& [vertexKey, accumulatedHeight] : _sharedVertexHeightScratch)
        {
            if (accumulatedHeight.count == 0)
                continue;

            const i32 globalVertexX = static_cast<i32>(vertexKey & 0xffff);
            const i32 globalVertexY = static_cast<i32>(vertexKey >> 16);
            const f32 synchronizedHeight = accumulatedHeight.sum / static_cast<f32>(accumulatedHeight.count);
            const i32 primaryCellX = globalVertexX / static_cast<i32>(Terrain::CELL_NUM_PATCHES_PER_STRIDE);
            const i32 primaryCellY = globalVertexY / static_cast<i32>(Terrain::CELL_NUM_PATCHES_PER_STRIDE);
            const i32 minCellX = globalVertexX % static_cast<i32>(Terrain::CELL_NUM_PATCHES_PER_STRIDE) == 0 ? primaryCellX - 1 : primaryCellX;
            const i32 minCellY = globalVertexY % static_cast<i32>(Terrain::CELL_NUM_PATCHES_PER_STRIDE) == 0 ? primaryCellY - 1 : primaryCellY;

            // A vertex on both axes is shared by four cells; an edge vertex is shared by two.
            for (i32 globalCellY = minCellY; globalCellY <= primaryCellY; globalCellY++)
            {
                if (globalCellY < 0 || globalCellY >= MAP_CELL_STRIDE)
                    continue;

                for (i32 globalCellX = minCellX; globalCellX <= primaryCellX; globalCellX++)
                {
                    if (globalCellX < 0 || globalCellX >= MAP_CELL_STRIDE)
                        continue;

                    const u32 chunkID = static_cast<u32>((globalCellX / static_cast<i32>(Terrain::CHUNK_NUM_CELLS_PER_STRIDE)) + (globalCellY / static_cast<i32>(Terrain::CHUNK_NUM_CELLS_PER_STRIDE)) * static_cast<i32>(Terrain::CHUNK_NUM_PER_MAP_STRIDE));
                    if (!_loadedChunks.contains(chunkID))
                        continue;

                    auto editableItr = _editableChunkScratch.find(chunkID);
                    if (editableItr == _editableChunkScratch.end())
                    {
                        TerrainLoader::LoadedChunkView editableChunk;
                        if (!_terrainLoader.GetEditableChunk(chunkID, editableChunk))
                            continue;

                        _loadedChunks[chunkID] = editableChunk;
                        editableItr = _editableChunkScratch.emplace(chunkID, editableChunk).first;
                    }

                    const i32 localCellX = globalCellX % static_cast<i32>(Terrain::CHUNK_NUM_CELLS_PER_STRIDE);
                    const i32 localCellY = globalCellY % static_cast<i32>(Terrain::CHUNK_NUM_CELLS_PER_STRIDE);
                    const u16 cellID = static_cast<u16>(localCellX + localCellY * static_cast<i32>(Terrain::CHUNK_NUM_CELLS_PER_STRIDE));
                    const u16 localVertexX = static_cast<u16>(globalVertexX - globalCellX * static_cast<i32>(Terrain::CELL_NUM_PATCHES_PER_STRIDE));
                    const u16 localVertexY = static_cast<u16>(globalVertexY - globalCellY * static_cast<i32>(Terrain::CELL_NUM_PATCHES_PER_STRIDE));
                    const u16 vertexID = static_cast<u16>(localVertexX + localVertexY * Terrain::CELL_GRID_ROW_SIZE);
                    Map::Chunk& chunk = *editableItr->second.chunk;
                    f32& height = chunk.cellsData.heightField[cellID][vertexID];
                    if (glm::abs(height - synchronizedHeight) <= HEIGHT_EPSILON)
                        continue;

                    const VertexAddress address = { .chunkID = chunkID, .cellID = cellID, .vertexID = vertexID };
                    RecordBeforeChange(address, height);
                    height = synchronizedHeight;
                    _activeTransaction.deltas[_activeTransaction.deltaLookup[PackVertexAddress(address)]].after = height;
                    outChangedCells.insert(PackCellAddress(chunkID, cellID));
                }
            }
        }
    }

    void TerrainEditSession::SynchronizeSharedVertexColors(robin_hood::unordered_set<u32>& outChangedCells)
    {
        constexpr i32 MAP_CELL_STRIDE = Terrain::CHUNK_NUM_PER_MAP_STRIDE * Terrain::CHUNK_NUM_CELLS_PER_STRIDE;
        _sharedVertexColorScratch.clear();
        for (const VertexCandidate& candidate : _candidateScratch)
        {
            const u16 packedX = candidate.address.vertexID % Terrain::CELL_GRID_ROW_SIZE;
            const u16 packedY = candidate.address.vertexID / Terrain::CELL_GRID_ROW_SIZE;
            const bool sharedVertex = packedX < Terrain::CELL_OUTER_GRID_STRIDE && (packedX == 0 || packedX == Terrain::CELL_NUM_PATCHES_PER_STRIDE || packedY == 0 || packedY == Terrain::CELL_NUM_PATCHES_PER_STRIDE);
            if (!sharedVertex || !_activeTransaction.colorDeltaLookup.contains(PackVertexAddress(candidate.address)))
                continue;

            const i32 chunkX = static_cast<i32>(candidate.address.chunkID % Terrain::CHUNK_NUM_PER_MAP_STRIDE);
            const i32 chunkY = static_cast<i32>(candidate.address.chunkID / Terrain::CHUNK_NUM_PER_MAP_STRIDE);
            const i32 cellX = candidate.address.cellID % Terrain::CHUNK_NUM_CELLS_PER_STRIDE;
            const i32 cellY = candidate.address.cellID / Terrain::CHUNK_NUM_CELLS_PER_STRIDE;
            const u16 globalVertexX = static_cast<u16>((chunkX * Terrain::CHUNK_NUM_CELLS_PER_STRIDE + cellX) * Terrain::CELL_NUM_PATCHES_PER_STRIDE + packedX);
            const u16 globalVertexY = static_cast<u16>((chunkY * Terrain::CHUNK_NUM_CELLS_PER_STRIDE + cellY) * Terrain::CELL_NUM_PATCHES_PER_STRIDE + packedY);
            _sharedVertexColorScratch.try_emplace(static_cast<u32>(globalVertexX) | (static_cast<u32>(globalVertexY) << 16));
        }

        for (const VertexCandidate& candidate : _candidateScratch)
        {
            const u16 packedX = candidate.address.vertexID % Terrain::CELL_GRID_ROW_SIZE;
            const u16 packedY = candidate.address.vertexID / Terrain::CELL_GRID_ROW_SIZE;
            if (packedX >= Terrain::CELL_OUTER_GRID_STRIDE)
                continue;

            const i32 chunkX = static_cast<i32>(candidate.address.chunkID % Terrain::CHUNK_NUM_PER_MAP_STRIDE);
            const i32 chunkY = static_cast<i32>(candidate.address.chunkID / Terrain::CHUNK_NUM_PER_MAP_STRIDE);
            const i32 cellX = candidate.address.cellID % Terrain::CHUNK_NUM_CELLS_PER_STRIDE;
            const i32 cellY = candidate.address.cellID / Terrain::CHUNK_NUM_CELLS_PER_STRIDE;
            const u16 globalVertexX = static_cast<u16>((chunkX * Terrain::CHUNK_NUM_CELLS_PER_STRIDE + cellX) * Terrain::CELL_NUM_PATCHES_PER_STRIDE + packedX);
            const u16 globalVertexY = static_cast<u16>((chunkY * Terrain::CHUNK_NUM_CELLS_PER_STRIDE + cellY) * Terrain::CELL_NUM_PATCHES_PER_STRIDE + packedY);
            const u32 vertexKey = static_cast<u32>(globalVertexX) | (static_cast<u32>(globalVertexY) << 16);
            auto colorItr = _sharedVertexColorScratch.find(vertexKey);
            auto chunkItr = _editableChunkScratch.find(candidate.address.chunkID);
            if (colorItr == _sharedVertexColorScratch.end() || chunkItr == _editableChunkScratch.end())
                continue;

            const u8* color = chunkItr->second.chunk->cellsData.colors[candidate.address.cellID][candidate.address.vertexID];
            for (u32 channel = 0; channel < colorItr->second.sums.size(); channel++)
                colorItr->second.sums[channel] += color[channel];
            colorItr->second.count++;
        }

        for (const auto& [vertexKey, accumulatedColor] : _sharedVertexColorScratch)
        {
            if (accumulatedColor.count == 0)
                continue;

            std::array<u8, 3> synchronizedColor;
            for (u32 channel = 0; channel < synchronizedColor.size(); channel++)
                synchronizedColor[channel] = static_cast<u8>((accumulatedColor.sums[channel] + accumulatedColor.count / 2) / accumulatedColor.count);

            const i32 globalVertexX = static_cast<i32>(vertexKey & 0xffff);
            const i32 globalVertexY = static_cast<i32>(vertexKey >> 16);
            const i32 primaryCellX = globalVertexX / static_cast<i32>(Terrain::CELL_NUM_PATCHES_PER_STRIDE);
            const i32 primaryCellY = globalVertexY / static_cast<i32>(Terrain::CELL_NUM_PATCHES_PER_STRIDE);
            const i32 minCellX = globalVertexX % static_cast<i32>(Terrain::CELL_NUM_PATCHES_PER_STRIDE) == 0 ? primaryCellX - 1 : primaryCellX;
            const i32 minCellY = globalVertexY % static_cast<i32>(Terrain::CELL_NUM_PATCHES_PER_STRIDE) == 0 ? primaryCellY - 1 : primaryCellY;

            for (i32 globalCellY = minCellY; globalCellY <= primaryCellY; globalCellY++)
            {
                if (globalCellY < 0 || globalCellY >= MAP_CELL_STRIDE)
                    continue;

                for (i32 globalCellX = minCellX; globalCellX <= primaryCellX; globalCellX++)
                {
                    if (globalCellX < 0 || globalCellX >= MAP_CELL_STRIDE)
                        continue;

                    const u32 chunkID = static_cast<u32>((globalCellX / static_cast<i32>(Terrain::CHUNK_NUM_CELLS_PER_STRIDE)) + (globalCellY / static_cast<i32>(Terrain::CHUNK_NUM_CELLS_PER_STRIDE)) * static_cast<i32>(Terrain::CHUNK_NUM_PER_MAP_STRIDE));
                    if (!_loadedChunks.contains(chunkID))
                        continue;

                    auto editableItr = _editableChunkScratch.find(chunkID);
                    if (editableItr == _editableChunkScratch.end())
                    {
                        TerrainLoader::LoadedChunkView editableChunk;
                        if (!_terrainLoader.GetEditableChunk(chunkID, editableChunk))
                            continue;
                        _loadedChunks[chunkID] = editableChunk;
                        editableItr = _editableChunkScratch.emplace(chunkID, editableChunk).first;
                    }

                    const i32 localCellX = globalCellX % static_cast<i32>(Terrain::CHUNK_NUM_CELLS_PER_STRIDE);
                    const i32 localCellY = globalCellY % static_cast<i32>(Terrain::CHUNK_NUM_CELLS_PER_STRIDE);
                    const u16 cellID = static_cast<u16>(localCellX + localCellY * static_cast<i32>(Terrain::CHUNK_NUM_CELLS_PER_STRIDE));
                    const u16 localVertexX = static_cast<u16>(globalVertexX - globalCellX * static_cast<i32>(Terrain::CELL_NUM_PATCHES_PER_STRIDE));
                    const u16 localVertexY = static_cast<u16>(globalVertexY - globalCellY * static_cast<i32>(Terrain::CELL_NUM_PATCHES_PER_STRIDE));
                    const u16 vertexID = static_cast<u16>(localVertexX + localVertexY * Terrain::CELL_GRID_ROW_SIZE);
                    u8* color = editableItr->second.chunk->cellsData.colors[cellID][vertexID];
                    if (std::equal(synchronizedColor.begin(), synchronizedColor.end(), color))
                        continue;

                    const VertexAddress address = { .chunkID = chunkID, .cellID = cellID, .vertexID = vertexID };
                    RecordVertexColorBeforeChange(address, color);
                    std::copy(synchronizedColor.begin(), synchronizedColor.end(), color);
                    _activeTransaction.colorDeltas[_activeTransaction.colorDeltaLookup[PackVertexAddress(address)]].after = synchronizedColor;
                    outChangedCells.insert(PackCellAddress(chunkID, cellID));
                }
            }
        }
    }

    void TerrainEditSession::RecordBeforeChange(const VertexAddress& address, f32 height)
    {
        const u64 key = PackVertexAddress(address);
        auto itr = _activeTransaction.deltaLookup.find(key);
        if (itr != _activeTransaction.deltaLookup.end())
            return;

        const u32 index = static_cast<u32>(_activeTransaction.deltas.size());
        _activeTransaction.deltaLookup[key] = index;
        _activeTransaction.deltas.push_back({ .address = address, .before = height, .after = height });
    }

    void TerrainEditSession::RecordVertexColorBeforeChange(const VertexAddress& address, const u8* color)
    {
        const u64 key = PackVertexAddress(address);
        if (_activeTransaction.colorDeltaLookup.contains(key))
            return;

        VertexColorDelta delta;
        delta.address = address;
        std::copy(color, color + delta.before.size(), delta.before.begin());
        delta.after = delta.before;
        _activeTransaction.colorDeltaLookup[key] = static_cast<u32>(_activeTransaction.colorDeltas.size());
        _activeTransaction.colorDeltas.push_back(std::move(delta));
    }

    void TerrainEditSession::RecordTextureCellBeforeChange(u32 chunkID, u16 cellID, const u8* cellData)
    {
        const u32 key = PackCellAddress(chunkID, cellID);
        if (_activeTransaction.textureCellDeltaLookup.contains(key))
            return;

        TextureCellDelta delta;
        delta.chunkID = chunkID;
        delta.cellID = cellID;
        delta.before.assign(cellData, cellData + ALPHA_MAP_CELL_BYTE_SIZE);
        _activeTransaction.textureCellDeltaLookup[key] = static_cast<u32>(_activeTransaction.textureCellDeltas.size());
        _activeTransaction.textureCellDeltas.push_back(std::move(delta));
    }

    void TerrainEditSession::RecordCellLayersBeforeChange(u32 chunkID, u16 cellID, const u64* layers)
    {
        const u32 key = PackCellAddress(chunkID, cellID);
        if (_activeTransaction.cellLayerDeltaLookup.contains(key))
            return;

        CellLayerDelta delta;
        delta.chunkID = chunkID;
        delta.cellID = cellID;
        std::copy(layers, layers + Map::CellsData::CELL_LAYER_COUNT, delta.before.begin());
        delta.after = delta.before;

        _activeTransaction.cellLayerDeltaLookup[key] = static_cast<u32>(_activeTransaction.cellLayerDeltas.size());
        _activeTransaction.cellLayerDeltas.push_back(std::move(delta));
    }

    void TerrainEditSession::RecordChunkAlphaMapBeforeChange(u32 chunkID, u64 alphaMapHash)
    {
        if (_activeTransaction.chunkAlphaMapDeltaLookup.contains(chunkID))
            return;

        const u32 index = static_cast<u32>(_activeTransaction.chunkAlphaMapDeltas.size());
        _activeTransaction.chunkAlphaMapDeltaLookup[chunkID] = index;
        _activeTransaction.chunkAlphaMapDeltas.push_back({ .chunkID = chunkID, .before = alphaMapHash, .after = alphaMapHash });
    }

    void TerrainEditSession::RefreshDerivedTerrain(const robin_hood::unordered_set<u32>& changedCells, robin_hood::unordered_set<u32>* outAffectedChunkIDs)
    {
        _affectedCellScratch.clear();
        _affectedCellScratch.reserve(changedCells.size() * 9);
        _affectedCellScratch.insert(changedCells.begin(), changedCells.end());

        for (u32 packedCell : changedCells)
        {
            const u32 chunkID = packedCell >> 8;
            const u16 cellID = static_cast<u16>(packedCell & 0xff);
            const i32 chunkX = static_cast<i32>(chunkID % Terrain::CHUNK_NUM_PER_MAP_STRIDE);
            const i32 chunkY = static_cast<i32>(chunkID / Terrain::CHUNK_NUM_PER_MAP_STRIDE);
            const i32 localCellX = cellID % Terrain::CHUNK_NUM_CELLS_PER_STRIDE;
            const i32 localCellY = cellID / Terrain::CHUNK_NUM_CELLS_PER_STRIDE;
            const i32 globalCellX = chunkX * Terrain::CHUNK_NUM_CELLS_PER_STRIDE + localCellX;
            const i32 globalCellY = chunkY * Terrain::CHUNK_NUM_CELLS_PER_STRIDE + localCellY;

            for (i32 offsetY = -1; offsetY <= 1; offsetY++)
            {
                for (i32 offsetX = -1; offsetX <= 1; offsetX++)
                {
                    const i32 neighborX = globalCellX + offsetX;
                    const i32 neighborY = globalCellY + offsetY;
                    if (neighborX < 0 || neighborY < 0 || neighborX >= static_cast<i32>(Terrain::CHUNK_NUM_PER_MAP_STRIDE * Terrain::CHUNK_NUM_CELLS_PER_STRIDE) || neighborY >= static_cast<i32>(Terrain::CHUNK_NUM_PER_MAP_STRIDE * Terrain::CHUNK_NUM_CELLS_PER_STRIDE))
                        continue;

                    const u32 neighborChunkID = static_cast<u32>((neighborX / static_cast<i32>(Terrain::CHUNK_NUM_CELLS_PER_STRIDE)) + (neighborY / static_cast<i32>(Terrain::CHUNK_NUM_CELLS_PER_STRIDE)) * static_cast<i32>(Terrain::CHUNK_NUM_PER_MAP_STRIDE));
                    if (!_loadedChunks.contains(neighborChunkID))
                        continue;

                    const u16 neighborCellID = static_cast<u16>((neighborX % static_cast<i32>(Terrain::CHUNK_NUM_CELLS_PER_STRIDE)) + (neighborY % static_cast<i32>(Terrain::CHUNK_NUM_CELLS_PER_STRIDE)) * static_cast<i32>(Terrain::CHUNK_NUM_CELLS_PER_STRIDE));
                    _affectedCellScratch.insert(PackCellAddress(neighborChunkID, neighborCellID));
                }
            }
        }

        for (auto& [chunkID, cellIDs] : _chunkCellScratch)
            cellIDs.clear();

        for (u32 packedCell : _affectedCellScratch)
        {
            const u32 chunkID = packedCell >> 8;
            const u16 cellID = static_cast<u16>(packedCell & 0xff);
            _chunkCellScratch[chunkID].push_back(cellID);
        }

        bool refreshedBounds = false;
        for (auto& [chunkID, cellIDs] : _chunkCellScratch)
        {
            if (cellIDs.empty())
                continue;

            TerrainLoader::LoadedChunkView editableChunk;
            if (!_terrainLoader.GetEditableChunk(chunkID, editableChunk))
                continue;

            _loadedChunks[chunkID] = editableChunk;
            Map::Chunk& chunk = *editableChunk.chunk;
            for (u16 cellID : cellIDs)
            {
                vec2 heightBounds(std::numeric_limits<f32>::max(), std::numeric_limits<f32>::lowest());

                for (u16 vertexID = 0; vertexID < Terrain::CELL_NUM_VERTICES; vertexID++)
                {
                    const f32 height = chunk.cellsData.heightField[cellID][vertexID];
                    heightBounds.x = glm::min(heightBounds.x, height);
                    heightBounds.y = glm::max(heightBounds.y, height);

                    const vec2 vertexPosition = GetVertexWorldPosition(chunkID, cellID, vertexID);
                    constexpr f32 NORMAL_OFFSET = Terrain::PATCH_HALF_SIZE;
                    f32 left = height;
                    f32 right = height;
                    f32 back = height;
                    f32 front = height;
                    SampleHeight(vertexPosition + vec2(-NORMAL_OFFSET, 0.0f), left);
                    SampleHeight(vertexPosition + vec2(NORMAL_OFFSET, 0.0f), right);
                    SampleHeight(vertexPosition + vec2(0.0f, -NORMAL_OFFSET), back);
                    SampleHeight(vertexPosition + vec2(0.0f, NORMAL_OFFSET), front);
                    const vec3 normal = glm::normalize(vec3(left - right, NORMAL_OFFSET * 2.0f, back - front));

                    chunk.cellsData.normals[cellID][vertexID][0] = static_cast<u8>(glm::round(glm::clamp(normal.x, -1.0f, 1.0f) * 127.0f + 127.0f));
                    chunk.cellsData.normals[cellID][vertexID][1] = static_cast<u8>(glm::round(glm::clamp(normal.y, -1.0f, 1.0f) * 127.0f + 127.0f));
                    chunk.cellsData.normals[cellID][vertexID][2] = static_cast<u8>(glm::round(glm::clamp(normal.z, -1.0f, 1.0f) * 127.0f + 127.0f));
                }

                chunk.cellsData.heightBounds[cellID] = heightBounds;
            }

            if (outAffectedChunkIDs)
                outAffectedChunkIDs->insert(chunkID);

            chunk.heightHeader.gridMinHeight = std::numeric_limits<f32>::max();
            chunk.heightHeader.gridMaxHeight = std::numeric_limits<f32>::lowest();
            for (const vec2& bounds : chunk.cellsData.heightBounds)
            {
                chunk.heightHeader.gridMinHeight = glm::min(chunk.heightHeader.gridMinHeight, bounds.x);
                chunk.heightHeader.gridMaxHeight = glm::max(chunk.heightHeader.gridMaxHeight, bounds.y);
            }

            std::sort(cellIDs.begin(), cellIDs.end());
            cellIDs.erase(std::unique(cellIDs.begin(), cellIDs.end()), cellIDs.end());
            _terrainRenderer.UpdateChunkCells(editableChunk.rendererChunkIndex, chunk, std::span<const u16>(cellIDs));
            refreshedBounds = true;
        }

        if (refreshedBounds)
        {
            RefreshLoadedBounds();
            CVarSystem::Get()->SetIntCVar(CVarCategory::Client | CVarCategory::Rendering, "svsmInvalidateAll"_h, 1);
        }
    }

    void TerrainEditSession::ApplyTransaction(const Transaction& transaction, bool useAfterValues)
    {
        ZoneScopedN("Terrain Edit Apply Transaction");

        _changedCellScratch.clear();
        _changedCellScratch.reserve(transaction.deltas.size() / 8 + 1);
        _editableChunkScratch.clear();
        for (const VertexDelta& delta : transaction.deltas)
        {
            auto editableItr = _editableChunkScratch.find(delta.address.chunkID);
            if (editableItr == _editableChunkScratch.end())
            {
                TerrainLoader::LoadedChunkView editableChunk;
                if (!_terrainLoader.GetEditableChunk(delta.address.chunkID, editableChunk))
                    continue;

                _loadedChunks[editableChunk.chunkID] = editableChunk;
                editableItr = _editableChunkScratch.emplace(editableChunk.chunkID, editableChunk).first;
            }

            editableItr->second.chunk->cellsData.heightField[delta.address.cellID][delta.address.vertexID] = useAfterValues ? delta.after : delta.before;
            _changedCellScratch.insert(PackCellAddress(delta.address.chunkID, delta.address.cellID));
        }

        if (!_changedCellScratch.empty())
            RefreshDerivedTerrain(_changedCellScratch);

        _changedCellScratch.clear();
        _editableChunkScratch.clear();
        for (const VertexColorDelta& delta : transaction.colorDeltas)
        {
            auto editableItr = _editableChunkScratch.find(delta.address.chunkID);
            if (editableItr == _editableChunkScratch.end())
            {
                TerrainLoader::LoadedChunkView editableChunk;
                if (!_terrainLoader.GetEditableChunk(delta.address.chunkID, editableChunk))
                    continue;

                _loadedChunks[editableChunk.chunkID] = editableChunk;
                editableItr = _editableChunkScratch.emplace(editableChunk.chunkID, editableChunk).first;
            }

            const std::array<u8, 3>& value = useAfterValues ? delta.after : delta.before;
            std::copy(value.begin(), value.end(), editableItr->second.chunk->cellsData.colors[delta.address.cellID][delta.address.vertexID]);
            _changedCellScratch.insert(PackCellAddress(delta.address.chunkID, delta.address.cellID));
        }
        if (!_changedCellScratch.empty())
            UploadChangedVertexCells(_changedCellScratch);

        _changedCellScratch.clear();
        for (const CellLayerDelta& delta : transaction.cellLayerDeltas)
        {
            TerrainLoader::LoadedChunkView editableChunk;
            if (!_terrainLoader.GetEditableChunk(delta.chunkID, editableChunk))
                continue;

            _loadedChunks[delta.chunkID] = editableChunk;
            const auto& layers = useAfterValues ? delta.after : delta.before;
            std::copy(layers.begin(), layers.end(), std::begin(editableChunk.chunk->cellsData.layerTextureIDs[delta.cellID]));
            _changedCellScratch.insert(PackCellAddress(delta.chunkID, delta.cellID));
        }

        for (const TextureCellDelta& delta : transaction.textureCellDeltas)
        {
            const std::vector<u8>& value = useAfterValues ? delta.after : delta.before;
            if (value.size() != ALPHA_MAP_CELL_BYTE_SIZE)
                continue;

            EditableAlphaMap* alphaMap = GetOrCreateAlphaMap(delta.chunkID);
            if (!alphaMap)
                continue;

            const size_t cellOffset = static_cast<size_t>(delta.cellID) * ALPHA_MAP_CELL_BYTE_SIZE;
            std::copy(value.begin(), value.end(), alphaMap->rgba.begin() + cellOffset);
            alphaMap->dirty = true;
            _changedCellScratch.insert(PackCellAddress(delta.chunkID, delta.cellID));
        }

        for (const ChunkAlphaMapDelta& delta : transaction.chunkAlphaMapDeltas)
        {
            TerrainLoader::LoadedChunkView editableChunk;
            if (!_terrainLoader.GetEditableChunk(delta.chunkID, editableChunk))
                continue;

            _loadedChunks[delta.chunkID] = editableChunk;
            editableChunk.chunk->chunkAlphaMapTextureHash = useAfterValues ? delta.after : delta.before;
            auto alphaItr = _editableAlphaMaps.find(delta.chunkID);
            if (alphaItr != _editableAlphaMaps.end())
                alphaItr->second.dirty = true;
        }

        if (!_changedCellScratch.empty())
            UploadChangedAlphaCells(_changedCellScratch);
    }

    void TerrainEditSession::MarkTransactionChunksEdited(const Transaction& transaction)
    {
        _transactionChunkScratch.clear();
        _transactionChunkScratch.insert(transaction.affectedChunkIDs.begin(), transaction.affectedChunkIDs.end());
        for (const VertexDelta& delta : transaction.deltas)
            _transactionChunkScratch.insert(delta.address.chunkID);
        for (const VertexColorDelta& delta : transaction.colorDeltas)
            _transactionChunkScratch.insert(delta.address.chunkID);
        for (const TextureCellDelta& delta : transaction.textureCellDeltas)
            _transactionChunkScratch.insert(delta.chunkID);
        for (const CellLayerDelta& delta : transaction.cellLayerDeltas)
            _transactionChunkScratch.insert(delta.chunkID);
        for (const ChunkAlphaMapDelta& delta : transaction.chunkAlphaMapDeltas)
            _transactionChunkScratch.insert(delta.chunkID);

        for (u32 chunkID : _transactionChunkScratch)
        {
            if (_terrainLoader.MarkChunkEdited(chunkID))
                _dirtyChunks.insert(chunkID);
        }

        for (const VertexDelta& delta : transaction.deltas)
        {
            if (_dirtyChunks.contains(delta.address.chunkID))
                _physicsDirtyChunks.insert(delta.address.chunkID);
        }
    }

    void TerrainEditSession::EnforceHistoryBudget()
    {
        while (_historyBytes > HISTORY_MEMORY_BUDGET && !_undoHistory.empty())
        {
            const Transaction& transaction = _undoHistory.front();
            _historyBytes -= std::min(_historyBytes, CalculateTransactionSize(transaction));
            _undoHistory.pop_front();
        }
    }

    size_t TerrainEditSession::CalculateTransactionSize(const Transaction& transaction)
    {
        size_t size = transaction.name.size() +
            transaction.deltas.size() * sizeof(VertexDelta) +
            transaction.colorDeltas.size() * sizeof(VertexColorDelta) +
            transaction.textureCellDeltas.size() * sizeof(TextureCellDelta) +
            transaction.cellLayerDeltas.size() * sizeof(CellLayerDelta) +
            transaction.chunkAlphaMapDeltas.size() * sizeof(ChunkAlphaMapDelta);
        for (const TextureCellDelta& delta : transaction.textureCellDeltas)
            size += delta.before.size() + delta.after.size();

        return size;
    }

    u64 TerrainEditSession::PackVertexAddress(const VertexAddress& address)
    {
        return static_cast<u64>(address.chunkID) | (static_cast<u64>(address.cellID) << 16) | (static_cast<u64>(address.vertexID) << 24);
    }

    u32 TerrainEditSession::PackCellAddress(u32 chunkID, u16 cellID)
    {
        return (chunkID << 8) | cellID;
    }

    vec2 TerrainEditSession::GetVertexWorldPosition(u32 chunkID, u16 cellID, u16 vertexID)
    {
        const i32 chunkX = static_cast<i32>(chunkID % Terrain::CHUNK_NUM_PER_MAP_STRIDE);
        const i32 chunkY = static_cast<i32>(chunkID / Terrain::CHUNK_NUM_PER_MAP_STRIDE);
        const i32 cellX = cellID % Terrain::CHUNK_NUM_CELLS_PER_STRIDE;
        const i32 cellY = cellID / Terrain::CHUNK_NUM_CELLS_PER_STRIDE;
        const i32 packedX = vertexID % Terrain::CELL_GRID_ROW_SIZE;
        const i32 packedY = vertexID / Terrain::CELL_GRID_ROW_SIZE;
        const bool innerVertex = packedX >= Terrain::CELL_OUTER_GRID_STRIDE;

        const f32 vertexX = static_cast<f32>(packedX) - (innerVertex ? 8.5f : 0.0f);
        const f32 vertexY = static_cast<f32>(packedY) + (innerVertex ? 0.5f : 0.0f);
        const f32 mapX = (static_cast<f32>(chunkX * Terrain::CHUNK_NUM_CELLS_PER_STRIDE + cellX) * Terrain::CELL_SIZE) + vertexX * Terrain::PATCH_SIZE;
        const f32 mapY = (static_cast<f32>(chunkY * Terrain::CHUNK_NUM_CELLS_PER_STRIDE + cellY) * Terrain::CELL_SIZE) + vertexY * Terrain::PATCH_SIZE;
        return vec2(mapX - Terrain::MAP_HALF_SIZE, Terrain::MAP_HALF_SIZE - mapY);
    }

    f32 TerrainEditSession::CalculateFalloff(f32 distance, f32 radius, f32 hardness)
    {
        if (hardness >= 1.0f)
            return 1.0f;

        const f32 falloffStart = radius * hardness;
        if (distance <= falloffStart)
            return 1.0f;

        return 1.0f - glm::smoothstep(falloffStart, radius, distance);
    }
}
