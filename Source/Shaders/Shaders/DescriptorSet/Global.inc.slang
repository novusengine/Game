#ifndef GLOBAL_SET_INCLUDED
#define GLOBAL_SET_INCLUDED

#include "Include/Camera.inc.slang"

// WARNING: For now, when you change this file, make sure you recompile ALL shaders or you might get descriptor set reflection issues
// This will be fixed later when we have better dependency tracking

[[vk::binding(0, GLOBAL)]] StructuredBuffer<Camera> _cameras;

#endif // GLOBAL_SET_INCLUDED