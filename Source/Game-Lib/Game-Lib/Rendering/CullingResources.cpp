#include "CullingResources.h"
#include "CulledRenderer.h"

#include "Game-Lib/Rendering/RenderUtils.h"

CullingResourcesBase::CullingResourcesBase()
    : _occluderFillDescriptorSet(Renderer::DescriptorSetSlot::PER_PASS)
    , _cullingDescriptorSet(Renderer::DescriptorSetSlot::PER_PASS)
    , _geometryFillDescriptorSet(Renderer::DescriptorSetSlot::PER_PASS)
    , _geometryPassDescriptorSet(Renderer::DescriptorSetSlot::PER_PASS)
    , _createIndirectAfterCullingDescriptorSet(Renderer::DescriptorSetSlot::PER_PASS)
{

}

void CullingResourcesBase::Init(InitParams& params)
{
    NC_ASSERT(params.renderer != nullptr, "CullingResources : params.renderer is nullptr");
    NC_ASSERT(params.culledRenderer != nullptr, "CullingResources : params.culledRenderer is nullptr");

    _renderer = params.renderer;
    _bufferNamePrefix = params.bufferNamePrefix;
    _enableTwoStepCulling = params.enableTwoStepCulling;
    _materialPassDescriptorSet = params.materialPassDescriptorSet;
    _isInstanced = params.isInstanced;

    params.culledRenderer->InitCullingResources(*this);

    // Create DrawCountBuffer
    {
        Renderer::BufferDesc desc;
        desc.name = _bufferNamePrefix + "DrawCountBuffer";
        desc.size = sizeof(u32);
        desc.usage = Renderer::BufferUsage::INDIRECT_ARGUMENT_BUFFER | Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION | Renderer::BufferUsage::TRANSFER_SOURCE;
        _instanceCountBuffer = _renderer->CreateBuffer(_instanceCountBuffer, desc);

        if (!_isInstanced)
        {
            _occluderFillDescriptorSet.Bind("_drawCount"_h, _instanceCountBuffer);
            _cullingDescriptorSet.Bind("_drawCount"_h, _instanceCountBuffer);
            _geometryFillDescriptorSet.Bind("_drawCount"_h, _instanceCountBuffer);
        }
        _createIndirectAfterCullingDescriptorSet.Bind("_drawCount"_h, _instanceCountBuffer);

        desc.size = sizeof(u32) * Renderer::Settings::MAX_VIEWS;
        desc.name = _bufferNamePrefix + "DrawCountRBBuffer";
        desc.usage = Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION;
        desc.cpuAccess = Renderer::BufferCPUAccess::ReadOnly;
        _drawCountReadBackBuffer = _renderer->CreateBuffer(_drawCountReadBackBuffer, desc);

        if (_enableTwoStepCulling)
        {
            desc.name = _bufferNamePrefix + "OccluderDrawCountRBBuffer";
            _occluderDrawCountReadBackBuffer = _renderer->CreateBuffer(_occluderDrawCountReadBackBuffer, desc);
        }
    }

    // Create TriangleCountBuffer
    {
        Renderer::BufferDesc desc;
        desc.name = _bufferNamePrefix + "TriangleCountBuffer";
        desc.size = sizeof(u32);
        desc.usage = Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION | Renderer::BufferUsage::TRANSFER_SOURCE;
        _triangleCountBuffer = _renderer->CreateBuffer(_triangleCountBuffer, desc);

        if (!_isInstanced)
        {
            _occluderFillDescriptorSet.Bind("_triangleCount"_h, _triangleCountBuffer);
            _cullingDescriptorSet.Bind("_triangleCount"_h, _triangleCountBuffer);
            _geometryFillDescriptorSet.Bind("_triangleCount"_h, _triangleCountBuffer);
        }
        _createIndirectAfterCullingDescriptorSet.Bind("_triangleCount"_h, _triangleCountBuffer);

        desc.size = sizeof(u32) * Renderer::Settings::MAX_VIEWS;
        desc.name = _bufferNamePrefix + "TriangleCountRBBuffer";
        desc.cpuAccess = Renderer::BufferCPUAccess::ReadOnly;
        _triangleCountReadBackBuffer = _renderer->CreateBuffer(_triangleCountReadBackBuffer, desc);

        if (_enableTwoStepCulling)
        {
            desc.name = _bufferNamePrefix + "OccluderTriangleCountRBBuffer";
            _occluderTriangleCountReadBackBuffer = _renderer->CreateBuffer(_occluderTriangleCountReadBackBuffer, desc);
        }
    }

    _instanceRefs.SetDebugName(_bufferNamePrefix + " InstanceRefs");
    _instanceRefs.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);
}

