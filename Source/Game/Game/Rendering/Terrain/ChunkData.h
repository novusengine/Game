#pragma once
#include <Base/Types.h>

struct ChunkData
{
public:
	u32 meshletOffset = 0;
	u32 meshletCount = 0;
	u32 indexOffset = 0;
	u32 vertexOffset = 0;
};