material CompatibilityOpaque_Mod2xNA_Alpha_Add
{
    programFamily = CompatibilityCommon;
    pixelShaderIDs = [15, 19];
}

#include "Material/Authored/Compatibility/CompatibilityMaterial.inc.slang"

SurfaceDescription EvaluateMaterial(MaterialEvaluationContext context)
{
    CompatibilityMaterialParameters p = LoadCompatibilityMaterialParameters(context.instance.parameterOffset);
    float4 a = SampleMaterialTexture(context, 0u);
    float4 b = SampleMaterialTexture(context, 1u);
    float4 c = SampleMaterialTexture(context, 2u);

    float4 color = float4(lerp(a.rgb * b.rgb, a.rgb, a.aaa), 1.0f) * p.baseColor;

    SurfaceDescription surface = MakeDefaultSurface(context.input);
    surface.albedo = color.rgb;
    surface.alpha = color.a;
    surface.emissive = c.rgb * c.a;
    surface.emissiveIntensity = any(surface.emissive != 0.0f) ? 1.0f : 0.0f;
    surface.flags = context.instance.flags;
    return surface;
}

float EvaluateCoverage(MaterialEvaluationContext context)
{
    CompatibilityMaterialParameters parameters = LoadCompatibilityMaterialParameters(context.instance.parameterOffset);
    float4 texture0 = SampleMaterialTexture(context, 0u);

    float coverage = texture0.a * parameters.baseColor.a;

    return coverage;
}
