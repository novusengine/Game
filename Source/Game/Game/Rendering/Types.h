#pragma once
#include <Base/Types.h>

struct DrawCall
{
public:
    u32 indexCount;
    u32 instanceCount;
    u32 firstIndex;
    u32 vertexOffset;
    u32 firstInstance;
};

struct Dispatch
{
public:
    u32 x = 0;
    u32 y = 0;
    u32 z = 0;
};

// In some of our culling shaders we use 64 bit atomic operations to increment two things in sync
// For our usecase, the second 32 bit value should become our dispatchX value
struct PaddedDispatch
{
public:
    u32 paddedData = 0;
    Dispatch dispatch = {};
};

struct Meshlet
{
public:
    u32 indexStart = 0;
    u32 indexCount = 0;
};