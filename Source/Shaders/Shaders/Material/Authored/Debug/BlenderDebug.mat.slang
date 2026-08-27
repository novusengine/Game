material BlenderDebug
{
    programFamily = CompatibilityCommon;
    materialPath = "material/generated/nbs/blender.material";
    rasterClass = Opaque;

    instance Blender
    {
        path = "material/generated/nbs/blender.materialinstance";
        lightingModel = Unlit;
        flags = [TwoSided, CastsShadows];
    }
}

SurfaceDescription EvaluateMaterial(MaterialEvaluationContext context)
{
    SurfaceDescription surface = MakeDefaultSurface(context.input);
    surface.albedo = normalize(context.input.geometricNormal) * 0.5f + 0.5f;
    surface.emissive = 0.0f;
    surface.emissiveIntensity = 0.0f;
    surface.flags = context.instance.flags;
    return surface;
}
