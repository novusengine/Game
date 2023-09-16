#include "WaterLoader.h"
#include "WaterRenderer.h"

#include <FileFormat/Novus/Map/MapChunk.h>

AutoCVar_Int CVAR_WaterLoaderEnabled("waterLoader.enabled", "enable water loading", 0, CVarFlags::EditCheckbox);
AutoCVar_Int CVAR_WaterLoaderNumThreads("waterLoader.numThreads", "number of threads used for water loading, 0 = number of hardware threads", 0, CVarFlags::None);

WaterLoader::WaterLoader(WaterRenderer* waterRenderer)
	: _waterRenderer(waterRenderer)
{
    i32 numThreads = CVAR_WaterLoaderNumThreads.Get();
    if (numThreads == 0 || numThreads == -1)
    {
        _scheduler.Initialize();
    }
    else
    {
        _scheduler.Initialize(numThreads);
    }
}

void WaterLoader::Init()
{

}

void WaterLoader::Clear()
{

}

void WaterLoader::Update(f32 deltaTime)
{
    if (!CVAR_WaterLoaderEnabled.Get())
        return;

    // Count how many unique non-loaded request we have
    u32 numDequeued = static_cast<u32>(_requests.try_dequeue_bulk(&_workingRequests[0], MAX_LOADS_PER_FRAME));
    if (numDequeued == 0)
        return;

    WaterRenderer::ReserveInfo reserveInfo;

    u32 bitmapOffset = 0;
    std::vector<u32> bitmapOffsets;
    bitmapOffsets.reserve(numDequeued);

    u32 vertexDataOffset = 0;
    std::vector<u32> vertexDataOffsets;
    vertexDataOffsets.reserve(numDequeued);

    for (u32 i = 0; i < numDequeued; i++)
    {
        LoadRequestInternal& request = _workingRequests[i];

        reserveInfo.numInstances += static_cast<u32>(request.instances.size());

        bitmapOffsets.push_back(bitmapOffset);
        vertexDataOffsets.push_back(vertexDataOffset);

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

            // Bitmap offset
            u32 bitMapBytes = (width * height + 7) / 8;
            bitmapOffset += bitMapBytes;

            // Vertex data offset
            bool hasVertexData = instance.packedData >> 7;
            u16 liquidVertexFormat = instance.packedData & 0x3F;

            if (hasVertexData)
            {
                if (liquidVertexFormat == 0) // LiquidVertexFormat_Height
                {
                    vertexDataOffset += vertexCount * sizeof(f32);
                }
                else if (liquidVertexFormat == 1 || liquidVertexFormat == 3) // LiquidVertexFormat_Height_UV
                {
                    const size_t uvEntriesSize = sizeof(u16) * 2;
                    vertexDataOffset += vertexCount * (sizeof(f32) + uvEntriesSize);
                }
                else
                {
                    DebugHandler::PrintFatal("Unknown liquid vertex format: {}", liquidVertexFormat);
                }
            }
		}
    }

    // Have WaterRenderer prepare all buffers for what we need to load
    _waterRenderer->Reserve(reserveInfo);

#if 1
    for (u32 i = 0; i < numDequeued; i++)
    {
        LoadRequestInternal& request = _workingRequests[i];
        LoadRequest(request, bitmapOffsets[i], vertexDataOffsets[i]);
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
    _scheduler.AddTaskSetToPipe(&loadModelsTask);
    _scheduler.WaitforTask(&loadModelsTask);
#endif

    _waterRenderer->FitAfterGrow();
}

vec2 GetChunkPosition(u32 chunkID)
{
    const u32 chunkX = chunkID / Terrain::CHUNK_NUM_PER_MAP_STRIDE;
    const u32 chunkY = chunkID % Terrain::CHUNK_NUM_PER_MAP_STRIDE;

    const vec2 chunkPos = Terrain::MAP_HALF_SIZE - (vec2(chunkX, chunkY) * Terrain::CHUNK_SIZE);
    return chunkPos;
}

vec2 GetCellPosition(u32 chunkID, u32 cellID)
{
    const u32 cellX = cellID / Terrain::CHUNK_NUM_CELLS_PER_STRIDE;
    const u32 cellY = cellID % Terrain::CHUNK_NUM_CELLS_PER_STRIDE;

    const vec2 chunkPos = GetChunkPosition(chunkID);
    const vec2 cellPos = vec2(cellX, cellY) * Terrain::CELL_SIZE;
    vec2 cellWorldPos = chunkPos + cellPos;

    f32 x = cellWorldPos.x;
    cellWorldPos.x = -cellWorldPos.y;
    cellWorldPos.y = x;

    return cellWorldPos;
}

