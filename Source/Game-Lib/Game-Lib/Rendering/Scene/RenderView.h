#pragma once

#include <Base/Types.h>

#include <Renderer/Descriptors/DepthImageDesc.h>
#include <Renderer/Descriptors/ImageDesc.h>

namespace RenderScenes
{
    class RenderScene;

    enum class RenderViewLifetime : u8
    {
        Persistent,
        Transient
    };

    struct RenderViewDesc
    {
        u64 viewID = 0;
        RenderScene* scene = nullptr;
        u32 cameraIndex = 0;
        Renderer::ImageID colorTarget = Renderer::ImageID::Invalid();
        Renderer::DepthImageID depthTarget = Renderer::DepthImageID::Invalid();
        RenderViewLifetime lifetime = RenderViewLifetime::Persistent;
    };

    // Owns CPU-side camera, target, lifetime, and temporal-reset configuration for one renderer View.
    // It gives geometry systems one shared View identity while keeping their GPU state feature-specific.
    class RenderView
    {
      public:
        explicit RenderView(const RenderViewDesc& desc)
            : _viewID(desc.viewID), _scene(desc.scene), _cameraIndex(desc.cameraIndex), _colorTarget(desc.colorTarget),
              _depthTarget(desc.depthTarget), _lifetime(desc.lifetime)
        {
        }

        void RequestTemporalReset() { ++_temporalResetGeneration; }

        u64 GetID() const { return _viewID; }
        RenderScene* GetScene() const { return _scene; }
        u32 GetCameraIndex() const { return _cameraIndex; }
        Renderer::ImageID GetColorTarget() const { return _colorTarget; }
        Renderer::DepthImageID GetDepthTarget() const { return _depthTarget; }
        RenderViewLifetime GetLifetime() const { return _lifetime; }
        u32 GetTemporalResetGeneration() const { return _temporalResetGeneration; }

      private:
        u64 _viewID = 0;
        RenderScene* _scene = nullptr;
        u32 _cameraIndex = 0;
        Renderer::ImageID _colorTarget = Renderer::ImageID::Invalid();
        Renderer::DepthImageID _depthTarget = Renderer::DepthImageID::Invalid();
        RenderViewLifetime _lifetime = RenderViewLifetime::Persistent;
        u32 _temporalResetGeneration = 0;
    };
} // namespace RenderScenes
