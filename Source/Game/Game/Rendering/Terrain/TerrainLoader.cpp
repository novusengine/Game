#include "TerrainLoader.h"
#include "TerrainRenderer.h"
#include "Game/Application/EnttRegistries.h"
#include "Game/ECS/Singletons/JoltState.h"
#include "Game/Rendering/GameRenderer.h"
#include "Game/Rendering/Debug/DebugRenderer.h"
#include "Game/Rendering/Model/ModelLoader.h"
#include "Game/Rendering/Water/WaterLoader.h"
#include "Game/Util/ServiceLocator.h"

#include <Base/CVarSystem/CVarSystem.h>
#include <Base/Memory/FileReader.h>
#include <Base/Util/StringUtils.h>

#include <FileFormat/Novus/Map/Map.h>
#include <FileFormat/Novus/Map/MapChunk.h>

#include <Jolt/Jolt.h>
#include <Jolt/Geometry/Triangle.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>

#include <entt/entt.hpp>

#include <atomic>
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

AutoCVar_Int CVAR_TerrainLoaderPhysicsEnabled("terrainLoader.physics.enabled", "enable loading the terrain into the physics engine", 0, CVarFlags::EditCheckbox);
AutoCVar_Int CVAR_TerrainLoaderPhysicsOptimizeBP("terrainLoader.physics.optimizeBP", "enables optimizing the broadphase", 1, CVarFlags::EditCheckbox);

TerrainLoader::TerrainLoader(TerrainRenderer* terrainRenderer, ModelLoader* modelLoader, WaterLoader* waterLoader)
	: _terrainRenderer(terrainRenderer)
	, _modelLoader(modelLoader)
	, _waterLoader(waterLoader)
	, _requests()
{
	_scheduler.Initialize();
}

void TerrainLoader::Update(f32 deltaTime)
{
	LoadRequestInternal loadRequest;

	while (_requests.try_dequeue(loadRequest))
	{
		if (loadRequest.loadType == LoadType::Partial)
		{
			// TODO : Disabled as this needs fixing
			//LoadPartialMapRequest(loadRequest);
		}
		else if (loadRequest.loadType == LoadType::Full)
		{
			LoadFullMapRequest(loadRequest);
		}
		else
		{
			DebugHandler::PrintFatal("TerrainLoader : Encountered LoadRequest with invalid LoadType");
		}
	}
}

void TerrainLoader::AddInstance(const LoadDesc& loadDesc)
{
	LoadRequestInternal loadRequest;
	loadRequest.loadType = loadDesc.loadType;
	loadRequest.mapName = loadDesc.mapName;
	loadRequest.chunkGridStartPos = loadDesc.chunkGridStartPos;
	loadRequest.chunkGridEndPos = loadDesc.chunkGridEndPos;

	_requests.enqueue(loadRequest);
}

