#include "TerrainHeightFieldImport.h"

#include <Renderer/Renderers/Vulkan/Backend/stb_image.h>

#include <json/json.hpp>
#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <unordered_set>

namespace Editor
{
    namespace
    {
        constexpr u32 MAX_IMAGE_DIMENSION = 16384;
        constexpr u64 MAX_IMAGE_PIXELS = 128ull * 1024ull * 1024ull;
        constexpr f32 PATCHES_PER_CHUNK = static_cast<f32>(Terrain::CHUNK_NUM_CELLS_PER_STRIDE * Terrain::CELL_NUM_PATCHES_PER_STRIDE);

        bool ReadFileBytes(const std::filesystem::path& path, std::vector<u8>& outBytes)
        {
            std::ifstream stream(path, std::ios::binary | std::ios::ate);
            if (!stream)
                return false;

            const std::streamsize size = stream.tellg();
            if (size <= 0 || size > static_cast<std::streamsize>(std::numeric_limits<int>::max()))
                return false;

            outBytes.resize(static_cast<size_t>(size));
            stream.seekg(0, std::ios::beg);
            return stream.read(reinterpret_cast<char*>(outBytes.data()), size).good();
        }

        bool ReadVec2(const nlohmann::json& value, vec2& outValue)
        {
            if (!value.is_array() || value.size() != 2 || !value[0].is_number() || !value[1].is_number())
                return false;

            outValue = vec2(value[0].get<f32>(), value[1].get<f32>());
            return std::isfinite(outValue.x) && std::isfinite(outValue.y);
        }

        bool ReadVec4(const nlohmann::json& value, vec4& outValue)
        {
            if (!value.is_array() || value.size() != 4)
                return false;
            for (const nlohmann::json& component : value)
            {
                if (!component.is_number())
                    return false;
            }

            outValue = vec4(value[0].get<f32>(), value[1].get<f32>(), value[2].get<f32>(), value[3].get<f32>());
            return std::isfinite(outValue.x) && std::isfinite(outValue.y) && std::isfinite(outValue.z) && std::isfinite(outValue.w);
        }

        f32 SampleHeight(const TerrainHeightFieldData& data, f32 chunkX, f32 chunkY, f32 minimumHeight, f32 maximumHeight)
        {
            const vec4& bounds = data.manifest.chunkCoordinateBounds;
            const f32 normalizedX = (chunkX - bounds.x) / (bounds.z - bounds.x);
            const f32 normalizedY = (chunkY - bounds.y) / (bounds.w - bounds.y);
            if (normalizedX < 0.0f || normalizedX > 1.0f || normalizedY < 0.0f || normalizedY > 1.0f)
                return minimumHeight;

            const f32 imageX = normalizedX * static_cast<f32>(data.manifest.sourceWidth - 1);
            const f32 imageY = normalizedY * static_cast<f32>(data.manifest.sourceHeight - 1);
            const u32 x0 = static_cast<u32>(std::floor(imageX));
            const u32 y0 = static_cast<u32>(std::floor(imageY));
            const u32 x1 = std::min(x0 + 1, data.manifest.sourceWidth - 1);
            const u32 y1 = std::min(y0 + 1, data.manifest.sourceHeight - 1);
            const f32 fractionX = imageX - static_cast<f32>(x0);
            const f32 fractionY = imageY - static_cast<f32>(y0);
            const size_t rowStride = data.manifest.sourceWidth;
            const f32 top = glm::mix(static_cast<f32>(data.heights[static_cast<size_t>(y0) * rowStride + x0]), static_cast<f32>(data.heights[static_cast<size_t>(y0) * rowStride + x1]), fractionX);
            const f32 bottom = glm::mix(static_cast<f32>(data.heights[static_cast<size_t>(y1) * rowStride + x0]), static_cast<f32>(data.heights[static_cast<size_t>(y1) * rowStride + x1]), fractionX);
            return glm::mix(minimumHeight, maximumHeight, glm::mix(top, bottom, fractionY) / 65535.0f);
        }