void CullingResourcesBase::Update(f32 deltaTime, bool cullingEnabled)
{
    // Read back from the culling counters
    for (u32 i = 0; i < Renderer::Settings::MAX_VIEWS; i++)
    {
        _numSurvivingInstances[i] = _numInstances;
        _numSurvivingTriangles[i] = _numTriangles;
    }

    if (cullingEnabled && _numInstances > 0)
    {
        // Drawcalls

        if (_enableTwoStepCulling)
        {
            u32* count = static_cast<u32*>(_renderer->MapBuffer(_occluderDrawCountReadBackBuffer));
            if (count != nullptr)
            {
                _numSurvivingOccluderInstances = *count;
            }
            _renderer->UnmapBuffer(_occluderDrawCountReadBackBuffer);
        }

        {
            u32* count = static_cast<u32*>(_renderer->MapBuffer(_drawCountReadBackBuffer));
            if (count != nullptr)
            {
                for (u32 i = 0; i < Renderer::Settings::MAX_VIEWS; i++)
                {
                    _numSurvivingInstances[i] = count[i];
                }
            }
            _renderer->UnmapBuffer(_drawCountReadBackBuffer);
        }

        // Triangles
        if (_enableTwoStepCulling)
        {
            u32* count = static_cast<u32*>(_renderer->MapBuffer(_occluderTriangleCountReadBackBuffer));
            if (count != nullptr)
            {
                _numSurvivingOccluderTriangles = *count;
            }
            _renderer->UnmapBuffer(_occluderTriangleCountReadBackBuffer);
        }

        {
            u32* count = static_cast<u32*>(_renderer->MapBuffer(_triangleCountReadBackBuffer));
            if (count != nullptr)
            {
                for (u32 i = 0; i < Renderer::Settings::MAX_VIEWS; i++)
                {
                    _numSurvivingTriangles[i] = count[i];
                }
            }
            _renderer->UnmapBuffer(_triangleCountReadBackBuffer);
        }
    }
}

bool CullingResourcesBase::SyncToGPU(bool forceRecount)
{
    bool gotRecreated = false;
    
    if (_instanceRefs.SyncToGPU(_renderer))
    {
        if (_isInstanced)
        {
            _occluderFillDescriptorSet.Bind("_instanceRefTable"_h, _instanceRefs.GetBuffer());
            _cullingDescriptorSet.Bind("_instanceRefTable"_h, _instanceRefs.GetBuffer());
        }
        //_geometryFillDescriptorSet.Bind2("_instanceRefTable"_h, _instanceRefs.GetBuffer());
        _geometryPassDescriptorSet.Bind("_instanceRefTable"_h, _instanceRefs.GetBuffer(), true);
        if (_materialPassDescriptorSet != nullptr)
        {
            _materialPassDescriptorSet->Bind("_opaqueInstanceRefTable"_h, _instanceRefs.GetBuffer());
        }

        u32 numInstanceCapacity = static_cast<u32>(_instanceRefs.Capacity());

        // (Re)create Culled Instance Lookup Table Buffer
        {
            Renderer::BufferDesc desc;
            desc.name = _bufferNamePrefix + "CulledInstanceLookupTableBuffer";
            desc.size = sizeof(u32) * numInstanceCapacity;
            desc.usage = Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION;

            _culledInstanceLookupTableBuffer = _renderer->CreateBuffer(_culledInstanceLookupTableBuffer, desc);

            if (_isInstanced)
            {
                _occluderFillDescriptorSet.Bind("_culledInstanceLookupTable"_h, _culledInstanceLookupTableBuffer);
                _cullingDescriptorSet.Bind("_culledInstanceLookupTable"_h, _culledInstanceLookupTableBuffer);
            }
            //_geometryFillDescriptorSet.Bind("_culledInstanceLookupTable"_h, _culledInstanceLookupTableBuffer);
            _geometryPassDescriptorSet.Bind("_culledInstanceLookupTable"_h, _culledInstanceLookupTableBuffer, true);
            if (_materialPassDescriptorSet != nullptr)
            {
                _materialPassDescriptorSet->Bind("_opaqueCulledInstanceLookupTable"_h, _culledInstanceLookupTableBuffer);
            }
        }

        // (Re)create Culled DrawCall Bitmask buffer
        if (_enableTwoStepCulling)
        {
            _culledInstanceBitMaskBufferSizePerView = RenderUtils::CalcCullingBitmaskSize(numInstanceCapacity);
            _culledInstanceBitMaskBufferUintsPerView = RenderUtils::CalcCullingBitmaskUints(numInstanceCapacity);

            Renderer::BufferDesc desc;
            desc.size = _culledInstanceBitMaskBufferSizePerView * Renderer::Settings::MAX_VIEWS;
            desc.usage = Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION;

            for (u32 i = 0; i < _culledDrawCallsBitMaskBuffer.Num; i++)
            {
                desc.name = _bufferNamePrefix + "CulledDrawCallsBitMaskBuffer" + std::to_string(i);
                _culledDrawCallsBitMaskBuffer.Get(i) = _renderer->CreateAndFillBuffer(_culledDrawCallsBitMaskBuffer.Get(i), desc, [](void* mappedMemory, size_t size)
                {
                    memset(mappedMemory, 0, size);
                });
            }
        }

        gotRecreated = true;
    }

    return gotRecreated;
}

