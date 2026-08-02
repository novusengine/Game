#pragma once

#include "VisibilityPayload.h"

#include <Renderer/GPUVector.h>
#include <Renderer/RenderGraph.h>

namespace Renderer
{
    class DescriptorSet;
    class Renderer;
}

namespace Visibility
{
    struct LegacyModelRecord
    {
        u32 instanceRefID = 0;
        u32 triangleBase = 0;
    };

    // Owns the CPU-built, GPU-readable records used by the legacy model visibility path.
    // Each record lets the 32-bit payload retain a full primitive ID without constraining legacy draw sizes.
    class LegacyModelVisibilityRecords
    {
      public:
        LegacyModelVisibilityRecords();

        void Clear();
        u32 AddInstance(u32 instanceRefID, u32 triangleCount);
        void Upload(Renderer::Renderer* renderer, Renderer::DescriptorSet& modelSet);
        void RegisterUsage(Renderer::RenderGraphBuilder& builder, Renderer::BufferPassUsage usage) const;

      private:
        Renderer::GPUVector<LegacyModelRecord> _records;
        bool _overflowReported = false;
    };
} // namespace Visibility
