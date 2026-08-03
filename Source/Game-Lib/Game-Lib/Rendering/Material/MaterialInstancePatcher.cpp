#include "MaterialInstancePatcher.h"

#include <cstring>

namespace MaterialLoading
{
    bool MaterialInstancePatcher::Patch(const MaterialInstanceAssetView& instance, const ResolveTextureCallback& resolveTexture,
                                        std::vector<u8>& outParameterData, u32* outPackedSamplerIDs)
    {
        constexpr u32 TEXTURE_PARAMETER_OFFSET = 16;
        constexpr u32 MAX_TEXTURES = 8;
        constexpr u32 SAMPLER_BITS = 2;
        u32 packedSamplerIDs = 0;
        outParameterData.assign(instance.parameterData.begin(), instance.parameterData.end());
        for (const FileFormat::Material::ResourceBinding& binding : instance.resourceBindings)
        {
            if (binding.parameterByteOffset > outParameterData.size() || sizeof(u32) > outParameterData.size() - binding.parameterByteOffset)
                return false;

            u32 value = 0;
            switch (binding.type)
            {
            case FileFormat::Material::ResourceType::Texture2D:
            case FileFormat::Material::ResourceType::TextureCube:
                value = resolveTexture(binding.resourceAssetID,
                                       (binding.flags & FileFormat::Material::ResourceBindingFlags_Optional) != 0);
                if (binding.parameterByteOffset >= TEXTURE_PARAMETER_OFFSET)
                {
                    const u32 textureSlot = (binding.parameterByteOffset - TEXTURE_PARAMETER_OFFSET) / sizeof(u32);
                    if (textureSlot < MAX_TEXTURES)
                        packedSamplerIDs |= (binding.samplerID & 0x3u) << (textureSlot * SAMPLER_BITS);
                }
                break;
            case FileFormat::Material::ResourceType::Sampler:
                value = binding.samplerID;
                break;
            default:
                return false;
            }

            std::memcpy(outParameterData.data() + binding.parameterByteOffset, &value, sizeof(value));
        }
        if (outPackedSamplerIDs)
            *outPackedSamplerIDs = packedSamplerIDs;
        return true;
    }
} // namespace MaterialLoading
