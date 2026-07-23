permutation SVSM_DYNAMIC = [0, 1]; // 1 = dynamic caster split pass, renders into the dynamic pool

#define GEOMETRY_PASS 1

#include "DescriptorSet/Global.inc.slang"
#include "DescriptorSet/Model.inc.slang"

#include "Include/Common.inc.slang"
#include "Model/ModelAlphaKey.inc.slang"
#include "Model/ModelShared.inc.slang"
#include "Shadows/SVSM.inc.slang"

// SVSM page render for models: keeps the alpha-key discard from Model/Draw.ps, then writes
// reversed-Z depth into the physical page pool with image atomics. No attachments, viewport
// spans the full virtual texture so SV_Position.xy is the window-local virtual texel. Paired
// with the SVSM_PASS=1 vertex permutation.
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
#if SVSM_DYNAMIC
    const uint requiredFlags = SVSM_PAGE_RESIDENT;
    const uint poolPagesPerRow = _svsmData[0].configDynamicPoolPagesPerRow;
#else
    const uint requiredFlags = SVSM_PAGE_RESIDENT | SVSM_PAGE_DIRTY;
    const uint poolPagesPerRow = _svsmData[0].configPoolPagesPerRow;
#endif

    // Fragments over pages that don't render this frame skip the alpha-key sampling entirely.
    // The skip diverges only along page borders, where a wrong implicit mip for one quad row in a
    // depth-only alpha test is invisible — Sample stays Sample
    uint pageEntry = SVSMLoadPageEntry(input.position.xy, _constants.viewIndex - 1, _svsmData, _pageTable);
    if ((pageEntry & requiredFlags) != requiredFlags)
    {
        return;
    }

    uint textureDataID = input.drawIDInstanceIDTextureDataIDInstanceRefID.z;

    // Same alpha-key discard as Model/Draw.ps, without the visibility-buffer write
    AlphaKeyDiscard(textureDataID, input.uv01.xy);

    SVSMWritePageDepthEntry(input.position.xyz, pageEntry, requiredFlags, poolPagesPerRow, _svsmData, _pagePool);
}
