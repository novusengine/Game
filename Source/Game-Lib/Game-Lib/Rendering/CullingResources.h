#pragma once

#include <Base/Types.h>
#include <Base/Util/DebugHandler.h>

#include <Renderer/FrameResource.h>
#include <Renderer/Buffer.h>
#include <Renderer/GPUVector.h>

class CulledRenderer;

struct InstanceRef
{
    u32 instanceID;
    u32 drawID;
    u32 extraID; // For example ModelRenderer uses this for TextureData
    u32 padding;
};

class CullingResourcesBase
{
public:
    CullingResourcesBase();

    struct InitParams
    {
        Renderer::Renderer* renderer = nullptr;
        CulledRenderer* culledRenderer = nullptr;
        std::string bufferNamePrefix = "";
        Renderer::DescriptorSet* materialPassDescriptorSet = nullptr;

        bool enableTwoStepCulling = true;
        bool isInstanced = true;
    };
    virtual void Init(InitParams& params);

    virtual void Update(f32 deltaTime, bool cullingEnabled);

    virtual bool SyncToGPU(bool forceRecount);
    virtual void Clear();

    virtual u32 GetDrawCallCount() = 0;
    virtual bool IsIndexed() = 0;
    bool IsInstanced() { return _isInstanced; }

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
    Renderer::DescriptorSet& GetCreateIndirectAfterCullDescriptorSet() { return _createIndirectAfterCullingDescriptorSet; }
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
    bool _isInstanced;

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
    Renderer::DescriptorSet _createIndirectAfterCullingDescriptorSet;
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

    virtual u32 Add();
    virtual u32 AddCount(u32 count);

    virtual void Remove(u32 index);

    virtual void Reserve(u32 count);

    bool SyncToGPU(bool forceRecount) override;
    void Clear() override;

    virtual bool IsDirty() const;
    virtual void SetDirtyRegion(size_t offset, size_t size);
    virtual void SetDirtyElement(size_t index);
    virtual void SetDirtyElements(size_t startIndex, size_t count);
    virtual void SetDirty();

    u32 GetDrawCallCount() override;
    bool IsIndexed() override { return true; }

    virtual void SetValidation(bool validation);

    const Renderer::GPUVector<Renderer::IndexedIndirectDraw>& GetDrawCalls() { return _drawCalls; }

protected:
    Renderer::GPUVector<Renderer::IndexedIndirectDraw> _drawCalls;
};

class CullingResourcesNonIndexedBase : public CullingResourcesBase
{
public:
    void Init(InitParams& params) override;

    virtual u32 Add();
    virtual u32 AddCount(u32 count);

    virtual void Remove(u32 index);

    virtual void Reserve(u32 count);

    bool SyncToGPU(bool forceRecount) override;
    void Clear() override;

    virtual bool IsDirty() const;
    virtual void SetDirtyRegion(size_t offset, size_t size);
    virtual void SetDirtyElement(size_t index);
    virtual void SetDirtyElements(size_t startIndex, size_t count);
    virtual void SetDirty();

    u32 GetDrawCallCount() override;
    bool IsIndexed() override { return false; }

    virtual void SetValidation(bool validation);

    const Renderer::GPUVector<Renderer::IndirectDraw>& GetDrawCalls() { return _drawCalls; }

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