void TerrainLoader::LoadPartialMapRequest(const LoadRequestInternal& request)
{
	std::string mapName = request.mapName;
	fs::path absoluteMapPath = fs::absolute("Data/Map/" + mapName);

	assert(request.loadType == LoadType::Partial);
	assert(request.mapName.size() > 0);
	assert(request.chunkGridStartPos.x <= request.chunkGridEndPos.x);
	assert(request.chunkGridStartPos.y <= request.chunkGridEndPos.y);

	u32 gridWidth = (request.chunkGridEndPos.x - request.chunkGridStartPos.x) + 1;
	u32 gridHeight = (request.chunkGridEndPos.y - request.chunkGridStartPos.y) + 1;

	u32 startChunkNum = request.chunkGridStartPos.x + (request.chunkGridStartPos.y * Terrain::CHUNK_NUM_PER_MAP_STRIDE);
	u32 chunksToLoad = gridHeight * gridWidth;

	std::atomic<u32> numExistingChunks = 0;
	enki::TaskSet countValidChunksTask(chunksToLoad, [&, startChunkNum](enki::TaskSetPartition range, uint32_t threadNum)
	{
		u32 localNumExistingChunks = 0;

		std::string chunkLocalPathStr = "";
		chunkLocalPathStr.reserve(128);

		for (u32 i = range.start; i < range.end; i++)
		{
			u32 chunkX = request.chunkGridStartPos.x + (i % gridWidth);
			u32 chunkY = request.chunkGridStartPos.y + (i / gridHeight);

			chunkLocalPathStr.clear();
			chunkLocalPathStr += mapName + "_" + std::to_string(chunkX) + "_" + std::to_string(chunkY) + ".chunk";

			fs::path chunkLocalPath = chunkLocalPathStr;
			fs::path chunkPath = absoluteMapPath / chunkLocalPath;

			if (!fs::exists(chunkPath))
				continue;

			localNumExistingChunks++;
		}

		numExistingChunks.fetch_add(localNumExistingChunks);
	});

	enki::TaskSet loadChunksTask(chunksToLoad, [&, startChunkNum](enki::TaskSetPartition range, uint32_t threadNum)
	{
		std::string chunkLocalPathStr = "";
		chunkLocalPathStr.reserve(128);

		for (u32 i = range.start; i < range.end; i++)
		{
			u32 chunkX = request.chunkGridStartPos.x + (i % gridWidth);
			u32 chunkY = request.chunkGridStartPos.y + (i / gridHeight);

			chunkLocalPathStr.clear();
			chunkLocalPathStr += mapName + "_" + std::to_string(chunkX) + "_" + std::to_string(chunkY) + ".chunk";

			fs::path chunkLocalPath = chunkLocalPathStr;
			fs::path chunkPath = absoluteMapPath / chunkLocalPath;

			if (!fs::exists(chunkPath))
				continue;

			std::string chunkPathStr = chunkPath.string();

			FileReader reader(chunkPathStr);
			if (reader.Open())
			{
				size_t bufferSize = reader.Length();

				std::shared_ptr<Bytebuffer> chunkBuffer = Bytebuffer::BorrowRuntime(bufferSize);
				reader.Read(chunkBuffer.get(), bufferSize);

				u32 chunkHash = StringUtils::fnv1a_32(chunkPathStr.c_str(), chunkPathStr.size());
				Map::Chunk* chunk = reinterpret_cast<Map::Chunk*>(chunkBuffer->GetDataPointer());

				u32 chunkDataID = _terrainRenderer->AddChunk(chunkHash, chunk, ivec2(chunkX, chunkY));

				for (Terrain::Placement& placement : chunk->complexModelPlacements)
				{
					_modelLoader->LoadPlacement(placement);
				}
			}
		}
	});

	DebugHandler::Print("TerrainLoader : Started Preparing Chunk Loading");
	_scheduler.AddTaskSetToPipe(&countValidChunksTask);
	_scheduler.WaitforTask(&countValidChunksTask);
	DebugHandler::Print("TerrainLoader : Finished Preparing Chunk Loading");

	u32 numChunksToLoad = numExistingChunks.load();
	if (numChunksToLoad == 0)
	{
		DebugHandler::PrintError("TerrainLoader : Failed to prepare chunks for map '{0}'", request.mapName);
		return;
	}

	_terrainRenderer->ReserveChunks(numChunksToLoad);

	DebugHandler::Print("TerrainLoader : Started Chunk Loading");
	_scheduler.AddTaskSetToPipe(&loadChunksTask);
	_scheduler.WaitforTask(&loadChunksTask);
	DebugHandler::Print("TerrainLoader : Finished Chunk Loading");
}

