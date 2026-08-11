#ifndef MATERIAL_EVALUATION_INCLUDED
#define MATERIAL_EVALUATION_INCLUDED

#include "Material/MaterialAuthoring.inc.slang"
#include "DescriptorSet/Material.inc.slang"
#define MATERIAL_BASE_COLOR_OFFSET 0u
#define FALLBACK_MATERIAL_PROGRAM_ID 0xFFFFFFFFu

struct MaterialParameters
{
    float4 baseColor;
    float alphaCutoff;
};

MaterialParameters LoadMaterialParameters(uint parameterOffset)
{
    MaterialParameters parameters;
    parameters.baseColor = asfloat(_materialParameters.Load4(parameterOffset + MATERIAL_BASE_COLOR_OFFSET));
    parameters.alphaCutoff = asfloat(_materialParameters.Load(parameterOffset + MATERIAL_ALPHA_CUTOFF_OFFSET));
    return parameters;
}

SurfaceDescription EvaluateFallbackMaterial(MaterialInstanceRecord instance,
                                            MaterialParameters parameters, MaterialInput input)
{
    MaterialEvaluationContext context;
    context.input = input;
    context.instance = instance;
    context.textureOffset = 0u;
    context.parameterBlockIndex = 0u;

    float4 color = parameters.baseColor * SampleMaterialTexture(context, 0u);
    SurfaceDescription surface = MakeDefaultSurface(input);
    surface.albedo = color.rgb;
    surface.alpha = color.a;
    surface.flags = instance.flags;
    return surface;
}

#endif
