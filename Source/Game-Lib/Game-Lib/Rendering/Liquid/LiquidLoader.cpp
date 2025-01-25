#include "LiquidLoader.h"
#include "LiquidRenderer.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Base/CVarSystem/CVarSystem.h>
#include <FileFormat/Novus/Map/MapChunk.h>

AutoCVar_Int CVAR_LiquidLoaderEnabled(CVarCategory::Client, "liquidLoaderEnabled", "enable liquid loading", 1, CVarFlags::EditCheckbox);

LiquidLoader::LiquidLoader(LiquidRenderer* liquidRenderer)
    : _liquidRenderer(liquidRenderer) { }

void LiquidLoader::Init()
{

}

void LiquidLoader::Clear()
{
    LoadRequestInternal dummyRequest;
    while (_requests.try_dequeue(dummyRequest))
    {
        // Just empty the queue
    }

    _liquidRenderer->Clear();
}

void LiquidLoader::Update(f32 deltaTime)
{
    ZoneScoped;

    if (!CVAR_LiquidLoaderEnabled.Get())
        return;

    enki::TaskScheduler* taskScheduler = ServiceLocator::GetTaskScheduler();

    // Count how many unique non-loaded request we have
    u32 numDequeued = static_cast<u32>(_requests.try_dequeue_bulk(&_workingRequests[0], MAX_LOADS_PER_FRAME));
    if (numDequeued == 0)
        return;

    LiquidRenderer::ReserveInfo reserveInfo;

    for (u32 i = 0; i < numDequeued; i++)
    {
        LoadRequestInternal& request = _workingRequests[i];

        reserveInfo.numInstances += request.numInstances;
        reserveInfo.numVertices += request.numVertices;
        reserveInfo.numIndices += request.numIndices;
    }

    // Have LiquidRenderer prepare all buffers for what we need to load
    LiquidReserveOffsets reserveOffsets;
    _liquidRenderer->Reserve(reserveInfo, reserveOffsets);

    std::atomic<u32> instanceOffset = reserveOffsets.instanceStartOffset;
    std::atomic<u32> vertexOffset = reserveOffsets.vertexStartOffset;
    std::atomic<u32> indexOffset = reserveOffsets.indexStartOffset;

#if 0
    for (u32 i = 0; i < numDequeued; i++)
    {
        LoadRequestInternal& request = _workingRequests[i];
        LoadRequest(request, instanceOffset, vertexOffset, indexOffset);
    }
#else
    enki::TaskSet loadLiquidTask(numDequeued, [&](enki::TaskSetPartition range, u32 threadNum)
    {
        for (u32 i = range.start; i < range.end; i++)
        {
            LoadRequestInternal& request = _workingRequests[i];
            LoadRequest(request, instanceOffset, vertexOffset, indexOffset);
        }
    });
    
    // Execute the multithreaded job
    taskScheduler->AddTaskSetToPipe(&loadLiquidTask);
    taskScheduler->WaitforTask(&loadLiquidTask);
#endif
}

vec2 GetChunkPosition(u32 chunkID)
{
    const u32 chunkX = chunkID / Terrain::CHUNK_NUM_PER_MAP_STRIDE;
    const u32 chunkY = chunkID % Terrain::CHUNK_NUM_PER_MAP_STRIDE;

    const vec2 chunkPos = -Terrain::MAP_HALF_SIZE + (vec2(chunkX, chunkY) * Terrain::CHUNK_SIZE);
    return chunkPos;
}

vec2 GetCellPosition(u32 chunkID, u32 cellID)
{
    const u32 cellX = cellID % Terrain::CHUNK_NUM_CELLS_PER_STRIDE;
    const u32 cellY = cellID / Terrain::CHUNK_NUM_CELLS_PER_STRIDE;

    const vec2 chunkPos = GetChunkPosition(chunkID);
    const vec2 cellPos = vec2(cellX+1, cellY) * Terrain::CELL_SIZE;

    vec2 cellWorldPos = chunkPos + cellPos;
    return vec2(cellWorldPos.x, -cellWorldPos.y);
}