        u8 EncodeNormalComponent(f32 component)
        {
            return static_cast<u8>(glm::round(glm::clamp(component, -1.0f, 1.0f) * 127.0f + 127.0f));
        }
    }

    bool LoadTerrainHeightFieldManifest(const std::string& path, TerrainHeightFieldManifest& outManifest, std::string& outError)
    {
        outManifest = {};
        outError.clear();

        try
        {
            std::filesystem::path manifestPath = std::filesystem::absolute(path).lexically_normal();
            std::ifstream stream(manifestPath, std::ios::binary);
            if (!stream)
            {
                outError = "Could not open the import manifest.";
                return false;
            }

            nlohmann::json manifest;
            stream >> manifest;
            if (!manifest.is_object() || manifest.value("version", 0) != 1)
            {
                outError = "Unsupported height-field manifest version.";
                return false;
            }

            const nlohmann::json& source = manifest.at("source");
            const nlohmann::json& encoding = manifest.at("heightEncoding");
            const nlohmann::json& placement = manifest.at("placement");
            const nlohmann::json& sampling = manifest.at("terrainSampling");
            const nlohmann::json& files = manifest.at("files");
            if (encoding.value("format", "") != "PNG_GRAY16_UNORM" || !source.at("width").is_number_unsigned() || !source.at("height").is_number_unsigned())
            {
                outError = "The manifest must describe a 16-bit grayscale PNG.";
                return false;
            }

            TerrainHeightFieldManifest result;
            result.manifestPath = manifestPath.string();
            result.sourceWidth = source.at("width").get<u32>();
            result.sourceHeight = source.at("height").get<u32>();
            const u64 pixelCount = static_cast<u64>(result.sourceWidth) * result.sourceHeight;
            if (result.sourceWidth < 2 || result.sourceHeight < 2 || result.sourceWidth > MAX_IMAGE_DIMENSION || result.sourceHeight > MAX_IMAGE_DIMENSION || pixelCount > MAX_IMAGE_PIXELS)
            {
                outError = "The height image dimensions are outside the supported range.";
                return false;
            }

            if (!ReadVec2(placement.at("footprintChunks"), result.footprintChunks) || !ReadVec4(placement.at("chunkCoordinateBounds"), result.chunkCoordinateBounds) || !ReadVec2(sampling.at("sourcePixelsPerPatch"), result.sourcePixelsPerPatch))
            {
                outError = "The manifest contains invalid placement or sampling values.";
                return false;
            }

            const f32 mapStride = static_cast<f32>(Terrain::CHUNK_NUM_PER_MAP_STRIDE);
            if (result.footprintChunks.x <= 0.0f || result.footprintChunks.y <= 0.0f || result.sourcePixelsPerPatch.x <= 0.0f || result.sourcePixelsPerPatch.y <= 0.0f ||
                result.chunkCoordinateBounds.x < 0.0f || result.chunkCoordinateBounds.y < 0.0f || result.chunkCoordinateBounds.z > mapStride || result.chunkCoordinateBounds.w > mapStride ||
                result.chunkCoordinateBounds.x >= result.chunkCoordinateBounds.z || result.chunkCoordinateBounds.y >= result.chunkCoordinateBounds.w)
            {
                outError = "The import footprint falls outside the terrain map.";
                return false;
            }

            const std::string heightFile = files.at("height").get<std::string>();
            result.heightPath = (manifestPath.parent_path() / std::filesystem::path(heightFile)).lexically_normal().string();
            if (!std::filesystem::is_regular_file(result.heightPath))
            {
                outError = "The manifest's height image does not exist.";
                return false;
            }

            const nlohmann::json& occupiedChunks = manifest.at("occupiedChunks");
            if (!occupiedChunks.is_array() || occupiedChunks.empty() || occupiedChunks.size() > Terrain::CHUNK_NUM_PER_MAP_STRIDE * Terrain::CHUNK_NUM_PER_MAP_STRIDE)
            {
                outError = "The manifest has no valid occupied chunk list.";
                return false;
            }

            std::unordered_set<u32> uniqueChunkIDs;
            result.occupiedChunkIDs.reserve(occupiedChunks.size());
            for (const nlohmann::json& occupiedChunk : occupiedChunks)
            {
                if (!occupiedChunk.is_object() || !occupiedChunk.at("x").is_number_unsigned() || !occupiedChunk.at("y").is_number_unsigned())
                {
                    outError = "The manifest contains an invalid occupied chunk.";
                    return false;
                }

                const u32 x = occupiedChunk.at("x").get<u32>();
                const u32 y = occupiedChunk.at("y").get<u32>();
                if (x >= Terrain::CHUNK_NUM_PER_MAP_STRIDE || y >= Terrain::CHUNK_NUM_PER_MAP_STRIDE)
                {
                    outError = "An occupied chunk falls outside the terrain map.";
                    return false;
                }

                const u32 chunkID = x + y * Terrain::CHUNK_NUM_PER_MAP_STRIDE;
                if (!uniqueChunkIDs.insert(chunkID).second)
                {
                    outError = "The manifest contains duplicate occupied chunks.";
                    return false;
                }
                result.occupiedChunkIDs.push_back(chunkID);
            }

            std::sort(result.occupiedChunkIDs.begin(), result.occupiedChunkIDs.end());
            outManifest = std::move(result);
            return true;
        }
        catch (const std::exception& exception)
        {
            outError = std::string("Invalid height-field manifest: ") + exception.what();
            return false;
        }
    }

