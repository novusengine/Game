#ifndef MATERIAL_SHARED_INCLUDED
#define MATERIAL_SHARED_INCLUDED

#include "Material/MaterialABI.inc.slang"

#define MATERIAL_TWO_SIDED (1u << 0u)
#define MATERIAL_CASTS_SHADOWS (1u << 1u)

struct MaterialInstanceRecord
{
    uint parameterOffset;
    uint materialIndex;
    uint textureOffset;
    uint textureCount;
    uint flags;
    uint packedClassification;
    uint materialHandle;
    uint samplerOffset;
};

struct MaterialRecord
{
    uint defaultParameterOffset;
    uint parameterBlockSize;
    uint programID;
    uint flags;
    uint packedProgramRouting01;
    uint packedProgramRouting2AndBaseGroup;
};

uint MaterialLightingModel(MaterialInstanceRecord instance) { return instance.packedClassification & 0xFFFFu; }
uint MaterialExecutionGroup(MaterialInstanceRecord instance) { return instance.packedClassification >> 16u; }
uint MaterialHandle(MaterialInstanceRecord instance) { return instance.materialHandle; }
uint MaterialRasterClass(MaterialInstanceRecord instance)
{
    return (MaterialExecutionGroup(instance) % MATERIAL_EXECUTION_GROUP_CLASS_COUNT) / 2u;
}
uint MaterialGroupLocalProgramID(MaterialRecord material, uint rasterClass)
{
    if (rasterClass == 0u) return material.packedProgramRouting01 & 0xFFFFu;
    if (rasterClass == 1u) return material.packedProgramRouting01 >> 16u;
    return material.packedProgramRouting2AndBaseGroup & 0xFFFFu;
}

#endif
