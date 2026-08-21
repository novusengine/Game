#pragma once

#include <Base/Types.h>

#include <FileFormat/Novus/Map/MapChunk.h>

#include <memory>
#include <string>
#include <vector>

namespace Editor
{
    struct TerrainHeightFieldManifest
    {
    public:
        std::string manifestPath;
        std::string heightPath;
        u32 sourceWidth = 0;
        u32 sourceHeight = 0;
        vec2 footprintChunks = vec2(0.0f);
        vec4 chunkCoordinateBounds = vec4(0.0f);
        vec2 sourcePixelsPerPatch = vec2(0.0f);
        std::vector<u32> occupiedChunkIDs;
    };

    struct TerrainHeightFieldData
    {
    public:
        TerrainHeightFieldManifest manifest;
        std::vector<u16> heights;
    };

    struct GeneratedTerrainChunk
    {
    public:
        u32 chunkID = Terrain::CHUNK_INVALID_ID;
        std::shared_ptr<Map::Chunk> chunk;
    };

    bool LoadTerrainHeightFieldManifest(const std::string& path, TerrainHeightFieldManifest& outManifest, std::string& outError);
    bool LoadTerrainHeightField(const std::string& path, TerrainHeightFieldData& outData, std::string& outError);
    bool GenerateTerrainHeightFieldChunks(const TerrainHeightFieldData& data, f32 minimumHeight, f32 maximumHeight, std::vector<GeneratedTerrainChunk>& outChunks, std::string& outError);
}