void CullingResourcesBase::Clear()
{
    _instanceRefs.Clear();
    _numInstances = 0;
}

void CullingResourcesBase::ResetCullingStats()
{
    _numSurvivingOccluderInstances = 0;
    _numSurvivingOccluderTriangles = 0;

    for (u32 i = 0; i < Renderer::Settings::MAX_VIEWS; i++)
    {
        _numSurvivingInstances[i] = 0;
        _numSurvivingTriangles[i] = 0; 
    }
}

void CullingResourcesIndexedBase::Init(InitParams& params)
{
    CullingResourcesBase::Init(params);

    // DrawCalls
    _drawCalls.SetDebugName(params.bufferNamePrefix + "DrawCallBuffer");
    _drawCalls.SetUsage(Renderer::BufferUsage::INDIRECT_ARGUMENT_BUFFER | Renderer::BufferUsage::STORAGE_BUFFER);
}

u32 CullingResourcesIndexedBase::Add()
{
    return _drawCalls.Add();
}
u32 CullingResourcesIndexedBase::AddCount(u32 count)
{
    return _drawCalls.AddCount(count);
}

void CullingResourcesIndexedBase::Remove(u32 index)
{
    _drawCalls.Remove(index);
}

void CullingResourcesIndexedBase::Reserve(u32 count)
{
    _drawCalls.Reserve(count);
}

bool CullingResourcesIndexedBase::SyncToGPU(bool forceRecount)
{
    bool gotRecreated = CullingResourcesBase::SyncToGPU(forceRecount);
    
    // DrawCalls
    if (_drawCalls.SyncToGPU(_renderer))
    {
        if (_enableTwoStepCulling)
        {
            if (!_isInstanced)
            {
                _occluderFillDescriptorSet.Bind("_drawCalls"_h, _drawCalls.GetBuffer());
                _geometryFillDescriptorSet.Bind("_drawCalls"_h, _drawCalls.GetBuffer());
            }
        }
        _createIndirectAfterCullingDescriptorSet.Bind("_drawCalls"_h, _drawCalls.GetBuffer());
        _cullingDescriptorSet.Bind("_drawCalls"_h, _drawCalls.GetBuffer());
        if (_materialPassDescriptorSet != nullptr)
        {
            _materialPassDescriptorSet->Bind("_modelDraws"_h, _drawCalls.GetBuffer());
        }

        // (Re)create Culled Instance Counts Buffer
        {
            Renderer::BufferDesc desc;
            desc.name = _bufferNamePrefix + "CulledInstanceCountsBuffer";
            desc.size = sizeof(u32) * _drawCalls.Capacity();
            desc.usage = Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION;

            _culledInstanceCountsBuffer = _renderer->CreateBuffer(_culledInstanceCountsBuffer, desc);

            if (_isInstanced)
            {
                _occluderFillDescriptorSet.Bind("_culledInstanceCounts"_h, _culledInstanceCountsBuffer);
                _cullingDescriptorSet.Bind("_culledInstanceCounts"_h, _culledInstanceCountsBuffer);
                _geometryFillDescriptorSet.Bind("_culledInstanceCounts"_h, _culledInstanceCountsBuffer);
                _createIndirectAfterCullingDescriptorSet.Bind("_culledInstanceCounts"_h, _culledInstanceCountsBuffer);
            }
        }

        // CulledDrawCallBuffer, one for each view
        {
            Renderer::BufferDesc desc;
            desc.size = sizeof(Renderer::IndexedIndirectDraw) * _drawCalls.Capacity();
            desc.usage = Renderer::BufferUsage::INDIRECT_ARGUMENT_BUFFER | Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION;
            desc.name = _bufferNamePrefix + "CulledDrawCallBuffer";
            _culledDrawCallsBuffer = _renderer->CreateBuffer(_culledDrawCallsBuffer, desc);

            if (!_isInstanced)
            {
                _cullingDescriptorSet.Bind("_culledDrawCalls"_h, _culledDrawCallsBuffer);
                _occluderFillDescriptorSet.Bind("_culledDrawCalls"_h, _culledDrawCallsBuffer);
                _geometryFillDescriptorSet.Bind("_culledDrawCalls"_h, _culledDrawCallsBuffer);
            }
            _createIndirectAfterCullingDescriptorSet.Bind("_culledDrawCalls"_h, _culledDrawCallsBuffer);
        }

        // (Re)create CulledDrawCallCountBuffer
        {
            Renderer::BufferDesc desc;
            desc.name = _bufferNamePrefix + "CulledDrawCallCountsBuffer";
            desc.size = sizeof(u32);
            desc.usage = Renderer::BufferUsage::INDIRECT_ARGUMENT_BUFFER | Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION;

            _culledDrawCallCountBuffer = _renderer->CreateBuffer(_culledDrawCallCountBuffer, desc);

            _createIndirectAfterCullingDescriptorSet.Bind("_culledDrawCallCount"_h, _culledDrawCallCountBuffer);
        }

        gotRecreated = true;
    }

    if (gotRecreated || forceRecount)
    {
        // Count drawcalls, instances and triangles
        _numInstances = 0;
        _numTriangles = 0;

        u32 numDrawCalls = static_cast<u32>(_drawCalls.Count()); 
        for (u32 i = 0; i < numDrawCalls; i++) // TODO: This does not take into account gaps
        {
            Renderer::IndexedIndirectDraw& drawCall = _drawCalls[i];
            _numInstances += drawCall.instanceCount;
            _numTriangles += (drawCall.indexCount / 3) * drawCall.instanceCount;
        }
    }
    
    return gotRecreated;
}

