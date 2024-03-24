#include "CullingResources.h"
#include "CullingResources.h"

#include "Game/Rendering/RenderUtils.h"

void CullingResourcesBase::Init(InitParams& params)
{
    DebugHandler::Assert(params.renderer != nullptr, "CullingResources : params.renderer is nullptr");

    _renderer = params.renderer;
    _bufferNamePrefix = params.bufferNamePrefix;
    _enableTwoStepCulling = params.enableTwoStepCulling;
    _materialPassDescriptorSet = params.materialPassDescriptorSet;

    // Create DrawCountBuffer
    {
        Renderer::BufferDesc desc;
        desc.name = _bufferNamePrefix + "DrawCountBuffer";
        desc.size = sizeof(u32) * Renderer::Settings::MAX_VIEWS; // One per view
        desc.usage = Renderer::BufferUsage::INDIRECT_ARGUMENT_BUFFER | Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION | Renderer::BufferUsage::TRANSFER_SOURCE;
        _instanceCountBuffer = _renderer->CreateBuffer(_instanceCountBuffer, desc);

        _occluderFillDescriptorSet.Bind("_drawCount"_h, _instanceCountBuffer);
        _cullingDescriptorSet.Bind("_drawCount"_h, _instanceCountBuffer);

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
        desc.size = sizeof(u32) * Renderer::Settings::MAX_VIEWS; // One per view
        desc.usage = Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION | Renderer::BufferUsage::TRANSFER_SOURCE;
        _triangleCountBuffer = _renderer->CreateBuffer(_triangleCountBuffer, desc);

        _occluderFillDescriptorSet.Bind("_triangleCount"_h, _triangleCountBuffer);
        _cullingDescriptorSet.Bind("_triangleCount"_h, _triangleCountBuffer);

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

    if (cullingEnabled)
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

bool CullingResourcesBase::SyncToGPU()
{
    bool gotRecreated = false;
    {
        if (_instanceRefs.SyncToGPU(_renderer))
        {
            _occluderFillDescriptorSet.Bind("_instanceRefTable"_h, _instanceRefs.GetBuffer());
            _cullingDescriptorSet.Bind("_instanceRefTable"_h, _instanceRefs.GetBuffer());
            _geometryPassDescriptorSet.Bind("_instanceRefTable"_h, _instanceRefs.GetBuffer());

            u32 numInstances = static_cast<u32>(_instanceRefs.Size());

            // (Re)create Culled Instance Lookup Table Buffer
            {
                Renderer::BufferDesc desc;
                desc.name = _bufferNamePrefix + "CulledInstanceLookupTableBuffer";
                desc.size = sizeof(u32) * numInstances;
                desc.usage = Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION;

                _culledInstanceLookupTableBuffer = _renderer->CreateBuffer(_culledInstanceLookupTableBuffer, desc);

                _occluderFillDescriptorSet.Bind("_culledInstanceLookupTable"_h, _culledInstanceLookupTableBuffer);
                _cullingDescriptorSet.Bind("_culledInstanceLookupTable"_h, _culledInstanceLookupTableBuffer);
                _geometryPassDescriptorSet.Bind("_culledInstanceLookupTable"_h, _culledInstanceLookupTableBuffer);
            }

            // (Re)create Culled DrawCall Bitmask buffer
            if (_enableTwoStepCulling)
            {
                Renderer::BufferDesc desc;
                desc.size = RenderUtils::CalcCullingBitmaskSize(numInstances);
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
    }

    return gotRecreated;
}

void CullingResourcesBase::Clear()
{
    _drawCallsIndex.store(0);
    _instanceRefs.Clear();
}

void CullingResourcesBase::Grow(u32 growthSize)
{
    _instanceRefs.Grow(growthSize);
}

void CullingResourcesBase::Resize(u32 size)
{
    _instanceRefs.Resize(size);
}

void CullingResourcesBase::FitBuffersAfterLoad()
{
    u32 numDrawCalls = _drawCallsIndex.load();
    _instanceRefs.Resize(numDrawCalls);
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

bool CullingResourcesIndexedBase::SyncToGPU()
{
    bool gotRecreated = CullingResourcesBase::SyncToGPU();
    {
        // DrawCalls
        if (_drawCalls.SyncToGPU(_renderer))
        {
            if (_enableTwoStepCulling)
            {
                _occluderFillDescriptorSet.Bind("_drawCalls"_h, _drawCalls.GetBuffer());
            }
            _cullingDescriptorSet.Bind("_drawCalls"_h, _drawCalls.GetBuffer());
            _geometryPassDescriptorSet.Bind("_modelDraws"_h, _drawCalls.GetBuffer());
            if (_materialPassDescriptorSet != nullptr)
            {
                _materialPassDescriptorSet->Bind("_modelDraws"_h, _drawCalls.GetBuffer());
            }

            // (Re)create Culled Instance Counts Buffer
            {
                Renderer::BufferDesc desc;
                desc.name = _bufferNamePrefix + "CulledInstanceCountsBuffer";
                desc.size = sizeof(u32) * _drawCalls.Size();
                desc.usage = Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION;

                _culledInstanceCountsBuffer = _renderer->CreateBuffer(_culledInstanceCountsBuffer, desc);

                _occluderFillDescriptorSet.Bind("_culledInstanceCounts"_h, _culledInstanceCountsBuffer);
                _cullingDescriptorSet.Bind("_culledInstanceCounts"_h, _culledInstanceCountsBuffer);
            }

            // CulledDrawCallBuffer, one for each view
            {
                Renderer::BufferDesc desc;
                desc.size = sizeof(Renderer::IndexedIndirectDraw) * _drawCalls.Size();
                desc.usage = Renderer::BufferUsage::INDIRECT_ARGUMENT_BUFFER | Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION;

                for (u32 i = 0; i < Renderer::Settings::MAX_VIEWS; i++)
                {
                    desc.name = _bufferNamePrefix + "CulledDrawCallBuffer" + std::to_string(i);

                    _culledDrawCallsBuffer[i] = _renderer->CreateBuffer(_culledDrawCallsBuffer[i], desc);

                }
                _cullingDescriptorSet.Bind("_culledDrawCalls"_h, _culledDrawCallsBuffer[0]);
                _occluderFillDescriptorSet.Bind("_culledDrawCalls"_h, _culledDrawCallsBuffer[0]);
            }

            // (Re)create CulledDrawCallCountBuffer
            {
                Renderer::BufferDesc desc;
                desc.name = _bufferNamePrefix + "CulledDrawCallCountsBuffer";
                desc.size = sizeof(u32);
                desc.usage = Renderer::BufferUsage::INDIRECT_ARGUMENT_BUFFER | Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION;

                _culledDrawCallCountBuffer = _renderer->CreateBuffer(_culledDrawCallCountBuffer, desc);

                _cullingDescriptorSet.Bind("_culledDrawCallCount"_h, _culledDrawCallCountBuffer);
            }

            gotRecreated = true;
        }

        if (gotRecreated)
        {
            // Count drawcalls, instances and triangles
            _numInstances = 0;
            _numTriangles = 0;

            std::vector<Renderer::IndexedIndirectDraw>& drawCalls = _drawCalls.Get();
            for (Renderer::IndexedIndirectDraw& drawCall : drawCalls)
            {
                _numInstances += drawCall.instanceCount;
                _numTriangles += (drawCall.indexCount / 3) * drawCall.instanceCount;
            }
        }
    }
    return gotRecreated;
}

void CullingResourcesIndexedBase::Clear()
{
    CullingResourcesBase::Clear();
    _drawCalls.Clear();
}

u32 CullingResourcesIndexedBase::GetDrawCallsSize()
{
    return static_cast<u32>(_drawCalls.Size());
}

void CullingResourcesIndexedBase::Grow(u32 growthSize)
{
    CullingResourcesBase::Grow(growthSize);
    _drawCalls.Grow(growthSize);
}

void CullingResourcesIndexedBase::Resize(u32 size)
{
    CullingResourcesBase::Resize(size);
    _drawCalls.Resize(size);
}

void CullingResourcesIndexedBase::FitBuffersAfterLoad()
{
    CullingResourcesBase::FitBuffersAfterLoad();
    u32 numDrawCalls = _drawCallsIndex.load();
    _drawCalls.Resize(numDrawCalls);
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

bool CullingResourcesNonIndexedBase::SyncToGPU()
{
    bool gotRecreated = CullingResourcesBase::SyncToGPU();
    {
        // DrawCalls
        if (_drawCalls.SyncToGPU(_renderer))
        {
            if (_enableTwoStepCulling)
            {
                _occluderFillDescriptorSet.Bind("_drawCalls"_h, _drawCalls.GetBuffer());
            }
            _cullingDescriptorSet.Bind("_drawCalls"_h, _drawCalls.GetBuffer());
            _geometryPassDescriptorSet.Bind("_modelDraws"_h, _drawCalls.GetBuffer());
            if (_materialPassDescriptorSet != nullptr)
            {
                _materialPassDescriptorSet->Bind("_modelDraws"_h, _drawCalls.GetBuffer());
            }

            // (Re)create Culled Instance Counts Buffer
            {
                Renderer::BufferDesc desc;
                desc.name = _bufferNamePrefix + "CulledInstanceCountsBuffer";
                desc.size = sizeof(u32) * _drawCalls.Size();
                desc.usage = Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION;

                _culledInstanceCountsBuffer = _renderer->CreateBuffer(_culledInstanceCountsBuffer, desc);

                _occluderFillDescriptorSet.Bind("_culledInstanceCounts"_h, _culledInstanceCountsBuffer);
                _cullingDescriptorSet.Bind("_culledInstanceCounts"_h, _culledInstanceCountsBuffer);
            }

            // CulledDrawCallBuffer, one for each view
            {
                Renderer::BufferDesc desc;
                desc.size = sizeof(Renderer::IndirectDraw) * _drawCalls.Size();
                desc.usage = Renderer::BufferUsage::INDIRECT_ARGUMENT_BUFFER | Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION;

                for (u32 i = 0; i < Renderer::Settings::MAX_VIEWS; i++)
                {
                    desc.name = _bufferNamePrefix + "CulledDrawCallBuffer" + std::to_string(i);

                    _culledDrawCallsBuffer[i] = _renderer->CreateBuffer(_culledDrawCallsBuffer[i], desc);
                }
                _cullingDescriptorSet.Bind("_culledDrawCalls"_h, _culledDrawCallsBuffer[0]);
                _occluderFillDescriptorSet.Bind("_culledDrawCalls"_h, _culledDrawCallsBuffer[0]);
            }

            // (Re)create CulledDrawCallCountBuffer
            {
                Renderer::BufferDesc desc;
                desc.name = _bufferNamePrefix + "CulledDrawCallCountsBuffer";
                desc.size = sizeof(u32);
                desc.usage = Renderer::BufferUsage::INDIRECT_ARGUMENT_BUFFER | Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION;

                _culledDrawCallCountBuffer = _renderer->CreateBuffer(_culledDrawCallCountBuffer, desc);

                _cullingDescriptorSet.Bind("_culledDrawCallCount"_h, _culledDrawCallCountBuffer);
            }

            gotRecreated = true;
        }

        if (gotRecreated)
        {
            // Count drawcalls and triangles
            _numInstances = 0;
            _numTriangles = 0;

            std::vector<Renderer::IndirectDraw>& drawCalls = _drawCalls.Get();
            for (Renderer::IndirectDraw& drawCall : drawCalls)
            {
                _numInstances += drawCall.instanceCount;
                _numTriangles += (drawCall.vertexCount / 3) * drawCall.instanceCount;
            }
        }
    }
    return gotRecreated;
}

void CullingResourcesNonIndexedBase::Clear()
{
    CullingResourcesBase::Clear();
    _drawCalls.Clear();
}

u32 CullingResourcesNonIndexedBase::GetDrawCallsSize()
{
    return static_cast<u32>(_drawCalls.Size());
}

void CullingResourcesNonIndexedBase::Grow(u32 growthSize)
{
    CullingResourcesBase::Grow(growthSize);
    _drawCalls.Grow(growthSize);
}

void CullingResourcesNonIndexedBase::Resize(u32 size)
{
    CullingResourcesBase::Resize(size);
    _drawCalls.Resize(size);
}

void CullingResourcesNonIndexedBase::FitBuffersAfterLoad()
{
    CullingResourcesBase::FitBuffersAfterLoad();
    u32 numDrawCalls = _drawCallsIndex.load();
    _drawCalls.Resize(numDrawCalls);
}

void CullingResourcesNonIndexedBase::SetValidation(bool validation)
{
    _drawCalls.SetValidation(validation);
}