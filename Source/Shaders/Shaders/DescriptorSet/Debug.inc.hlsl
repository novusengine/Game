#ifndef DEBUG_SET_INCLUDED
#define DEBUG_SET_INCLUDED

[[vk::binding(0, DEBUG)]] RWStructuredBuffer<DebugVertex2D> _debugVertices2D;
[[vk::binding(1, DEBUG)]] RWByteAddressBuffer _debugVertices2DCount;
[[vk::binding(2, DEBUG)]] RWStructuredBuffer<DebugVertex3D> _debugVertices3D;
[[vk::binding(3, DEBUG)]] RWByteAddressBuffer _debugVertices3DCount;

#endif // DEBUG_SET_INCLUDED