    bool LoadTerrainHeightField(const std::string& path, TerrainHeightFieldData& outData, std::string& outError)
    {
        outData = {};
        if (!LoadTerrainHeightFieldManifest(path, outData.manifest, outError))
            return false;

        std::vector<u8> imageBytes;
        if (!ReadFileBytes(outData.manifest.heightPath, imageBytes) || !stbi_is_16_bit_from_memory(imageBytes.data(), static_cast<int>(imageBytes.size())))
        {
            outError = "The height image is not a readable 16-bit image.";
            return false;
        }

        int width = 0;
        int height = 0;
        int channels = 0;
        stbi_us* pixels = stbi_load_16_from_memory(imageBytes.data(), static_cast<int>(imageBytes.size()), &width, &height, &channels, 1);
        if (!pixels)
        {
            outError = std::string("Could not decode the height image: ") + stbi_failure_reason();
            return false;
        }

        const bool dimensionsMatch = width == static_cast<int>(outData.manifest.sourceWidth) && height == static_cast<int>(outData.manifest.sourceHeight);
        if (dimensionsMatch)
            outData.heights.assign(pixels, pixels + static_cast<size_t>(width) * height);
        stbi_image_free(pixels);

        if (!dimensionsMatch)
        {
            outError = "The height image dimensions do not match the manifest.";
            return false;
        }

        return true;
    }

