#pragma once
#include "Game-Lib/Editor/TerrainTools.h"

#include <Base/Types.h>
#include <Input/KeybindGroup.h>

class TerrainRenderer;
class DebugRenderer;

class TerrainManipulator
{
	struct VertexData
	{
		u32 vertexID;
		u16 chunkID;
		u16 localCellID;
		u32 cellHeightRangeID;
		f32 hardness;
	};
public:
	TerrainManipulator(TerrainRenderer& terrainRenderer, DebugRenderer& debugRenderer);
	~TerrainManipulator();

	void Update(f32 deltaTime);

private:
	void GetVertexDatasAroundWorldPos(const vec3& worldPos, f32 radius, f32 hardness, Editor::TerrainTools::HardnessMode hardnessMode, std::vector<VertexData>& outVertexData);

private:
	TerrainRenderer& _terrainRenderer;
	DebugRenderer& _debugRenderer;

	bool _isManipulating = false;
	bool _isLower = false;

	vec3 _debugLastClickPos;
	std::vector<vec3> _debugPoints;
};