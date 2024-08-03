#pragma once

#include <Base/Types.h>
#include <Base/Util/DebugHandler.h>

#include <Renderer/FrameResource.h>
#include <Renderer/Buffer.h>
#include <Renderer/GPUVector.h>

struct InstanceRef
{
    u32 instanceID;
    u32 drawID;
};

class CullingResourcesBase
{
public:
    struct InitParams
    {
        Renderer::Renderer* renderer = nullptr;
        std::string bufferNamePrefix = "";
        Renderer::DescriptorSet* materialPassDescriptorSet = nullptr;

        bool enableTwoStepCulling = true;
    };
    virtual void Init(InitParams& params);

    virtual void Update(f32 deltaTime, bool cullingEnabled);

    virtual bool SyncToGPU();
    virtual void Clear();

    virtual u32 GetDrawCallsSize() = 0;
    virtual bool IsIndexed() = 0;

    virtual void Grow(u32 growthSize);
    virtual void Resize(u32 size);
    virtual void FitBuffersAfterLoad();

    std::atomic<u32>& GetDrawCallsIndex() { return _drawCallsIndex; }

    Renderer::GPUVector<InstanceRef>& GetInstanceRefs() { return _instanceRefs; }

    Renderer::BufferID GetCulledDrawCallsBitMaskBuffer(u8 frame) { return _culledDrawCallsBitMaskBuffer.Get(frame); }
    u32 GetBitMaskBufferSizePerView() { return _culledInstanceBitMaskBufferSizePerView; }
    u32 GetBitMaskBufferUintsPerView() { return _culledInstanceBitMaskBufferUintsPerView; }

    Renderer::BufferID GetCulledInstanceCountsBuffer() { return _culledInstanceCountsBuffer; }
    Renderer::BufferID GetCulledInstanceLookupTableBuffer() { return _culledInstanceLookupTableBuffer; }

    Renderer::BufferID GetCulledDrawsBuffer() { return _culledDrawCallsBuffer; }
    Renderer::BufferID GetCulledDrawCallCountBuffer() { return _culledDrawCallCountBuffer; }

    Renderer::BufferID GetDrawCountBuffer() { return _instanceCountBuffer; }
    Renderer::BufferID GetTriangleCountBuffer() { return _triangleCountBuffer; }

    Renderer::BufferID GetOccluderDrawCountReadBackBuffer() { return _occluderDrawCountReadBackBuffer; }
    Renderer::BufferID GetOccluderTriangleCountReadBackBuffer() { return _occluderTriangleCountReadBackBuffer; }
    Renderer::BufferID GetDrawCountReadBackBuffer() { return _drawCountReadBackBuffer; }
    Renderer::BufferID GetTriangleCountReadBackBuffer() { return _triangleCountReadBackBuffer; }

    Renderer::DescriptorSet& GetOccluderFillDescriptorSet() { return _occluderFillDescriptorSet; }
    Renderer::DescriptorSet& GetCullingDescriptorSet() { return _cullingDescriptorSet; }
    Renderer::DescriptorSet& GetGeometryFillDescriptorSet() { return _geometryFillDescriptorSet; }
    Renderer::DescriptorSet& GetGeometryPassDescriptorSet() { return _geometryPassDescriptorSet; }

    bool HasSupportForTwoStepCulling() { return _enableTwoStepCulling; }

    void ResetCullingStats();

    // Instances stats
    u32 GetNumInstances() { return _numInstances; }
    u32 GetNumSurvivingOccluderInstances() { return _numSurvivingOccluderInstances; }
    u32 GetNumSurvivingInstances(u32 viewID) { return _numSurvivingInstances[viewID]; }

    // Triangle stats
    u32 GetNumTriangles() { return _numTriangles; }
    u32 GetNumSurvivingOccluderTriangles() { return _numSurvivingOccluderTriangles; }
    u32 GetNumSurvivingTriangles(u32 viewID) { return _numSurvivingTriangles[viewID]; }

protected:
    Renderer::Renderer* _renderer;
    std::string _bufferNamePrefix = "";
    bool _enableTwoStepCulling;

    std::atomic<u32> _drawCallsIndex = 0;

    Renderer::GPUVector<InstanceRef> _instanceRefs;

