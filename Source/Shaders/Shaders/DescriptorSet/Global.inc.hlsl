#ifndef GLOBAL_SET_INCLUDED
#define GLOBAL_SET_INCLUDED

#include "Include/Camera.inc.hlsl"

[[vk::binding(0, GLOBAL)]] StructuredBuffer<Camera> _cameras;

#endif // GLOBAL_SET_INCLUDED