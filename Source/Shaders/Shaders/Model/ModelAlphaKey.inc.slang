#ifndef MODEL_ALPHA_KEY_INCLUDED
#define MODEL_ALPHA_KEY_INCLUDED

#include "DescriptorSet/Model.inc.slang"
#include "Model/ModelShared.inc.slang"

// Alpha-key discard shared by the model fragment passes that need cutouts without shading
// (Model/Draw.ps, Model/DrawSVSM.ps). Fragment-only (Sample + discard), keep it out of the
// includes compute shaders pull in.
// Experimental alphakey discard without shading, if this has issues check github history for
// cModel.ps.hlsl
void AlphaKeyDiscard(uint textureDataID, float2 uv0)
{
    TextureData textureData = LoadModelTextureData(_packedModelTextureDatas, textureDataID);

    for (uint textureUnitIndex = textureData.textureUnitOffset; textureUnitIndex < textureData.textureUnitOffset + textureData.numTextureUnits; textureUnitIndex++)
    {
        ModelTextureUnit textureUnit = _modelTextureUnits[textureUnitIndex];

        uint blendingMode = (textureUnit.data1 >> 11) & 0x7;

        if (blendingMode != 1) // ALPHA KEY
            continue;

        uint texture0SamplerIndex = (textureUnit.data1 >> 1) & 0x3;
        uint materialType = (textureUnit.data1 >> 16) & 0xFFFF;

        if (materialType == 0x8000)
            continue;

        float4 texture1 = _modelTextures[NonUniformResourceIndex(textureUnit.textureIDs[0])].Sample(_samplers[texture0SamplerIndex], uv0);

        // TODO : minAlpha is set based on the Pixel Shader Id, and per-instance diffuseColor
        // should participate
        float minAlpha = texture1.a;
        if (minAlpha < (128.0 / 255.0))
        {
            discard;
        }
    }
}

#endif // MODEL_ALPHA_KEY_INCLUDED
