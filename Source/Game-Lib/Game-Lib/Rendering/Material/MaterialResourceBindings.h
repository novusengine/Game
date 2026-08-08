#pragma once

#include <Renderer/DescriptorSet.h>

#include <array>

namespace Renderer { class Renderer; }

namespace MaterialLoading
{
    class MaterialStorage;
    class MaterialTextureRegistry;

    struct MaterialResourceBindingStats
    {
        u32 revisions = 0;
        u32 descriptorWrites = 0;
    };

    // Owns GPU-side renderer-wide Material descriptor bindings and samplers.
    // Stable bindings are rewritten only when a backing allocation or texture heap changes.
    class MaterialResourceBindings
    {
      public:
        MaterialResourceBindings(Renderer::Renderer* renderer, Renderer::DescriptorSet* descriptorSet);

        void Upload(const MaterialStorage& materials, const MaterialTextureRegistry& textures);
        MaterialResourceBindingStats GetStats() const { return _stats; }

      private:
        bool Bind(StringUtils::StringHash name, Renderer::BufferID buffer, Renderer::BufferID& current);

        Renderer::DescriptorSet* _descriptorSet = nullptr;
        std::array<Renderer::SamplerID, 4> _samplers = {};
        Renderer::BufferID _materials = Renderer::BufferID::Invalid();
        Renderer::BufferID _materialInstances = Renderer::BufferID::Invalid();
        Renderer::BufferID _parameters = Renderer::BufferID::Invalid();
        Renderer::BufferID _textureIndices = Renderer::BufferID::Invalid();
        Renderer::BufferID _samplerIDs = Renderer::BufferID::Invalid();
        Renderer::TextureArrayID _textures = Renderer::TextureArrayID::Invalid();
        MaterialResourceBindingStats _stats;
    };
}