void CullingResourcesIndexedBase::Clear()
{
    CullingResourcesBase::Clear();
    _drawCalls.Clear();
}

bool CullingResourcesIndexedBase::IsDirty() const
{
    return _drawCalls.IsDirty();
}
void CullingResourcesIndexedBase::SetDirtyRegion(size_t offset, size_t size)
{
    _drawCalls.SetDirtyRegion(offset, size);
}
void CullingResourcesIndexedBase::SetDirtyElement(size_t index)
{
    _drawCalls.SetDirtyElement(index);
}
void CullingResourcesIndexedBase::SetDirtyElements(size_t startIndex, size_t count)
{
    _drawCalls.SetDirtyElements(startIndex, count);
}
void CullingResourcesIndexedBase::SetDirty()
{
    _drawCalls.SetDirty();
}

u32 CullingResourcesIndexedBase::GetDrawCallCount()
{
    return static_cast<u32>(_drawCalls.Count());
}

void CullingResourcesIndexedBase::SetValidation(bool validation)
{
    _drawCalls.SetValidation(validation);
}

void CullingResourcesNonIndexedBase::Init(InitParams& params)
{
    CullingResourcesBase::Init(params);

    // DrawCalls
    _drawCalls.SetDebugName(params.bufferNamePrefix + "DrawCallBuffer");
    _drawCalls.SetUsage(Renderer::BufferUsage::INDIRECT_ARGUMENT_BUFFER | Renderer::BufferUsage::STORAGE_BUFFER);
}

u32 CullingResourcesNonIndexedBase::Add()
{
    return _drawCalls.Add();
}
u32 CullingResourcesNonIndexedBase::AddCount(u32 count)
{
    return _drawCalls.AddCount(count);
}

void CullingResourcesNonIndexedBase::Remove(u32 index)
{
    _drawCalls.Remove(index);
}

void CullingResourcesNonIndexedBase::Reserve(u32 count)
{
    _drawCalls.Reserve(count);
}

