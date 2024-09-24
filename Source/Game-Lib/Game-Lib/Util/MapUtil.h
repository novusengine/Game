#pragma once
#include <Base/Types.h>

namespace Util
{
    namespace Map
    {
        vec2 WorldPositionToChunkGlobalPos(const vec3& position);
     
        vec2 GetChunkIndicesFromAdtPosition(const vec2& adtPosition);
        vec2 GetCellIndicesFromAdtPosition(const vec2& adtPosition);
        u32  GetChunkIdFromChunkPos(const vec2& chunkPos);
        vec2 GetChunkPosition(u32 chunkID);
        u32  GetCellIdFromCellPos(const vec2& cellPos);
        u32  GetPatchIdFromPatchPos(const vec2& patchPos);
        vec2 GetCellPosition(u32 chunkID, u32 cellID);

        vec2 GetCellVertexPosition(u32 cellID, u32 vertexID);
        vec2 GetGlobalVertexPosition(u32 chunkID, u32 cellID, u32 vertexID);
    }
}