vec2 GetGlobalVertexPosition(u32 chunkID, u32 cellID, u32 vertexID)
{
	const u32 chunkX = chunkID / Terrain::CHUNK_NUM_PER_MAP_STRIDE * Terrain::CHUNK_NUM_CELLS_PER_STRIDE;
	const u32 chunkY = chunkID % Terrain::CHUNK_NUM_PER_MAP_STRIDE * Terrain::CHUNK_NUM_CELLS_PER_STRIDE;

	const u32 cellX = ((cellID % Terrain::CHUNK_NUM_CELLS_PER_STRIDE) + chunkX);
	const u32 cellY = ((cellID / Terrain::CHUNK_NUM_CELLS_PER_STRIDE) + chunkY);

	const u32 vX = vertexID % 17;
	const u32 vY = vertexID / 17;

	bool isOddRow = vX > 8;

	vec2 vertexOffset;
	vertexOffset.x = -(8.5f * isOddRow);
	vertexOffset.y = (0.5f * isOddRow);

	uvec2 globalVertex = uvec2(vX + cellX * 8, vY + cellY * 8);

	vec2 finalPos = -Terrain::MAP_HALF_SIZE + (vec2(globalVertex) + vertexOffset) * Terrain::PATCH_SIZE;

	return vec2(-finalPos.y, -finalPos.x);
}