void LiquidLoader::LoadFromChunk(u16 chunkX, u16 chunkY, const Map::LiquidInfo* liquidInfo)
{
    if (!CVAR_LiquidLoaderEnabled.Get())
        return;

    if (liquidInfo->headers.size() == 0)
        return;

    LoadRequestInternal request;
    request.chunkX = chunkX;
    request.chunkY = chunkY;
    request.liquidInfo = liquidInfo;

    u32 instanceIndex = 0;
    u32 numVertices = 0;
    u32 numIndices = 0;

    for (u32 i = 0; i < liquidInfo->headers.size(); i++)
    {
        const Map::CellLiquidHeader& header = liquidInfo->headers[i];
        u32 numInstances = (header.packedData & 0x7F);

        u32 start = instanceIndex;
        u32 end = instanceIndex + numInstances;

        for (u32 j = start; j < end; j++)
        {
            const Map::CellLiquidInstance& liquidInstance = liquidInfo->instances[j];

            u8 height = liquidInstance.packedSize >> 4;
            u8 width = liquidInstance.packedSize & 0xF;

            if (width == 0 || height == 0)
                continue;

            u32 vertexCount = (width + 1) * (height + 1);
            numVertices += vertexCount;

            u32 indexCount = width * height * 6;
            numIndices += indexCount;
        }

        instanceIndex += numInstances;
    }

    request.numInstances = instanceIndex;
    request.numVertices = numVertices;
    request.numIndices = numIndices;

    _requests.enqueue(request);
}

void LiquidLoader::LoadRequest(LoadRequestInternal& request, std::atomic<u32>& instanceOffset, std::atomic<u32>& vertexOffset, std::atomic<u32>& indexOffset)
{
    const Map::LiquidInfo* liquidInfo = request.liquidInfo;
    u32 chunkID = (request.chunkY * Terrain::CHUNK_NUM_PER_MAP_STRIDE) + request.chunkX;

    u32 numTotalInstances = static_cast<u32>(liquidInfo->instances.size());
    if (numTotalInstances == 0)
        return;

    u32 instanceStartIndex = instanceOffset.fetch_add(numTotalInstances);

    u32 instanceIndex = 0;
    for (u32 i = 0; i < liquidInfo->headers.size(); i++)
    {
        const Map::CellLiquidHeader& header = liquidInfo->headers[i];

        u16 cellID = i;
        u32 numInstances = (header.packedData & 0x7F);

        u32 start = instanceIndex;
        u32 end = instanceIndex + numInstances;
        instanceIndex += numInstances;

        for (u32 j = start; j < end; j++)
        {
            const Map::CellLiquidInstance& liquidInstance = liquidInfo->instances[j];

            u8 posX = liquidInstance.packedOffset & 0xF;
            u8 posY = liquidInstance.packedOffset >> 4;

            u8 width = liquidInstance.packedSize & 0xF;
            u8 height = liquidInstance.packedSize >> 4;

            u32 vertexCount = (width + 1) * (height + 1);
            u32 bitMapBytes = (width * height + 7) / 8;

            bool hasVertexData = liquidInstance.packedData >> 7;
            bool hasBitmapData = (liquidInstance.packedData >> 6) & 0x1;
            u16 liquidVertexFormat = liquidInstance.packedData & 0x3F;

            const f32* heightMap = nullptr;
            const u8* bitMap = nullptr;

            size_t vertexDataSize = liquidInfo->vertexData.size();
            if (vertexDataSize > 0 && hasVertexData)
            {
                heightMap = reinterpret_cast<const f32*>(&liquidInfo->vertexData[liquidInstance.vertexDataOffset]);
            }

            size_t bitmapDataSize = liquidInfo->bitmapData.size();
            if (bitmapDataSize > 0 && hasBitmapData)
            {
                bitMap = &liquidInfo->bitmapData[liquidInstance.bitmapDataOffset];
            }

            LiquidRenderer::LoadDesc desc;
            desc.chunkID = chunkID;
            desc.cellID = cellID;
            desc.typeID = liquidInstance.liquidTypeID;

            desc.posX = posX;
            desc.posY = posY;
            desc.width = width;
            desc.height = height;

            desc.startX = 0;
            desc.endX = 8;
            desc.startY = 0;
            desc.endY = 8;

            if (liquidVertexFormat != 2)
            {
                desc.startX = desc.posX;
                desc.endX = desc.startX + desc.width;

                desc.startY = desc.posY;
                desc.endY = desc.startY + desc.height;
            }

            desc.cellPos = GetCellPosition(chunkID, cellID);

            desc.defaultHeight = liquidInstance.height;
            desc.heightMap = heightMap;
            desc.bitMap = bitMap;

            desc.vertexCount = (width + 1) * (height + 1);
            desc.vertexOffset = vertexOffset.fetch_add(desc.vertexCount);

            desc.indexCount = width * height * 6;
            desc.indexOffset = indexOffset.fetch_add(desc.indexCount);

            desc.instanceOffset = instanceStartIndex + j;

            _liquidRenderer->Load(desc);
        }
    }
}
