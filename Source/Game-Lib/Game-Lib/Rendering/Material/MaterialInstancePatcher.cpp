#include "MaterialInstancePatcher.h"

namespace MaterialLoading
{
    bool MaterialInstancePatcher::Patch(const MaterialInstanceAssetView& instance, const ResolveTextureCallback& resolveTexture, u32 textureSlotCount, u32 fallbackTextureIndex, std::vector<u8>& outParameterData,
                                        std::vector<u32>& outTextureIndices, std::vector<u32>& outSamplerIDs)
    {
        outParameterData.assign(instance.parameterData.begin(), instance.parameterData.end());
        outTextureIndices.assign(textureSlotCount, fallbackTextureIndex);
        outSamplerIDs.assign(textureSlotCount, 0);
        for (const FileFormat::Material::TextureBinding& binding : instance.textureBindings)
        {
            if (binding.textureSlot >= textureSlotCount)
                return false;

            switch (binding.type)
            {
            case FileFormat::Material::ResourceType::Texture2D:
            case FileFormat::Material::ResourceType::TextureCube:
            {
                const bool optional = (binding.flags & FileFormat::Material::ResourceBindingFlags_Optional) != 0;
                if (binding.resourceAssetID != FileFormat::INVALID_ASSET_ID || !optional)
                    outTextureIndices[binding.textureSlot] = resolveTexture(binding.resourceAssetID, optional);
                outSamplerIDs[binding.textureSlot] = binding.samplerID;
                break;
            }
            case FileFormat::Material::ResourceType::Sampler:
                outSamplerIDs[binding.textureSlot] = binding.samplerID;
                break;
            default:
                return false;
            }
        }
        return true;
    }
} // namespace MaterialLoading
