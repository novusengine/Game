#include "MapUtil.h"

#include <Base/Math/Math.h>

#include <FileFormat/Shared.h>

namespace Util
{
    namespace Map
    {
        vec2 WorldPositionToChunkGlobalPos(const vec3& position)
        {
            // This is translated to remap positions [-17066 .. 17066] to [0 ..  34132]
            // This is because we want the Chunk Pos to be between [0 .. 64] and not [-32 .. 32]

            return vec2(Terrain::MAP_HALF_SIZE - -position.x, Terrain::MAP_HALF_SIZE - position.z);
        }

        vec2 GetChunkIndicesFromAdtPosition(const vec2& adtPosition)
        {
            return adtPosition / Terrain::CHUNK_SIZE;
        }

        vec2 GetCellIndicesFromAdtPosition(const vec2& adtPosition)
        {
            return adtPosition / Terrain::CELL_SIZE;
        }

        u32 GetChunkIdFromChunkPos(const vec2& chunkPos)
        {
            return Math::FloorToInt(chunkPos.x) + (Math::FloorToInt(chunkPos.y) * Terrain::CHUNK_NUM_PER_MAP_STRIDE);
        }

        vec2 GetChunkPosition(u32 chunkID)
        {
            const u32 chunkX = chunkID % Terrain::CHUNK_NUM_PER_MAP_STRIDE;
            const u32 chunkY = chunkID / Terrain::CHUNK_NUM_PER_MAP_STRIDE;

            const vec2 chunkPos = -Terrain::MAP_HALF_SIZE + (vec2(chunkX, chunkY) * Terrain::CHUNK_SIZE);
            return vec2(chunkPos.x, -chunkPos.y);
        }

        u32 GetCellIdFromCellPos(const vec2& cellPos)
        {
            return Math::FloorToInt(cellPos.y) + (Math::FloorToInt(cellPos.x) * Terrain::CHUNK_NUM_CELLS_PER_STRIDE);
        }

        u32 GetPatchIdFromPatchPos(const vec2& patchPos)
        {
            return Math::FloorToInt(patchPos.y) + (Math::FloorToInt(patchPos.x) * Terrain::CELL_NUM_PATCHES_PER_STRIDE);
        }

        vec2 GetCellPosition(u32 chunkID, u32 cellID)
        {
            const u32 cellX = cellID % Terrain::CHUNK_NUM_CELLS_PER_STRIDE;
            const u32 cellY = cellID / Terrain::CHUNK_NUM_CELLS_PER_STRIDE;

            const vec2 chunkPos = GetChunkPosition(chunkID);
            const vec2 cellPos = vec2(cellX + 1, cellY) * Terrain::CELL_SIZE;

            vec2 cellWorldPos = chunkPos + cellPos;
            return vec2(cellWorldPos.x, -cellWorldPos.y);
        }

        vec2 GetCellVertexPosition(u32 cellID, u32 vertexID)
        {
            const i32 cellX = ((cellID % Terrain::CHUNK_NUM_CELLS_PER_STRIDE));
            const i32 cellY = ((cellID / Terrain::CHUNK_NUM_CELLS_PER_STRIDE));

            const i32 vX = vertexID % 17;
            const i32 vY = vertexID / 17;

            bool isOddRow = vX > 8;

            vec2 vertexOffset;
            vertexOffset.x = -(8.5f * isOddRow);
            vertexOffset.y = (0.5f * isOddRow);

            ivec2 globalVertex = ivec2(vX + cellX * 8, vY + cellY * 8);

            vec2 finalPos = (vec2(globalVertex) + vertexOffset) * Terrain::PATCH_SIZE;

            return vec2(finalPos.x, -finalPos.y);
        }

        vec2 GetGlobalVertexPosition(u32 chunkID, u32 cellID, u32 vertexID)
        {
            const i32 chunkX = chunkID % Terrain::CHUNK_NUM_PER_MAP_STRIDE * Terrain::CHUNK_NUM_CELLS_PER_STRIDE;
            const i32 chunkY = chunkID / Terrain::CHUNK_NUM_PER_MAP_STRIDE * Terrain::CHUNK_NUM_CELLS_PER_STRIDE;

            const i32 cellX = ((cellID % Terrain::CHUNK_NUM_CELLS_PER_STRIDE) + chunkX);
            const i32 cellY = ((cellID / Terrain::CHUNK_NUM_CELLS_PER_STRIDE) + chunkY);

            const i32 vX = vertexID % 17;
            const i32 vY = vertexID / 17;

            bool isOddRow = vX > 8;

            vec2 vertexOffset;
            vertexOffset.x = -(8.5f * isOddRow);
            vertexOffset.y = (0.5f * isOddRow);

            ivec2 globalVertex = ivec2(vX + cellX * 8, vY + cellY * 8);

            vec2 finalPos = -Terrain::MAP_HALF_SIZE + (vec2(globalVertex) + vertexOffset) * Terrain::PATCH_SIZE;

            return vec2(-finalPos.y, finalPos.x);
        }
    }
}