void TerrainLoader::LoadFullMapRequest(const LoadRequestInternal& request)
{
	assert(request.loadType == LoadType::Full);
	assert(request.mapName.size() > 0);

	const std::string& mapName = request.mapName;
	if (mapName == _currentMapInternalName)
	{
		return;
	}

	fs::path absoluteMapPath = fs::absolute("Data/Map/" + mapName);
	if (!fs::is_directory(absoluteMapPath))
	{
		DebugHandler::PrintError("TerrainLoader : Failed to find '{0}' folder", absoluteMapPath.string());
		return;
	}

	std::vector<fs::path> paths;
	fs::directory_iterator dirpos{ absoluteMapPath };
	std::copy(begin(dirpos), end(dirpos), std::back_inserter(paths));

	u32 numPaths = static_cast<u32>(paths.size());
	std::atomic<u32> numExistingChunks = 0;

	entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;

	bool physicsEnabled = CVAR_TerrainLoaderPhysicsEnabled.Get();
	auto& joltState = registry->ctx().at<ECS::Singletons::JoltState>();
	JPH::BodyInterface& bodyInterface = joltState.physicsSystem.GetBodyInterface();

	enki::TaskSet countValidChunksTask(numPaths, [&](enki::TaskSetPartition range, uint32_t threadNum)
	{
		for (u32 i = range.start; i < range.end; i++)
		{
			const fs::path& path = paths[i];

			if (path.extension() != ".chunk")
				continue;

			numExistingChunks++;
		}
	});

	enki::TaskSet loadChunksTask(numPaths, [&, physicsEnabled](enki::TaskSetPartition range, uint32_t threadNum)
	{
		for (u32 i = range.start; i < range.end; i++)
		{
			const fs::path& path = paths[i];

			if (path.extension() != ".chunk")
				continue;

			std::string pathStr = path.string();

			std::vector<std::string> splitStrings = StringUtils::SplitString(pathStr, '_');
			u32 numSplitStrings = static_cast<u32>(splitStrings.size());

			u32 chunkX = std::stoi(splitStrings[numSplitStrings - 2]);
			u32 chunkY = std::stoi(splitStrings[numSplitStrings - 1].substr(0, 2));
			u32 chunkID = chunkX + (chunkY * Terrain::CHUNK_NUM_PER_MAP_STRIDE);

			std::string chunkPathStr = path.string();

			FileReader reader(chunkPathStr);
			if (reader.Open())
			{
				size_t bufferSize = reader.Length();

				std::shared_ptr<Bytebuffer> chunkBuffer = Bytebuffer::BorrowRuntime(bufferSize);
				reader.Read(chunkBuffer.get(), bufferSize);

				u32 chunkHash = StringUtils::fnv1a_32(chunkPathStr.c_str(), chunkPathStr.size());
				Map::Chunk* chunk = reinterpret_cast<Map::Chunk*>(chunkBuffer->GetDataPointer());

				// Load into Jolt
				if (physicsEnabled)
				{
					constexpr u32 numVerticesPerChunk = Terrain::CHUNK_NUM_CELLS * Terrain::CELL_TOTAL_GRID_SIZE;
					constexpr u32 numTrianglePerChunk = Terrain::CHUNK_NUM_CELLS * Terrain::CELL_NUM_TRIANGLES;

					JPH::VertexList vertexList;
					JPH::IndexedTriangleList triangleList;
					vertexList.reserve(numVerticesPerChunk);
					triangleList.reserve(numTrianglePerChunk);

					u32 patchVertexIDs[5] = { 0 };
					vec2 patchVertexOffsets[5] = {
						vec2(0, 0),
						vec2(Terrain::PATCH_SIZE, 0),
						vec2(Terrain::PATCH_HALF_SIZE, Terrain::PATCH_HALF_SIZE),
						vec2(0, Terrain::PATCH_SIZE),
						vec2(Terrain::PATCH_SIZE, Terrain::PATCH_SIZE)
					};

					uvec2 triangleComponentOffsets = uvec2(0, 0);

					for (u32 cellID = 0; cellID < Terrain::CHUNK_NUM_CELLS; cellID++)
					{
						const Map::Cell& cell = chunk->cells[cellID];

						for (u32 i = 0; i < Terrain::CELL_TOTAL_GRID_SIZE; i++)
						{
							const Map::Cell::VertexData& vertexA = cell.vertexData[i];

							vec2 pos = GetGlobalVertexPosition(chunkID, cellID, i);
							vertexList.push_back({ pos.x, f32(vertexA.height), pos.y });
						}

						for (u32 i = 0; i < Terrain::CELL_NUM_TRIANGLES; i++)
						{
							u32 triangleID = i;
							u32 patchID = triangleID / 4;
							u32 patchRow = patchID / 8;
							u32 patchColumn = patchID % 8;

							u32 patchX = patchID % Terrain::CELL_NUM_PATCHES_PER_STRIDE;
							u32 patchY = patchID / Terrain::CELL_NUM_PATCHES_PER_STRIDE;

							vec2 patchPos = vec2(patchX * Terrain::PATCH_SIZE, patchY * Terrain::PATCH_SIZE);

							// Top Left is calculated like this
							patchVertexIDs[0] = patchColumn + (patchRow * Terrain::CELL_GRID_ROW_SIZE);

							// Top Right is always +1 from Top Left
							patchVertexIDs[1] = patchVertexIDs[0] + 1;

							// Bottom Left is always NUM_VERTICES_PER_PATCH_ROW from the Top Left vertex
							patchVertexIDs[2] = patchVertexIDs[0] + Terrain::CELL_GRID_ROW_SIZE;

							// Bottom Right is always +1 from Bottom Left
							patchVertexIDs[3] = patchVertexIDs[2] + 1;

							// Center is always NUM_VERTICES_PER_OUTER_PATCH_ROW from Top Left
							patchVertexIDs[4] = patchVertexIDs[0] + Terrain::CELL_OUTER_GRID_STRIDE;

							u32 triangleWithinPatch = triangleID % 4; // 0 - top, 1 - left, 2 - bottom, 3 - right
							triangleComponentOffsets = uvec2(triangleWithinPatch > 1, // Identify if we are within bottom or right triangle
								triangleWithinPatch == 0 || triangleWithinPatch == 3); // Identify if we are within the top or right triangle

							u32 vertexID1 = (cellID * Terrain::CELL_TOTAL_GRID_SIZE) + patchVertexIDs[4];
							u32 vertexID2 = (cellID * Terrain::CELL_TOTAL_GRID_SIZE) + patchVertexIDs[triangleComponentOffsets.x * 2 + triangleComponentOffsets.y];
							u32 vertexID3 = (cellID * Terrain::CELL_TOTAL_GRID_SIZE) + patchVertexIDs[(!triangleComponentOffsets.y) * 2 + triangleComponentOffsets.x];
							triangleList.push_back({ vertexID3, vertexID2, vertexID1 });
						}
					}

					JPH::MeshShapeSettings shapeSetting(vertexList, triangleList);
					JPH::ShapeSettings::ShapeResult shapeResult = shapeSetting.Create();
					JPH::ShapeRefC shape = shapeResult.Get(); // We don't expect an error here, but you can check floor_shape_result for HasError() / GetError()

					// Create the settings for the body itself. Note that here you can also set other properties like the restitution / friction.
					JPH::BodyCreationSettings bodySettings(shape, JPH::RVec3(0.0f, 0.0f, 0.0f), JPH::Quat::sIdentity(), JPH::EMotionType::Static, Jolt::Layers::NON_MOVING);

					// Create the actual rigid body
					JPH::Body* body = bodyInterface.CreateBody(bodySettings); // Note that if we run out of bodies this can return nullptr
					body->SetFriction(0.8f);

					JPH::BodyID bodyID = body->GetID();
					bodyInterface.AddBody(bodyID, JPH::EActivation::DontActivate);
					_chunkIDToBodyID[chunkID] = bodyID.GetIndexAndSequenceNumber();
				}

				// Load into Terrain Renderer
				{
					u32 chunkDataID = _terrainRenderer->AddChunk(chunkHash, chunk, ivec2(chunkX, chunkY));
					_chunkIDToLoadedID[chunkID] = chunkDataID;

					u32 numMapObjectPlacements = chunk->numMapObjectPlacements;
					u32 numComplexModelPlacements = chunk->numComplexModelPlacements;

					size_t mapObjectOffset = sizeof(Map::Chunk) - ((sizeof(std::vector<Terrain::Placement>) * 2) + sizeof(Map::LiquidInfo));
					
					if (numMapObjectPlacements)
					{
						for (u32 j = 0; j < numMapObjectPlacements; j++)
						{
							size_t offset = mapObjectOffset + (j * sizeof(Terrain::Placement));
							Terrain::Placement* placement = reinterpret_cast<Terrain::Placement*>(&chunkBuffer->GetDataPointer()[offset]);
					
							_modelLoader->LoadPlacement(*placement);
						}
					}
					
					if (numComplexModelPlacements)
					{
						size_t cModelOffset = mapObjectOffset + (numMapObjectPlacements * sizeof(Terrain::Placement));

						for (u32 j = 0; j < numComplexModelPlacements; j++)
						{
							size_t offset = cModelOffset + (j * sizeof(Terrain::Placement));
							Terrain::Placement* placement = reinterpret_cast<Terrain::Placement*>(&chunkBuffer->GetDataPointer()[offset]);
					
							_modelLoader->LoadPlacement(*placement);
						}
					}

					Map::LiquidInfo liquidInfo;
					size_t liquidOffset = mapObjectOffset + (numMapObjectPlacements * sizeof(Terrain::Placement)) + (numComplexModelPlacements * sizeof(Terrain::Placement));
					chunkBuffer->SkipRead(liquidOffset);

					// Read Water
					{
						u32 numLiquidHeaders = 0;
						if (!chunkBuffer->GetU32(numLiquidHeaders))
							return false;

						if (numLiquidHeaders != 0 && numLiquidHeaders != 256)
						{
							DebugHandler::PrintFatal("LiquidInfo should always contain either 0 or 256 liquid headers, but it contained {0} liquid headers", numLiquidHeaders);
						}

						if (numLiquidHeaders > 0)
						{
							liquidInfo.headers.resize(numLiquidHeaders);
							if (!chunkBuffer->GetBytes(reinterpret_cast<u8*>(&liquidInfo.headers[0]), numLiquidHeaders * sizeof(Map::CellLiquidHeader)))
								return false;
						}

						u32 numLiquidInstances = 0;
						if (!chunkBuffer->GetU32(numLiquidInstances))
							return false;

						if (numLiquidInstances > 0)
						{
							liquidInfo.instances.resize(numLiquidInstances);
							if (!chunkBuffer->GetBytes(reinterpret_cast<u8*>(&liquidInfo.instances[0]), numLiquidInstances * sizeof(Map::CellLiquidInstance)))
								return false;
						}

						u32 numLiquidAttributes = 0;
						if (!chunkBuffer->GetU32(numLiquidAttributes))
							return false;

						if (numLiquidAttributes > 0)
						{
							liquidInfo.attributes.resize(numLiquidAttributes);
							if (!chunkBuffer->GetBytes(reinterpret_cast<u8*>(&liquidInfo.attributes[0]), numLiquidAttributes * sizeof(Map::CellLiquidAttributes)))
								return false;
						}

						u32 numLiquidBitmapDataBytes = 0;
						if (!chunkBuffer->GetU32(numLiquidBitmapDataBytes))
							return false;

						if (numLiquidBitmapDataBytes > 0)
						{
							liquidInfo.bitmapData.resize(numLiquidBitmapDataBytes);
							if (!chunkBuffer->GetBytes(reinterpret_cast<u8*>(&liquidInfo.bitmapData[0]), numLiquidBitmapDataBytes * sizeof(u8)))
								return false;
						}

						u32 numLiquidVertexDataBytes = 0;
						if (!chunkBuffer->GetU32(numLiquidVertexDataBytes))
							return false;

						if (numLiquidVertexDataBytes > 0)
						{
							liquidInfo.vertexData.resize(numLiquidVertexDataBytes);
							if (!chunkBuffer->GetBytes(reinterpret_cast<u8*>(&liquidInfo.vertexData[0]), numLiquidVertexDataBytes * sizeof(u8)))
								return false;
						}
					}

					_waterLoader->LoadFromChunk(chunkX, chunkY, &liquidInfo);
				}
			}
		}

		return true;
	});

	DebugHandler::Print("TerrainLoader : Started Preparing Chunk Loading");
	_scheduler.AddTaskSetToPipe(&countValidChunksTask);
	_scheduler.WaitforTask(&countValidChunksTask);
	DebugHandler::Print("TerrainLoader : Finished Preparing Chunk Loading");

	u32 numChunksToLoad = numExistingChunks.load();
	if (numChunksToLoad == 0)
	{
		DebugHandler::PrintError("TerrainLoader : Failed to prepare chunks for map '{0}'", request.mapName);
		return;
	}

	_currentMapInternalName = mapName;
	PrepareForChunks(LoadType::Full, numChunksToLoad);

	DebugHandler::Print("TerrainLoader : Started Chunk Loading");
	_scheduler.AddTaskSetToPipe(&loadChunksTask);
	_scheduler.WaitforTask(&loadChunksTask);

	if (physicsEnabled && CVAR_TerrainLoaderPhysicsOptimizeBP.Get())
	{
		joltState.physicsSystem.OptimizeBroadPhase();
	}

	DebugHandler::Print("TerrainLoader : Finished Chunk Loading");
}

void TerrainLoader::PrepareForChunks(LoadType loadType, u32 numChunks)
{
	entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
	auto& joltState = registry->ctx().at<ECS::Singletons::JoltState>();
	JPH::BodyInterface& bodyInterface = joltState.physicsSystem.GetBodyInterface();

	if (loadType == LoadType::Partial)
	{

	}
	else if (loadType == LoadType::Full)
	{
		_terrainRenderer->ClearChunks();

		for (auto& pair : _chunkIDToBodyID)
		{
			JPH::BodyID id = static_cast<JPH::BodyID>(pair.second);

			bodyInterface.RemoveBody(id);
			bodyInterface.DestroyBody(id);
		}

		_chunkIDToLoadedID.clear();
		_chunkIDToBodyID.clear();

		_terrainRenderer->ReserveChunks(numChunks);
		_chunkIDToLoadedID.reserve(numChunks);
		_chunkIDToBodyID.reserve(numChunks);
	}
}