    FrameResource<Renderer::BufferID, 2> _culledDrawCallsBitMaskBuffer;
    u32 _culledInstanceBitMaskBufferSizePerView = 0;
    u32 _culledInstanceBitMaskBufferUintsPerView = 0;

    Renderer::BufferID _culledInstanceCountsBuffer;
    Renderer::BufferID _culledInstanceLookupTableBuffer;

    Renderer::BufferID _culledDrawCallsBuffer;
    Renderer::BufferID _culledDrawCallCountBuffer;
    Renderer::BufferID _instanceCountBuffer;
    Renderer::BufferID _triangleCountBuffer;

    Renderer::BufferID _drawCountReadBackBuffer;
    Renderer::BufferID _triangleCountReadBackBuffer;
    Renderer::BufferID _occluderDrawCountReadBackBuffer;
    Renderer::BufferID _occluderTriangleCountReadBackBuffer;

    Renderer::DescriptorSet _occluderFillDescriptorSet;
    Renderer::DescriptorSet _cullingDescriptorSet;
    Renderer::DescriptorSet _geometryFillDescriptorSet;
    Renderer::DescriptorSet _geometryPassDescriptorSet;
    Renderer::DescriptorSet* _materialPassDescriptorSet;

    u32 _numInstances = 0;
    u32 _numSurvivingOccluderInstances = 0;
    u32 _numSurvivingInstances[Renderer::Settings::MAX_VIEWS] = { 0 }; // One for the main view, then one per shadow cascade
    
    u32 _numTriangles = 0;
    u32 _numSurvivingOccluderTriangles = 0;
    u32 _numSurvivingTriangles[Renderer::Settings::MAX_VIEWS] = { 0 }; // One for the main view, then one per shadow cascade
};

class CullingResourcesIndexedBase : public CullingResourcesBase
{
public:
    void Init(InitParams& params) override;

    bool SyncToGPU() override;
    void Clear() override;

    u32 GetDrawCallsSize() override;
    bool IsIndexed() override { return true; }

    virtual void Grow(u32 growthSize) override;
    virtual void Resize(u32 size) override;
    virtual void FitBuffersAfterLoad() override;

    virtual void SetValidation(bool validation);

    Renderer::GPUVector<Renderer::IndexedIndirectDraw>& GetDrawCalls() { return _drawCalls; }

protected:
    Renderer::GPUVector<Renderer::IndexedIndirectDraw> _drawCalls;
};

class CullingResourcesNonIndexedBase : public CullingResourcesBase
{
public:
    void Init(InitParams& params) override;

    bool SyncToGPU() override;
    void Clear() override;

    u32 GetDrawCallsSize() override;
    bool IsIndexed() override { return false; }

    virtual void Grow(u32 growthSize) override;
    virtual void Resize(u32 size) override;
    virtual void FitBuffersAfterLoad() override;

    virtual void SetValidation(bool validation);

    Renderer::GPUVector<Renderer::IndirectDraw>& GetDrawCalls() { return _drawCalls; }

protected:
    Renderer::GPUVector<Renderer::IndirectDraw> _drawCalls;
};

template<class T>
class CullingResourcesIndexed : public CullingResourcesIndexedBase
{
public:
	void Init(InitParams& params) override
	{
        CullingResourcesIndexedBase::Init(params);

        // DrawCallDatas
        _drawCallDatas.SetDebugName(_bufferNamePrefix + "DrawCallDataBuffer");
        _drawCallDatas.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);