    bool GenerateTerrainHeightFieldChunks(const TerrainHeightFieldData& data, f32 minimumHeight, f32 maximumHeight, std::vector<GeneratedTerrainChunk>& outChunks, std::string& outError)
    {
        outChunks.clear();
        outError.clear();
        if (!std::isfinite(minimumHeight) || !std::isfinite(maximumHeight) || maximumHeight <= minimumHeight || data.heights.size() != static_cast<size_t>(data.manifest.sourceWidth) * data.manifest.sourceHeight)
        {
            outError = "The world height range or source height data is invalid.";
            return false;
        }

        outChunks.reserve(data.manifest.occupiedChunkIDs.size());
        constexpr f32 HALF_PATCH_IN_CHUNKS = 0.5f / PATCHES_PER_CHUNK;
        for (u32 chunkID : data.manifest.occupiedChunkIDs)
        {
            auto chunk = std::make_shared<Map::Chunk>();
            std::memset(&chunk->cellsData, 0, sizeof(chunk->cellsData));
            chunk->chunkAlphaMapTextureHash = Terrain::TEXTURE_ID_INVALID;
            chunk->heightHeader.gridMinHeight = std::numeric_limits<f32>::max();
            chunk->heightHeader.gridMaxHeight = std::numeric_limits<f32>::lowest();

            const u32 chunkX = chunkID % Terrain::CHUNK_NUM_PER_MAP_STRIDE;
            const u32 chunkY = chunkID / Terrain::CHUNK_NUM_PER_MAP_STRIDE;
            for (u32 cellID = 0; cellID < Terrain::CHUNK_NUM_CELLS; cellID++)
            {
                const u32 cellX = cellID % Terrain::CHUNK_NUM_CELLS_PER_STRIDE;
                const u32 cellY = cellID / Terrain::CHUNK_NUM_CELLS_PER_STRIDE;
                vec2 cellBounds(std::numeric_limits<f32>::max(), std::numeric_limits<f32>::lowest());
                for (u32 vertexID = 0; vertexID < Terrain::CELL_TOTAL_GRID_SIZE; vertexID++)
                {
                    const u32 vertexX = vertexID % Terrain::CELL_GRID_ROW_SIZE;
                    const u32 vertexY = vertexID / Terrain::CELL_GRID_ROW_SIZE;
                    const bool innerVertex = vertexX >= Terrain::CELL_OUTER_GRID_STRIDE;
                    const f32 patchX = static_cast<f32>(cellX * Terrain::CELL_NUM_PATCHES_PER_STRIDE + vertexX) - (innerVertex ? 8.5f : 0.0f);
                    const f32 patchY = static_cast<f32>(cellY * Terrain::CELL_NUM_PATCHES_PER_STRIDE + vertexY) + (innerVertex ? 0.5f : 0.0f);
                    const f32 sampleX = static_cast<f32>(chunkX) + patchX / PATCHES_PER_CHUNK;
                    const f32 sampleY = static_cast<f32>(chunkY) + patchY / PATCHES_PER_CHUNK;
                    const f32 height = SampleHeight(data, sampleX, sampleY, minimumHeight, maximumHeight);
                    chunk->cellsData.heightField[cellID][vertexID] = height;
                    cellBounds.x = glm::min(cellBounds.x, height);
                    cellBounds.y = glm::max(cellBounds.y, height);

                    const f32 left = SampleHeight(data, sampleX - HALF_PATCH_IN_CHUNKS, sampleY, minimumHeight, maximumHeight);
                    const f32 right = SampleHeight(data, sampleX + HALF_PATCH_IN_CHUNKS, sampleY, minimumHeight, maximumHeight);
                    const f32 back = SampleHeight(data, sampleX, sampleY + HALF_PATCH_IN_CHUNKS, minimumHeight, maximumHeight);
                    const f32 front = SampleHeight(data, sampleX, sampleY - HALF_PATCH_IN_CHUNKS, minimumHeight, maximumHeight);
                    const vec3 normal = glm::normalize(vec3(left - right, Terrain::PATCH_SIZE, back - front));
                    chunk->cellsData.normals[cellID][vertexID][0] = EncodeNormalComponent(normal.x);
                    chunk->cellsData.normals[cellID][vertexID][1] = EncodeNormalComponent(normal.y);
                    chunk->cellsData.normals[cellID][vertexID][2] = EncodeNormalComponent(normal.z);
                    chunk->cellsData.colors[cellID][vertexID][0] = 255;
                    chunk->cellsData.colors[cellID][vertexID][1] = 255;
                    chunk->cellsData.colors[cellID][vertexID][2] = 255;
                }

                chunk->cellsData.heightBounds[cellID] = cellBounds;
                chunk->heightHeader.gridMinHeight = glm::min(chunk->heightHeader.gridMinHeight, cellBounds.x);
                chunk->heightHeader.gridMaxHeight = glm::max(chunk->heightHeader.gridMaxHeight, cellBounds.y);
            }
            outChunks.push_back({ .chunkID = chunkID, .chunk = std::move(chunk) });
        }

        return true;
    }
}
