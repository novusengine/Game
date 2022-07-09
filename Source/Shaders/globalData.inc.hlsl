#ifndef GLOBALDATA_INCLUDED
#define GLOBALDATA_INCLUDED

#include "Include/ViewData.inc.hlsl"

[[vk::binding(0, GLOBAL)]] ConstantBuffer<ViewData> _viewData;

#endif // GLOBALDATA_INCLUDED