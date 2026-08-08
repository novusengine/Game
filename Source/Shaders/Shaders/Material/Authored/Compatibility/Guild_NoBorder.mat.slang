material CompatibilityGuild_NoBorder
{
    programFamily = CompatibilityCommon;
    pixelShaderIDs = [31];
}

#include "Material/Authored/Compatibility/CompatibilityMaterial.inc.slang"

SurfaceDescription EvaluateMaterial(MaterialEvaluationContext context)
{
    CompatibilityMaterialParameters p = LoadCompatibilityMaterialParameters(context.instance.parameterOffset);
    float4 a = SampleMaterialTexture(context, 0u);
    float4 b = SampleMaterialTexture(context, 1u);

    float4 color = float4(a.rgb * lerp(1.0f, b.rgb, b.aaa), a.a) * p.baseColor;

    SurfaceDescription surface = MakeDefaultSurface(context.input);
    surface.albedo = color.rgb;
    surface.alpha = color.a;
    surface.emissive = 0.0f;
    surface.emissiveIntensity = 0.0f;
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
