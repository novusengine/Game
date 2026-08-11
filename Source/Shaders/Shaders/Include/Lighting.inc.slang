#ifndef LIGHTING_INCLUDED
#define LIGHTING_INCLUDED

#include "Include/VisibilityBuffers.inc.slang"
#include "Material/LightingInput.inc.slang"
#include "Material/LightingModels.inc.slang"

float3 ApplyLighting(float3 materialColor, PixelVertexData pixelVertexData, uint4 lightInfo, ShadowSettings shadowSettings)
{
    MaterialInput materialInput;
    materialInput.worldPosition = pixelVertexData.worldPos;
    materialInput.geometricNormal = pixelVertexData.worldNormal;
    materialInput.vertexColor = 1.0f;
    materialInput.uv0 = pixelVertexData.uv0.value;
    materialInput.uv0DDX = pixelVertexData.uv0.ddx;
    materialInput.uv0DDY = pixelVertexData.uv0.ddy;
    materialInput.frontFacing = true;
    SurfaceDescription surface = MakeDefaultSurface(materialInput);
    surface.albedo = materialColor;
    LightingModelInput lightingInput = BuildLightingModelInput(pixelVertexData.pixelPos, 0u, pixelVertexData.worldPos, pixelVertexData.worldNormal, pixelVertexData.faceNormal, lightInfo.x, true, shadowSettings);
    return EvaluateLightingModel(LIGHTING_MODEL_STANDARD, surface, lightingInput);
}

#endif
