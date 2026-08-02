#include "MaterialInstancePatcher.h"

#include <cstring>

namespace MaterialLoading
{
    bool MaterialInstancePatcher::Patch(const MaterialInstanceAssetView& instance, const ResolveTextureCallback& resolveTexture,
                                        std::vector<u8>& outParameterData)
    {
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
                break;
            case FileFormat::Material::ResourceType::Sampler:
                value = binding.samplerID;
                break;
            default:
                return false;
            }

            std::memcpy(outParameterData.data() + binding.parameterByteOffset, &value, sizeof(value));
        }
        return true;
    }
} // namespace MaterialLoading
