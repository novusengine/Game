#include "LiquidLoader.h"
#include "LiquidRenderer.h"
#include "Game/Util/ServiceLocator.h"

#include <Base/CVarSystem/CVarSystem.h>
#include <FileFormat/Novus/Map/MapChunk.h>

AutoCVar_Int CVAR_LiquidLoaderEnabled("liquidLoader.enabled", "enable liquid loading", 1, CVarFlags::EditCheckbox);

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

        reserveInfo.numInstances += static_cast<u32>(request.instances.size());

        for (LiquidInstance& instance : request.instances)
        {
            u8 height = instance.packedSize >> 4;
            u8 width = instance.packedSize & 0xF;

            if (width == 0 || height == 0)
				continue;

            u32 vertexCount = (width + 1) * (height + 1);
			reserveInfo.numVertices += vertexCount;

            u32 indexCount = width * height * 6;
            reserveInfo.numIndices += indexCount;
		}
    }

    // Have LiquidRenderer prepare all buffers for what we need to load
    _liquidRenderer->Reserve(reserveInfo);

#if 0
    for (u32 i = 0; i < numDequeued; i++)
    {
        LoadRequestInternal& request = _workingRequests[i];
        LoadRequest(request);
    }
#else
    enki::TaskSet loadModelsTask(numDequeued, [&](enki::TaskSetPartition range, u32 threadNum)
    {
        for (u32 i = range.start; i < range.end; i++)
        {
            LoadRequestInternal& request = _workingRequests[i];
            LoadRequest(request);
        }
    });
    
    // Execute the multithreaded job
    taskScheduler->AddTaskSetToPipe(&loadModelsTask);
    taskScheduler->WaitforTask(&loadModelsTask);
#endif

    _liquidRenderer->FitAfterGrow();
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

void LiquidLoader::LoadFromChunk(u16 chunkX, u16 chunkY, Map::LiquidInfo* liquidInfo)
{
    if (!CVAR_LiquidLoaderEnabled.Get())
        return;

    if (liquidInfo->headers.size() == 0)
        return;

    LoadRequestInternal request;
    request.chunkX = chunkX;
    request.chunkY = chunkY;

    u32 instanceIndex = 0;
    for (u32 i = 0; i < liquidInfo->headers.size(); i++)
    {
		Map::CellLiquidHeader& header = liquidInfo->headers[i];

        u32 cellID = i; // Directly corresponding to the header index?
        u32 numInstances = (header.packedData & 0x7F);

        u32 start = instanceIndex;
        u32 end = instanceIndex + numInstances;
		
        for (u32 j = start; j < end; j++)
        {
            const Map::CellLiquidInstance& instance = liquidInfo->instances[j];

            LiquidInstance& liquidInstance = request.instances.emplace_back();
            liquidInstance.cellID = cellID;
            liquidInstance.typeID = instance.liquidTypeID;
            liquidInstance.packedData = instance.packedData;
            liquidInstance.packedOffset = instance.packedOffset;
            liquidInstance.packedSize = instance.packedSize;
            liquidInstance.height = instance.height;
            liquidInstance.bitmapDataOffset = instance.bitmapDataOffset;
            liquidInstance.vertexDataOffset = instance.vertexDataOffset;
        }

        instanceIndex += numInstances;
	}

    size_t bitmapDataSize = liquidInfo->bitmapData.size();
    if (bitmapDataSize > 0)
    {
        request.bitmapData = new u8[bitmapDataSize];
        memcpy(request.bitmapData, liquidInfo->bitmapData.data(), bitmapDataSize * sizeof(u8));
    }
    else
    {
        request.vertexData = nullptr;
    }

    size_t vertexDataSize = liquidInfo->vertexData.size();
    if (vertexDataSize > 0)
    {
        request.vertexData = new u8[vertexDataSize];
        memcpy(request.vertexData, liquidInfo->vertexData.data(), vertexDataSize * sizeof(u8));
    }
    else
    {
        request.vertexData = nullptr;
    }

    _requests.enqueue(request);
}

void LiquidLoader::LoadRequest(LoadRequestInternal& request)
{
    u32 chunkID = (request.chunkY * Terrain::CHUNK_NUM_PER_MAP_STRIDE) + request.chunkX;

    for (u32 i = 0; i < request.instances.size(); i++)
    {
        LiquidLoader::LiquidInstance& liquidInstance = request.instances[i];

        u16 cellID = liquidInstance.cellID;

        u16 cellX = cellID % Terrain::CHUNK_NUM_CELLS_PER_STRIDE;
        u16 cellY = cellID / Terrain::CHUNK_NUM_CELLS_PER_STRIDE;

        const vec2 cellPos = GetCellPosition(chunkID, cellID);

        u8 posX = liquidInstance.packedOffset & 0xF;
        u8 posY = liquidInstance.packedOffset >> 4;

        u8 width = liquidInstance.packedSize & 0xF;
        u8 height = liquidInstance.packedSize >> 4;

        u32 vertexCount = (width + 1) * (height + 1);
        u32 bitMapBytes = (width * height + 7) / 8;

        bool hasVertexData = liquidInstance.packedData >> 7;
        bool hasBitmapData = (liquidInstance.packedData >> 6) & 0x1;
        u16 liquidVertexFormat = liquidInstance.packedData & 0x3F;

        f32* heightMap = nullptr;
        u8* bitMap = nullptr;
        
        if (request.vertexData != nullptr && hasVertexData)
        {
            heightMap = reinterpret_cast<f32*>(&request.vertexData[liquidInstance.vertexDataOffset]);
        }

        if (request.bitmapData != nullptr && hasBitmapData)
        {
            bitMap = &request.bitmapData[liquidInstance.bitmapDataOffset];
        }

        LiquidRenderer::LoadDesc desc;
        desc.chunkID = chunkID;
        desc.cellID = cellID;
        desc.typeID = liquidInstance.typeID;

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

        desc.cellPos = cellPos;

        desc.defaultHeight = liquidInstance.height;
        desc.heightMap = heightMap;
        desc.bitMap = bitMap;

        _liquidRenderer->Load(desc);
    }

    if (request.vertexData)
        delete request.vertexData; // TODO: Avoid this

    if (request.bitmapData)
        delete request.bitmapData;  // TODO: Avoid this
}
