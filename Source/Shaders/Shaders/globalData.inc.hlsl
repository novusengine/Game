#ifndef GLOBALDATA_INCLUDED
#define GLOBALDATA_INCLUDED

#include "Include/Camera.inc.hlsl"

[[vk::binding(0, GLOBAL)]] StructuredBuffer<Camera> _cameras;

#endif // GLOBALDATA_INCLUDED