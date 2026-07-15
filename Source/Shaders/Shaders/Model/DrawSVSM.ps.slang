permutation SVSM_DYNAMIC = [0, 1]; // 1 = dynamic caster split pass, renders into the dynamic pool

#define GEOMETRY_PASS 1

#include "DescriptorSet/Global.inc.slang"
#include "DescriptorSet/Model.inc.slang"

#include "Include/Common.inc.slang"
#include "Model/ModelShared.inc.slang"
#include "Shadows/SVSM.inc.slang"

// SVSM page render for models: keeps the alpha-key discard from the shadow permutation of
// Model/Draw.ps, then writes reversed-Z depth into the physical page pool with image atomics.
// No attachments, viewport spans the full virtual texture so SV_Position.xy is the window-local
// virtual texel. Paired with the SHADOW_PASS=1 vertex permutation.
//
// SVSM_DYNAMIC renders the dynamic-instance fill into the dynamic pool: same bindings, the pass
// binds the dynamic table/pool instead, and dynamic pages render whenever RESIDENT (every frame)

struct Constants
{
    uint viewIndex;     // 0 = main camera, clipmap k renders as view k + 1
    uint svsmRectIndex; // Consumed by the vertex shader's clip rect, declared here so the push block matches
};

[[vk::push_constant]] Constants _constants;

[[vk::binding(0, PER_PASS)]] StructuredBuffer<SVSMData> _svsmData;
[[vk::binding(1, PER_PASS)]] StructuredBuffer<uint> _pageTable;
[[vk::binding(2, PER_PASS)]] RWTexture2D<uint> _pagePool;

struct PSInput
{
    float4 position : SV_Position;
    uint4 drawIDInstanceIDTextureDataIDInstanceRefID : TEXCOORD0;
    float4 uv01 : TEXCOORD1;
};

[shader("fragment")]
void main(PSInput input)
{
    uint textureDataID = input.drawIDInstanceIDTextureDataIDInstanceRefID.z;

    // Alpha-key discard, mirrors the SHADOW_PASS branch of Model/Draw.ps
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

        float4 texture1 = _modelTextures[NonUniformResourceIndex(textureUnit.textureIDs[0])].Sample(_samplers[texture0SamplerIndex], input.uv01.xy);

        float minAlpha = texture1.a;
        if (minAlpha < (128.0 / 255.0))
        {
            discard;
        }
    }

#if SVSM_DYNAMIC
    SVSMWritePageDepth(input.position.xyz, _constants.viewIndex - 1, SVSM_PAGE_RESIDENT, _svsmData[0].configDynamicPoolPagesPerRow, _svsmData, _pageTable, _pagePool);
#else
    SVSMWritePageDepth(input.position.xyz, _constants.viewIndex - 1, SVSM_PAGE_RESIDENT | SVSM_PAGE_DIRTY, _svsmData[0].configPoolPagesPerRow, _svsmData, _pageTable, _pagePool);
#endif
}