        SyncToGPU();
	}

	bool SyncToGPU() override
	{
        bool gotRecreated = CullingResourcesIndexedBase::SyncToGPU();

        // DrawCallDatas
        if (_drawCallDatas.SyncToGPU(_renderer))
        {
            _occluderFillDescriptorSet.Bind("_drawCallDatas"_h, _drawCallDatas.GetBuffer());
            _cullingDescriptorSet.Bind("_drawCallDatas"_h, _drawCallDatas.GetBuffer());
            _geometryFillDescriptorSet.Bind("_drawCallDatas"_h, _drawCallDatas.GetBuffer());
            _geometryPassDescriptorSet.Bind("_drawCallDatas"_h, _drawCallDatas.GetBuffer());
            _geometryPassDescriptorSet.Bind("_packedModelDrawCallDatas"_h, _drawCallDatas.GetBuffer()); // TODO: This should not be this hardcoded...
            if (_materialPassDescriptorSet != nullptr)
            {
                _materialPassDescriptorSet->Bind("_packedModelDrawCallDatas"_h, _drawCallDatas.GetBuffer()); // TODO: This should not be this hardcoded...
            }
            gotRecreated = true;
        }

        return gotRecreated;
	}

    void Clear() override
    {
        CullingResourcesIndexedBase::Clear();
        _drawCallDatas.Clear();
    }

    void Grow(u32 growthSize) override
    {
        CullingResourcesIndexedBase::Grow(growthSize);
        _drawCallDatas.Grow(growthSize);
    }

    void Resize(u32 size) override
    {
        CullingResourcesIndexedBase::Resize(size);
        _drawCallDatas.Resize(size);
    }

    void FitBuffersAfterLoad() override
    {
        CullingResourcesIndexedBase::FitBuffersAfterLoad();
        u32 numDrawCalls = _drawCallsIndex.load();
        _drawCallDatas.Resize(numDrawCalls);
    }

    void SetValidation(bool validation) override
    {
        CullingResourcesIndexedBase::SetValidation(validation);
        _drawCallDatas.SetValidation(validation);
    }

    Renderer::GPUVector<T>& GetDrawCallDatas() { return _drawCallDatas; }

private:
	Renderer::GPUVector<T> _drawCallDatas;
};

template<class T>
class CullingResourcesNonIndexed : public CullingResourcesNonIndexedBase
{
public:
    void Init(InitParams& params) override
    {
        CullingResourcesNonIndexedBase::Init(params);

        // DrawCallDatas
        _drawCallDatas.SetDebugName(_bufferNamePrefix + "DrawCallDataBuffer");
        _drawCallDatas.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);

        SyncToGPU();
    }

    bool SyncToGPU() override
    {
        bool gotRecreated = CullingResourcesNonIndexedBase::SyncToGPU();

        // DrawCallDatas
        if (_drawCallDatas.SyncToGPU(_renderer))
        {
            _occluderFillDescriptorSet.Bind("_drawCallDatas"_h, _drawCallDatas.GetBuffer());
            _cullingDescriptorSet.Bind("_drawCallDatas"_h, _drawCallDatas.GetBuffer());
            _geometryFillDescriptorSet.Bind("_drawCallDatas"_h, _drawCallDatas.GetBuffer());
            _geometryPassDescriptorSet.Bind("_drawCallDatas"_h, _drawCallDatas.GetBuffer());
            _geometryPassDescriptorSet.Bind("_packedModelDrawCallDatas"_h, _drawCallDatas.GetBuffer()); // TODO: This should not be this hardcoded...
            if (_materialPassDescriptorSet != nullptr)
            {
                _materialPassDescriptorSet->Bind("_packedModelDrawCallDatas"_h, _drawCallDatas.GetBuffer()); // TODO: This should not be this hardcoded...
            }
            gotRecreated = true;
        }

        return gotRecreated;
    }

    void Clear() override
    {
        CullingResourcesNonIndexedBase::Clear();
        _drawCallDatas.Clear();
    }

    void Grow(u32 growthSize) override
    {
        CullingResourcesNonIndexedBase::Grow(growthSize);
        _drawCallDatas.Grow(growthSize);
    }

    void Resize(u32 size) override
    {
        CullingResourcesNonIndexedBase::Resize(size);
        _drawCallDatas.Resize(size);
    }

    void FitBuffersAfterLoad() override
    {
        CullingResourcesNonIndexedBase::FitBuffersAfterLoad();
        u32 numDrawCalls = _drawCallsIndex.load();
        _drawCallDatas.Resize(numDrawCalls);
    }

    void SetValidation(bool validation) override
    {
        CullingResourcesNonIndexedBase::SetValidation(validation);
        _drawCallDatas.SetValidation(validation);
    }

    Renderer::GPUVector<T>& GetDrawCallDatas() { return _drawCallDatas; }

private:
    Renderer::GPUVector<T> _drawCallDatas;
};