bool CullingResourcesNonIndexedBase::SyncToGPU(bool forceRecount)
{
    bool gotRecreated = CullingResourcesBase::SyncToGPU(forceRecount);
    
    // DrawCalls
    if (_drawCalls.SyncToGPU(_renderer))
    {
        if (_enableTwoStepCulling)
        {
            if (!_isInstanced)
            {
                _occluderFillDescriptorSet.Bind("_drawCalls"_h, _drawCalls.GetBuffer());
                _geometryFillDescriptorSet.Bind("_drawCalls"_h, _drawCalls.GetBuffer());
            }
            _createIndirectAfterCullingDescriptorSet.Bind("_drawCalls"_h, _drawCalls.GetBuffer());
        }
        _cullingDescriptorSet.Bind("_drawCalls"_h, _drawCalls.GetBuffer());
        if (_materialPassDescriptorSet != nullptr)
        {
            _materialPassDescriptorSet->Bind("_modelDraws"_h, _drawCalls.GetBuffer());
        }

        // (Re)create Culled Instance Counts Buffer
        {
            Renderer::BufferDesc desc;
            desc.name = _bufferNamePrefix + "CulledInstanceCountsBuffer";
            desc.size = sizeof(u32) * _drawCalls.Count();
            desc.usage = Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION;

            _culledInstanceCountsBuffer = _renderer->CreateBuffer(_culledInstanceCountsBuffer, desc);

            _occluderFillDescriptorSet.Bind("_culledInstanceCounts"_h, _culledInstanceCountsBuffer);
            _cullingDescriptorSet.Bind("_culledInstanceCounts"_h, _culledInstanceCountsBuffer);
            _geometryFillDescriptorSet.Bind("_culledInstanceCounts"_h, _culledInstanceCountsBuffer);
            _createIndirectAfterCullingDescriptorSet.Bind("_culledInstanceCounts"_h, _culledInstanceCountsBuffer);
        }

        // CulledDrawCallBuffer
        {
            Renderer::BufferDesc desc;
            desc.size = sizeof(Renderer::IndirectDraw) * _drawCalls.Count();
            desc.usage = Renderer::BufferUsage::INDIRECT_ARGUMENT_BUFFER | Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION;
            desc.name = _bufferNamePrefix + "CulledDrawCallBuffer";

            _culledDrawCallsBuffer = _renderer->CreateBuffer(_culledDrawCallsBuffer, desc);

            if (!_isInstanced)
            {
                _cullingDescriptorSet.Bind("_culledDrawCalls"_h, _culledDrawCallsBuffer);
                _occluderFillDescriptorSet.Bind("_culledDrawCalls"_h, _culledDrawCallsBuffer);
                _geometryFillDescriptorSet.Bind("_culledDrawCalls"_h, _culledDrawCallsBuffer);
            }
            _createIndirectAfterCullingDescriptorSet.Bind("_culledDrawCalls"_h, _culledDrawCallsBuffer);
        }

        // (Re)create CulledDrawCallCountBuffer
        {
            Renderer::BufferDesc desc;
            desc.name = _bufferNamePrefix + "CulledDrawCallCountsBuffer";
            desc.size = sizeof(u32);
            desc.usage = Renderer::BufferUsage::INDIRECT_ARGUMENT_BUFFER | Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION;

            _culledDrawCallCountBuffer = _renderer->CreateBuffer(_culledDrawCallCountBuffer, desc);

            _createIndirectAfterCullingDescriptorSet.Bind("_culledDrawCallCount"_h, _culledDrawCallCountBuffer);
        }

        gotRecreated = true;
    }

    if (gotRecreated || forceRecount)
    {
        // Count drawcalls and triangles
        _numInstances = 0;
        _numTriangles = 0;

        u32 numDrawCalls = static_cast<u32>(_drawCalls.Count());
        for (u32 i = 0; i < numDrawCalls; i++) // TODO: This does not take into account gaps
        {
            Renderer::IndirectDraw& drawCall = _drawCalls[i];
            _numInstances += drawCall.instanceCount;
            _numTriangles += (drawCall.vertexCount / 3) * drawCall.instanceCount;
        }
    }
    
    return gotRecreated;
}

void CullingResourcesNonIndexedBase::Clear()
{
    CullingResourcesBase::Clear();
    _drawCalls.Clear();
}

bool CullingResourcesNonIndexedBase::IsDirty() const
{
    return _drawCalls.IsDirty();
}
void CullingResourcesNonIndexedBase::SetDirtyRegion(size_t offset, size_t size)
{
    _drawCalls.SetDirtyRegion(offset, size);
}
void CullingResourcesNonIndexedBase::SetDirtyElement(size_t index)
{
    _drawCalls.SetDirtyElement(index);
}
void CullingResourcesNonIndexedBase::SetDirtyElements(size_t startIndex, size_t count)
{
    _drawCalls.SetDirtyElements(startIndex, count);
}
void CullingResourcesNonIndexedBase::SetDirty()
{
    _drawCalls.SetDirty();
}

u32 CullingResourcesNonIndexedBase::GetDrawCallCount()
{
    return static_cast<u32>(_drawCalls.Count());
}

void CullingResourcesNonIndexedBase::SetValidation(bool validation)
{
    _drawCalls.SetValidation(validation);
}