        //SyncToGPU(false);
    }

    virtual u32 Add() override
    {
        u32 baseIndex = CullingResourcesIndexedBase::Add();
        u32 index = _drawCallDatas.Add();
#if NC_DEBUG
        if (baseIndex != index)
        {
            NC_LOG_ERROR("CullingResourcesIndexed::Add() baseIndex != index");
        }
#endif
        return index;
    }
    virtual u32 AddCount(u32 count) override
    {
        u32 baseIndex = CullingResourcesIndexedBase::AddCount(count);
        u32 index = _drawCallDatas.AddCount(count);
#if NC_DEBUG
        if (baseIndex != index)
        {
            NC_LOG_ERROR("CullingResourcesIndexed::AddCount() baseIndex != index");
        }
#endif
        return index;
    }

    virtual void Remove(u32 index) override
    {
        CullingResourcesIndexedBase::Remove(index);
        _drawCallDatas.Remove(index);
    }

    virtual void Reserve(u32 count) override
    {
        CullingResourcesIndexedBase::Reserve(count);
        _drawCallDatas.Reserve(count);
    }

    bool SyncToGPU(bool forceRecount) override
    {
        bool gotRecreated = CullingResourcesIndexedBase::SyncToGPU(forceRecount);

        // DrawCallDatas
        if (_drawCallDatas.SyncToGPU(_renderer))
        {
            if (_isInstanced)
            {
                _occluderFillDescriptorSet.Bind("_drawCallDatas"_h, _drawCallDatas.GetBuffer());
                _geometryFillDescriptorSet.Bind("_drawCallDatas"_h, _drawCallDatas.GetBuffer());
            }
            _cullingDescriptorSet.Bind("_drawCallDatas"_h, _drawCallDatas.GetBuffer());
            _createIndirectAfterCullingDescriptorSet.Bind("_drawCallDatas"_h, _drawCallDatas.GetBuffer());
            _geometryPassDescriptorSet.Bind("_drawCallDatas"_h, _drawCallDatas.GetBuffer(), true);
            gotRecreated = true;
        }

        return gotRecreated;
    }

    void Clear() override
    {
        CullingResourcesIndexedBase::Clear();
        _drawCallDatas.Clear();
    }

    virtual bool IsDirty() const override
    {
        bool isDirty = CullingResourcesIndexedBase::IsDirty();
        return isDirty || _drawCallDatas.IsDirty();
    }
    virtual void SetDirtyRegion(size_t offset, size_t size) override
    {
        CullingResourcesIndexedBase::SetDirtyRegion(offset, size);
        _drawCallDatas.SetDirtyRegion(offset, size);
    }
    virtual void SetDirtyElement(size_t index) override
    {
        CullingResourcesIndexedBase::SetDirtyElement(index);
        _drawCallDatas.SetDirtyElement(index);
    }
    virtual void SetDirtyElements(size_t startIndex, size_t count) override
    {
        CullingResourcesIndexedBase::SetDirtyElements(startIndex, count);
        _drawCallDatas.SetDirtyElements(startIndex, count);
    }
    virtual void SetDirty() override
    {
        CullingResourcesIndexedBase::SetDirty();
        _drawCallDatas.SetDirty();
    }

    void SetValidation(bool validation) override
    {
        CullingResourcesIndexedBase::SetValidation(validation);
        _drawCallDatas.SetValidation(validation);
    }

    const Renderer::GPUVector<T>& GetDrawCallDatas() { return _drawCallDatas; }

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

        //SyncToGPU(false);
    }

    virtual u32 Add() override
    {
        u32 baseIndex = CullingResourcesNonIndexedBase::Add();
        u32 index = _drawCallDatas.Add();
#if NC_DEBUG
        if (baseIndex != index)
        {
            NC_LOG_ERROR("CullingResourcesNonIndexed::Add() baseIndex != index");
        }
#endif
        return index;
    }
    virtual u32 AddCount(u32 count) override
    {
        u32 baseIndex = CullingResourcesNonIndexedBase::AddCount(count);
        u32 index = _drawCallDatas.AddCount(count);
#if NC_DEBUG
        if (baseIndex != index)
        {
            NC_LOG_ERROR("CullingResourcesNonIndexed::AddCount() baseIndex != index");
        }
#endif
        return index;
    }

    virtual void Remove(u32 index) override
    {
        CullingResourcesNonIndexedBase::Remove(index);
        _drawCallDatas.Remove(index);
    }

    virtual void Reserve(u32 count) override
    {
        CullingResourcesNonIndexedBase::Reserve(count);
        _drawCallDatas.Reserve(count);
    }

    bool SyncToGPU(bool forceRecount) override
    {
        bool gotRecreated = CullingResourcesNonIndexedBase::SyncToGPU(forceRecount);

        // DrawCallDatas
        if (_drawCallDatas.SyncToGPU(_renderer))
        {
            _occluderFillDescriptorSet.Bind("_drawCallDatas"_h, _drawCallDatas.GetBuffer());
            _cullingDescriptorSet.Bind("_drawCallDatas"_h, _drawCallDatas.GetBuffer());
            _geometryFillDescriptorSet.Bind("_drawCallDatas"_h, _drawCallDatas.GetBuffer());
            _geometryPassDescriptorSet.Bind("_drawCallDatas"_h, _drawCallDatas.GetBuffer(), true);
            gotRecreated = true;
        }

        return gotRecreated;
    }

    void Clear() override
    {
        CullingResourcesNonIndexedBase::Clear();
        _drawCallDatas.Clear();
    }

    virtual bool IsDirty() const override
    {
        bool isDirty = CullingResourcesNonIndexedBase::IsDirty();
        return isDirty || _drawCallDatas.IsDirty();
    }
    virtual void SetDirtyRegion(size_t offset, size_t size) override
    {
        CullingResourcesNonIndexedBase::SetDirtyRegion(offset, size);
        _drawCallDatas.SetDirtyRegion(offset, size);
    }
    virtual void SetDirtyElement(size_t index) override
    {
        CullingResourcesNonIndexedBase::SetDirtyElement(index);
        _drawCallDatas.SetDirtyElement(index);
    }
    virtual void SetDirtyElements(size_t startIndex, size_t count) override
    {
        CullingResourcesNonIndexedBase::SetDirtyElements(startIndex, count);
        _drawCallDatas.SetDirtyElements(startIndex, count);
    }
    virtual void SetDirty() override
    {
        CullingResourcesNonIndexedBase::SetDirty();
        _drawCallDatas.SetDirty();
    }

    void SetValidation(bool validation) override
    {
        CullingResourcesNonIndexedBase::SetValidation(validation);
        _drawCallDatas.SetValidation(validation);
    }

    const Renderer::GPUVector<T>& GetDrawCallDatas() { return _drawCallDatas; }

private:
    Renderer::GPUVector<T> _drawCallDatas;
};