void WaterLoader::LoadFromChunk(u16 chunkX, u16 chunkY, Map::LiquidInfo* liquidInfo)
{
    if (!CVAR_WaterLoaderEnabled.Get())
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
            Map::CellLiquidInstance& instance = liquidInfo->instances[j];

            LiquidInstance& liquidInstance = request.instances.emplace_back();
            liquidInstance.cellID = cellID;
            liquidInstance.typeID = instance.liquidTypeID;
            liquidInstance.packedData = instance.packedData;
            liquidInstance.packedOffset = instance.packedOffset;
            liquidInstance.packedSize = instance.packedSize;
        }

        instanceIndex += numInstances;
	}

    size_t vertexDataSize = liquidInfo->vertexData.size();
    if (vertexDataSize > 0)
    {
        request.vertexData = new u8[vertexDataSize];
        memcpy(request.vertexData, liquidInfo->vertexData.data(), vertexDataSize * sizeof(u8));
    }
    
    size_t bitmapDataSize = liquidInfo->bitmapData.size();
    if (bitmapDataSize > 0)
    {
        request.bitmapData = new u8[bitmapDataSize];
        memcpy(request.bitmapData, liquidInfo->bitmapData.data(), bitmapDataSize * sizeof(u8));
    }

    _requests.enqueue(request);
}

void WaterLoader::LoadRequest(LoadRequestInternal& request, u32 bitmapOffset, u32 vertexDataOffset)
{
    u32 chunkID = (request.chunkY * Terrain::CHUNK_NUM_PER_MAP_STRIDE) + request.chunkX;
    //vec2 chunkPos = GetChunkPosition(chunkID);

    //vec3 chunkBasePos = Terrain::MAP_HALF_SIZE - vec3(Terrain::CHUNK_SIZE * request.chunkY, Terrain::CHUNK_SIZE * request.chunkX, Terrain::MAP_HALF_SIZE);

    //u32 liquidInstanceIndex = 0;
    u32 liquidBitmapDataOffset = 0;
    u32 liquidVertexDataOffset = 0;

    for (u32 i = 0; i < request.instances.size(); i++)
    {
        WaterLoader::LiquidInstance& liquidInstance = request.instances[i];

        u16 cellID = liquidInstance.cellID;

        u16 cellX = cellID % Terrain::CHUNK_NUM_CELLS_PER_STRIDE;
        u16 cellY = cellID / Terrain::CHUNK_NUM_CELLS_PER_STRIDE;
        //vec3 liquidBasePos = chunkBasePos - vec3(Terrain::CELL_SIZE * cellY, Terrain::CELL_SIZE * cellX, 0);
        //liquidBasePos.x = -liquidBasePos.x;
        //liquidBasePos.y = -liquidBasePos.y;

        const vec2 cellPos = GetCellPosition(chunkID, cellID);

        u8 posX = liquidInstance.packedOffset & 0xF;
        u8 posY = liquidInstance.packedOffset >> 4;

        u8 height = liquidInstance.packedSize >> 4;
        u8 width = liquidInstance.packedSize & 0xF;

        u8 liquidOffsetX = liquidInstance.packedOffset & 0xF;
        u8 liquidOffsetY = liquidInstance.packedOffset >> 4;

        u32 vertexCount = (width + 1) * (height + 1);
        u32 bitMapBytes = (width * height + 7) / 8;

        bool hasVertexData = liquidInstance.packedData >> 7;
        bool hasBitmapData = (liquidInstance.packedData >> 6) & 0x1;
        u16 liquidVertexFormat = liquidInstance.packedData & 0x3F;

        f32* heightMap = nullptr;
        //Terrain::LiquidUVMapEntry* uvEntries = nullptr;
        
        if (hasVertexData)
        {
            if (liquidVertexFormat == 0) // LiquidVertexFormat_Height
            {
                heightMap = reinterpret_cast<f32*>(&request.vertexData[liquidVertexDataOffset]);
                liquidVertexDataOffset += vertexCount * sizeof(f32);
            }
            else if (liquidVertexFormat == 1 || liquidVertexFormat == 3) // LiquidVertexFormat_Height_UV
            {
                heightMap = reinterpret_cast<f32*>(&request.vertexData[liquidVertexDataOffset]);
                
                //uvEntries = reinterpret_cast<Terrain::LiquidUVMapEntry*>(&request.vertexData[liquidVertexDataOffset + (vertexCount * sizeof(f32))]);
                const size_t uvEntriesSize = sizeof(u16) * 2;

                liquidVertexDataOffset += vertexCount * (sizeof(f32) + uvEntriesSize);// sizeof(Terrain::LiquidUVMapEntry));
            }
        }

        WaterRenderer::LoadDesc desc;
        desc.chunkID = chunkID;
        desc.cellID = cellID;
        desc.typeID = liquidInstance.typeID;

        desc.posX = posX;
        desc.posY = posY;
        desc.width = width;
        desc.height = height;
        desc.cellPos = cellPos;
        desc.liquidOffsetX = liquidOffsetX;
        desc.liquidOffsetY = liquidOffsetY;

        desc.bitmapDataOffset = liquidBitmapDataOffset;

        desc.heightMap = heightMap;
        desc.bitMap = request.bitmapData;

        _waterRenderer->Load(desc);

        // TODO: Get rid of this...
        liquidBitmapDataOffset += bitMapBytes;
    }

    delete request.vertexData; // TODO: Avoid this
}
