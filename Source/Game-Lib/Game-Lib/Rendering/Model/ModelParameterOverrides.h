#pragma once

#include "Game-Lib/Rendering/Asset/RenderAssetHandles.h"
#include "Game-Lib/Rendering/Scene/RenderSceneHandles.h"

#include <Renderer/Descriptors/TextureDesc.h>

namespace RenderAssets { class RenderAssetResources; }
namespace RenderScenes { class RenderScene; }

namespace ModelLoading
{
    // Applies CPU-side typed Model parameter overrides to an instance's GPU-backed material table.
    // It follows the Model's authored bindings so one runtime value can update every affected material slot.
    class ModelParameterOverrides
    {
      public:
        explicit ModelParameterOverrides(RenderAssets::RenderAssetResources* assets) : _assets(assets) { }

        bool SetTexture(RenderScenes::RenderScene& scene, RenderScenes::ModelInstanceHandle instance,
                        RenderAssets::ModelHandle model, u64 parameterNameHash, Renderer::TextureID textureID);

      private:
        RenderAssets::RenderAssetResources* _assets = nullptr;
    };
} // namespace ModelLoading
