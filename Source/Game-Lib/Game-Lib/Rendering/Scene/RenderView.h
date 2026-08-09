#pragma once

#include <Base/Types.h>

#include <Renderer/Descriptors/DepthImageDesc.h>
#include <Renderer/Descriptors/ImageDesc.h>
#include <Renderer/Descriptors/TextureDesc.h>

#include <limits>

namespace RenderScenes
{
    class RenderScene;

    inline constexpr u32 INVALID_RENDER_VIEW_CAMERA = std::numeric_limits<u32>::max();

    enum class RenderViewPassFamily : u32
    {
        None = 0,
        Models = 1u << 0u
    };

    enum class RenderViewLifetime : u8
    {
        Persistent,
        Transient
    };

    enum class RenderViewRefresh : u8
    {
        Continuous,
        Retained
    };

    struct RenderViewDesc
    {
        u64 viewID = 0;
        std::string debugName;
        RenderScene* scene = nullptr;
        u32 cameraIndex = 0;
        uvec2 dimensions = {};
        Renderer::ImageDimensionType dimensionType = Renderer::ImageDimensionType::DIMENSION_ABSOLUTE;
        Renderer::ImageID visibilityTarget = Renderer::ImageID::Invalid();
        Renderer::ImageID normalTarget = Renderer::ImageID::Invalid();
        Renderer::ImageID colorTarget = Renderer::ImageID::Invalid();
        Renderer::DepthImageID depthTarget = Renderer::DepthImageID::Invalid();
        Renderer::TextureID retainedOutput = Renderer::TextureID::Invalid();
        RenderViewPassFamily passFamilies = RenderViewPassFamily::Models;
        RenderViewLifetime lifetime = RenderViewLifetime::Persistent;
        RenderViewRefresh refresh = RenderViewRefresh::Continuous;
        bool clearTargets = false;
    };

    // Stores the CPU-side camera, targets, scheduling policy, and temporal-reset configuration for one View.
    // It lets feature renderers share one View identity while retaining their own GPU-side state.
    class RenderView
    {
      public:
        explicit RenderView(const RenderViewDesc& desc)
            : _viewID(desc.viewID), _debugName(desc.debugName), _scene(desc.scene), _cameraIndex(desc.cameraIndex), _dimensions(desc.dimensions),
              _dimensionType(desc.dimensionType),
              _visibilityTarget(desc.visibilityTarget), _normalTarget(desc.normalTarget),
              _colorTarget(desc.colorTarget), _depthTarget(desc.depthTarget), _retainedOutput(desc.retainedOutput),
              _passFamilies(desc.passFamilies), _lifetime(desc.lifetime), _refresh(desc.refresh),
              _clearTargets(desc.clearTargets)
        {
        }

        void MarkDirty() { _dirty = true; }
        void MarkRendered() { _dirty = false; }
        void SetDimensions(const uvec2& dimensions)
        {
            if (dimensions.x == 0 || dimensions.y == 0 || _dimensions == dimensions)
                return;
            _dimensions = dimensions;
            RequestTemporalReset();
        }
        void RequestTemporalReset()
        {
            ++_temporalResetGeneration;
            _dirty = true;
        }

        u64 GetID() const { return _viewID; }
        const std::string& GetDebugName() const { return _debugName; }
        RenderScene* GetScene() const { return _scene; }
        u32 GetCameraIndex() const { return _cameraIndex; }
        const uvec2& GetDimensions() const { return _dimensions; }
        Renderer::ImageDimensionType GetDimensionType() const { return _dimensionType; }
        Renderer::ImageID GetVisibilityTarget() const { return _visibilityTarget; }
        Renderer::ImageID GetNormalTarget() const { return _normalTarget; }
        Renderer::ImageID GetColorTarget() const { return _colorTarget; }
        Renderer::DepthImageID GetDepthTarget() const { return _depthTarget; }
        Renderer::TextureID GetRetainedOutput() const { return _retainedOutput; }
        bool HasPassFamily(RenderViewPassFamily family) const
        {
            return (static_cast<u32>(_passFamilies) & static_cast<u32>(family)) != 0;
        }
        RenderViewLifetime GetLifetime() const { return _lifetime; }
        RenderViewRefresh GetRefresh() const { return _refresh; }
        u32 GetTemporalResetGeneration() const { return _temporalResetGeneration; }
        bool ShouldRender() const { return _refresh == RenderViewRefresh::Continuous || _dirty; }
        bool ShouldClearTargets() const { return _clearTargets; }

      private:
        u64 _viewID = 0;
        std::string _debugName;
        RenderScene* _scene = nullptr;
        u32 _cameraIndex = 0;
        uvec2 _dimensions = {};
        Renderer::ImageDimensionType _dimensionType = Renderer::ImageDimensionType::DIMENSION_ABSOLUTE;
        Renderer::ImageID _visibilityTarget = Renderer::ImageID::Invalid();
        Renderer::ImageID _normalTarget = Renderer::ImageID::Invalid();
        Renderer::ImageID _colorTarget = Renderer::ImageID::Invalid();
        Renderer::DepthImageID _depthTarget = Renderer::DepthImageID::Invalid();
        Renderer::TextureID _retainedOutput = Renderer::TextureID::Invalid();
        RenderViewPassFamily _passFamilies = RenderViewPassFamily::None;
        RenderViewLifetime _lifetime = RenderViewLifetime::Persistent;
        RenderViewRefresh _refresh = RenderViewRefresh::Continuous;
        u32 _temporalResetGeneration = 0;
        bool _dirty = true;
        bool _clearTargets = false;
    };
} // namespace RenderScenes
