#ifndef DEBUG_SET_INCLUDED
#define DEBUG_SET_INCLUDED

struct DebugVertex2D
{
    float2 pos;
    uint color;
};

struct DebugVertex3D
{
    float3 pos;
    uint color;
};

// WARNING: For now, when you change this file, make sure you recompile ALL shaders or you might get descriptor set reflection issues
// This will be fixed later when we have better dependency tracking

[[vk::binding(0, DEBUG)]] RWStructuredBuffer<DebugVertex2D> _debugVertices2D;
[[vk::binding(1, DEBUG)]] RWByteAddressBuffer _debugVertices2DCount;
[[vk::binding(2, DEBUG)]] RWStructuredBuffer<DebugVertex3D> _debugVertices3D;
[[vk::binding(3, DEBUG)]] RWByteAddressBuffer _debugVertices3DCount;

#endif // DEBUG_SET_INCLUDED