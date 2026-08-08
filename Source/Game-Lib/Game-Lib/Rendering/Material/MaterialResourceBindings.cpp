#include "MaterialResourceBindings.h"

#include "MaterialStorage.h"
#include "MaterialTextureRegistry.h"

#include <Renderer/Renderer.h>

namespace MaterialLoading
{
    MaterialResourceBindings::MaterialResourceBindings(Renderer::Renderer* renderer, Renderer::DescriptorSet* descriptorSet)
        : _descriptorSet(descriptorSet)
    {
        for (u32 samplerIndex = 0; samplerIndex < _samplers.size(); ++samplerIndex)
        {
            Renderer::SamplerDesc sampler;
            sampler.enabled = true;
            sampler.filter = Renderer::SamplerFilter::ANISOTROPIC;
            sampler.maxAnisotropy = 16;
            sampler.addressU = (samplerIndex & 1u) != 0u ? Renderer::TextureAddressMode::WRAP
                                                         : Renderer::TextureAddressMode::CLAMP;
            sampler.addressV = (samplerIndex & 2u) != 0u ? Renderer::TextureAddressMode::WRAP
                                                         : Renderer::TextureAddressMode::CLAMP;
            sampler.addressW = Renderer::TextureAddressMode::CLAMP;
            sampler.shaderVisibility = Renderer::ShaderVisibility::ALL;
            _samplers[samplerIndex] = renderer->CreateSampler(sampler);
        }

        _descriptorSet->Bind("_materialSampler0"_h, _samplers[0]);
        _descriptorSet->Bind("_materialSampler1"_h, _samplers[1]);
        _descriptorSet->Bind("_materialSampler2"_h, _samplers[2]);
        _descriptorSet->Bind("_materialSampler3"_h, _samplers[3]);
        _stats.descriptorWrites = 4;
    }

    void MaterialResourceBindings::Upload(const MaterialStorage& materials, const MaterialTextureRegistry& textures)
    {
        bool changed = false;
        changed |= Bind("_materials"_h, materials.GetMaterials().GetBuffer(), _materials);
        changed |= Bind("_materialInstances"_h, materials.GetMaterialInstances().GetBuffer(), _materialInstances);
        changed |= Bind("_materialParameters"_h, materials.GetParameterStorage().GetBuffer().GetBuffer(), _parameters);
        changed |= Bind("_materialTextureIndices"_h, materials.GetTextureIndices().GetBuffer(), _textureIndices);
        changed |= Bind("_materialSamplerIDs"_h, materials.GetSamplerIDs().GetBuffer(), _samplerIDs);

        if (_textures != textures.GetTextureArray())
        {
            _descriptorSet->Bind("_materialTextures"_h, textures.GetTextureArray());
            _textures = textures.GetTextureArray();
            ++_stats.descriptorWrites;
            changed = true;
        }
        _stats.revisions += changed ? 1u : 0u;
    }

    bool MaterialResourceBindings::Bind(StringUtils::StringHash name, Renderer::BufferID buffer, Renderer::BufferID& current)
    {
        if (buffer == current)
            return false;
        _descriptorSet->Bind(name, buffer);
        current = buffer;
        ++_stats.descriptorWrites;
        return true;
    }
}
