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

    // DrawCalls
    _drawCalls.SetDebugName(params.bufferNamePrefix + "DrawCallBuffer");
    _drawCalls.SetUsage(Renderer::BufferUsage::INDIRECT_ARGUMENT_BUFFER | Renderer::BufferUsage::STORAGE_BUFFER);

    // Create DrawCountBuffer
    {
        Renderer::BufferDesc desc;
        desc.name = _bufferNamePrefix + "DrawCountBuffer";
        desc.size = sizeof(u32) * Renderer::Settings::MAX_VIEWS; // One per view
        desc.usage = Renderer::BufferUsage::INDIRECT_ARGUMENT_BUFFER | Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION | Renderer::BufferUsage::TRANSFER_SOURCE;
        _drawCountBuffer = _renderer->CreateBuffer(_drawCountBuffer, desc);

        _occluderFillDescriptorSet.Bind("_drawCount"_h, _drawCountBuffer);
        _cullingDescriptorSet.Bind("_drawCount"_h, _drawCountBuffer);

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
}

void CullingResourcesBase::Update(f32 deltaTime, bool cullingEnabled)
{
    // Read back from the culling counters
    for (u32 i = 0; i < Renderer::Settings::MAX_VIEWS; i++)
    {
        _numSurvivingDrawCalls[i] = _numDrawCalls;
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
                _numSurvivingOccluderDrawCalls = *count;
            }
            _renderer->UnmapBuffer(_occluderDrawCountReadBackBuffer);
        }

        {
            u32* count = static_cast<u32*>(_renderer->MapBuffer(_drawCountReadBackBuffer));
            if (count != nullptr)
            {
                for (u32 i = 0; i < Renderer::Settings::MAX_VIEWS; i++)
                {
                    _numSurvivingDrawCalls[i] = count[i];
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

            // (Re)create Culled DrawCall Bitmask buffer
            if (_enableTwoStepCulling)
            {
                Renderer::BufferDesc desc;
                desc.size = RenderUtils::CalcCullingBitmaskSize(_drawCalls.Size());
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

            // Count drawcalls and triangles
            _numDrawCalls = 0;
            _numTriangles = 0;

            std::vector<Renderer::IndexedIndirectDraw>& drawCalls = _drawCalls.Get();
            for (Renderer::IndexedIndirectDraw& drawCall : drawCalls)
            {
                _numDrawCalls += drawCall.instanceCount;
                _numTriangles += (drawCall.indexCount / 3) * drawCall.instanceCount;
            }

            gotRecreated = true;
        }
    }
    return gotRecreated;
}

void CullingResourcesBase::Clear()
{
    _drawCalls.Clear();
    _drawCallsIndex.store(0);
}

void CullingResourcesBase::Grow(u32 growthSize)
{
    _drawCalls.Grow(growthSize);
}

void CullingResourcesBase::FitBuffersAfterLoad()
{
    u32 numDrawCalls = _drawCallsIndex.load();
    _drawCalls.Resize(numDrawCalls);
}

void CullingResourcesBase::SetValidation(bool validation)
{
    _drawCalls.SetValidation(validation);
}