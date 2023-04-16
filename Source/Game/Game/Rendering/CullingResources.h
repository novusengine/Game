#pragma once

#include <Base/Types.h>
#include <Base/Util/DebugHandler.h>

#include <Renderer/FrameResource.h>
#include <Renderer/Buffer.h>
#include <Renderer/GPUVector.h>

class CullingResourcesBase
{
public:
    struct InitParams
    {
        Renderer::Renderer* renderer = nullptr;
        std::string bufferNamePrefix = "";
        Renderer::DescriptorSet* geometryPassDescriptorSet = nullptr;
        Renderer::DescriptorSet* materialPassDescriptorSet = nullptr;
    };
    virtual void Init(InitParams& params);

    virtual void Update(f32 deltaTime, bool cullingEnabled);

    virtual void SyncToGPU();
    virtual void Clear();
    virtual void Grow(u32 growthSize);

    Renderer::GPUVector<Renderer::IndexedIndirectDraw>& GetDrawCalls() { return _drawCalls; }
    std::atomic<u32>& GetDrawCallsIndex() { return _drawCallsIndex; }

    Renderer::BufferID GetCulledDrawCallsBitMaskBuffer(u8 frame) { return _culledDrawCallsBitMaskBuffer.Get(frame); }
    Renderer::BufferID GetCulledDrawsBuffer(u32 index) { return _culledDrawCallsBuffer[index]; }

    Renderer::BufferID GetDrawCountBuffer() { return _drawCountBuffer; }
    Renderer::BufferID GetTriangleCountBuffer() { return _triangleCountBuffer; }

    Renderer::BufferID GetOccluderDrawCountReadBackBuffer() { return _occluderDrawCountReadBackBuffer; }
    Renderer::BufferID GetOccluderTriangleCountReadBackBuffer() { return _occluderTriangleCountReadBackBuffer; }
    Renderer::BufferID GetDrawCountReadBackBuffer() { return _drawCountReadBackBuffer; }
    Renderer::BufferID GetTriangleCountReadBackBuffer() { return _triangleCountReadBackBuffer; }

    Renderer::DescriptorSet& GetOccluderFillDescriptorSet() { return _occluderFillDescriptorSet; }
    Renderer::DescriptorSet& GetCullingDescriptorSet() { return _cullingDescriptorSet; }

    // Drawcall stats
    u32 GetNumDrawCalls() { return static_cast<u32>(_drawCalls.Size()); }
    u32 GetNumSurvivingOccluderDrawCalls() { return _numSurvivingOccluderDrawCalls; }
    u32 GetNumSurvivingDrawCalls(u32 viewID) { return _numSurvivingDrawCalls[viewID]; }

    // Triangle stats
    u32 GetNumTriangles() { return _numTriangles; }
    u32 GetNumSurvivingOccluderTriangles() { return _numSurvivingOccluderTriangles; }
    u32 GetNumSurvivingTriangles(u32 viewID) { return _numSurvivingTriangles[viewID]; }

protected:
    Renderer::Renderer* _renderer;
    std::string _bufferNamePrefix = "";

    Renderer::GPUVector<Renderer::IndexedIndirectDraw> _drawCalls;
    std::atomic<u32> _drawCallsIndex = 0;

    FrameResource<Renderer::BufferID, 2> _culledDrawCallsBitMaskBuffer;
    Renderer::BufferID _culledDrawCallsBuffer[Renderer::Settings::MAX_VIEWS];
    Renderer::BufferID _drawCountBuffer;
    Renderer::BufferID _triangleCountBuffer;

    Renderer::BufferID _drawCountReadBackBuffer;
    Renderer::BufferID _triangleCountReadBackBuffer;
    Renderer::BufferID _occluderDrawCountReadBackBuffer;
    Renderer::BufferID _occluderTriangleCountReadBackBuffer;

    Renderer::DescriptorSet _occluderFillDescriptorSet;
    Renderer::DescriptorSet _cullingDescriptorSet;
    Renderer::DescriptorSet* _geometryPassDescriptorSet;
    Renderer::DescriptorSet* _materialPassDescriptorSet;

    u32 _numSurvivingOccluderDrawCalls;
    u32 _numSurvivingDrawCalls[Renderer::Settings::MAX_VIEWS]; // One for the main view, then one per shadow cascade

    u32 _numTriangles;
    u32 _numSurvivingOccluderTriangles;
    u32 _numSurvivingTriangles[Renderer::Settings::MAX_VIEWS]; // One for the main view, then one per shadow cascade
};

template<class T>
class CullingResources : public CullingResourcesBase
{
public:
	void Init(InitParams& params)
	{
        CullingResourcesBase::Init(params);

        // DrawCallDatas
        _drawCallDatas.SetDebugName(_bufferNamePrefix + "DrawCallDataBuffer");
        _drawCallDatas.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);

        SyncToGPU();
	}

	void SyncToGPU()
	{
        CullingResourcesBase::SyncToGPU();

        // DrawCallDatas
        if (_drawCallDatas.SyncToGPU(_renderer))
        {
            _cullingDescriptorSet.Bind("_drawCallDatas"_h, _drawCallDatas.GetBuffer());
            _geometryPassDescriptorSet->Bind("_packedModelDrawCallDatas"_h, _drawCallDatas.GetBuffer());
            _materialPassDescriptorSet->Bind("_packedModelDrawCallDatas"_h, _drawCallDatas.GetBuffer());
        }
	}

    void Clear()
    {
        CullingResourcesBase::Clear();
        _drawCallDatas.Clear();
    }

    void Grow(u32 growthSize)
    {
        CullingResourcesBase::Grow(growthSize);
        _drawCallDatas.Grow(growthSize);
    }

    Renderer::GPUVector<T>& GetDrawCallDatas() { return _drawCallDatas; }

private:
	Renderer::GPUVector<T> _drawCallDatas;
};
