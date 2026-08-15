#include "ModelTransparentWorkResources.h"

#include <Base/Util/DebugHandler.h>
#include <Renderer/Renderer.h>

#include <algorithm>
#include <bit>
#include <cstring>

namespace ModelView
{
    ModelTransparentWorkResources::ModelTransparentWorkResources(Renderer::Renderer* renderer) : _renderer(renderer)
    {
        for (u32 frame = 0; frame < MODEL_TRANSPARENT_FRAME_COUNT; ++frame)
        {
            _chunkQueues[frame] = Renderer::BufferID::Invalid();
            _chunkArguments[frame] = Renderer::BufferID::Invalid();
            _visibilityRecords[frame] = Renderer::BufferID::Invalid();
            _arguments[frame] = Renderer::BufferID::Invalid();
            _statsBuffers[frame] = Renderer::BufferID::Invalid();
            _statsReadbacks[frame] = Renderer::BufferID::Invalid();
        }

        Renderer::BufferDesc desc;
        desc.usage = Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::INDIRECT_ARGUMENT_BUFFER |
                     Renderer::BufferUsage::TRANSFER_SOURCE | Renderer::BufferUsage::TRANSFER_DESTINATION;
        for (u32 frame = 0; frame < MODEL_TRANSPARENT_FRAME_COUNT; ++frame)
        {
            desc.name = "Model Transparent Chunk Arguments " + std::to_string(frame);
            desc.size = sizeof(u32) * MODEL_TRANSPARENT_DISPATCH_ARGUMENT_COUNT;
            _chunkArguments[frame] = _renderer->CreateAndFillBuffer(_chunkArguments[frame], desc,
                [](void* memory, size_t size) { std::memset(memory, 0, size); });

            desc.name = "Model Transparent Arguments " + std::to_string(frame);
            desc.size = sizeof(u32) * MODEL_TRANSPARENT_DISPATCH_ARGUMENT_COUNT * MODEL_TRANSPARENT_BIN_COUNT;
            _arguments[frame] = _renderer->CreateAndFillBuffer(_arguments[frame], desc,
                [](void* memory, size_t size) { std::memset(memory, 0, size); });
        }

        desc.usage = Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::TRANSFER_SOURCE |
                     Renderer::BufferUsage::TRANSFER_DESTINATION;
        desc.size = sizeof(TransparentWorkStats);
        for (u32 frame = 0; frame < MODEL_TRANSPARENT_FRAME_COUNT; ++frame)
        {
            desc.name = "Model Transparent Stats " + std::to_string(frame);
            _statsBuffers[frame] = _renderer->CreateAndFillBuffer(_statsBuffers[frame], desc,
                [](void* memory, size_t size) { std::memset(memory, 0, size); });
        }

        desc.usage = Renderer::BufferUsage::TRANSFER_DESTINATION;
        desc.cpuAccess = Renderer::BufferCPUAccess::ReadOnly;
        for (u32 frame = 0; frame < MODEL_TRANSPARENT_FRAME_COUNT; ++frame)
        {
            desc.name = "Model Transparent Stats Readback " + std::to_string(frame);
            _statsReadbacks[frame] = _renderer->CreateBuffer(_statsReadbacks[frame], desc);
        }
        EnsureCapacity(1);
    }

    ModelTransparentWorkResources::~ModelTransparentWorkResources()
    {
        for (u32 frame = 0; frame < MODEL_TRANSPARENT_FRAME_COUNT; ++frame)
        {
            if (_chunkQueues[frame] != Renderer::BufferID::Invalid())
                _renderer->QueueDestroyBuffer(_chunkQueues[frame]);
            if (_visibilityRecords[frame] != Renderer::BufferID::Invalid())
                _renderer->QueueDestroyBuffer(_visibilityRecords[frame]);
            _renderer->QueueDestroyBuffer(_chunkArguments[frame]);
            _renderer->QueueDestroyBuffer(_arguments[frame]);
            _renderer->QueueDestroyBuffer(_statsBuffers[frame]);
            _renderer->QueueDestroyBuffer(_statsReadbacks[frame]);
        }
    }

    bool ModelTransparentWorkResources::EnsureCapacity(u32 sceneMeshletCapacity)
    {
        const u32 chunkCapacity = std::bit_ceil(std::clamp(sceneMeshletCapacity, 1u, 262144u));
        const u32 binCapacity = std::bit_ceil(std::clamp(sceneMeshletCapacity / 8u, 1024u, 131072u));
        if (chunkCapacity <= _chunkCapacity && binCapacity <= _binCapacity)
            return false;

        _chunkCapacity = std::max(_chunkCapacity, chunkCapacity);
        _binCapacity = std::max(_binCapacity, binCapacity);
        Renderer::BufferDesc desc;
        desc.usage = Renderer::BufferUsage::STORAGE_BUFFER;
        for (u32 frame = 0; frame < MODEL_TRANSPARENT_FRAME_COUNT; ++frame)
        {
            desc.name = "Model Transparent Chunk Queue " + std::to_string(frame);
            desc.size = sizeof(TransparentMeshletChunk) * _chunkCapacity;
            _chunkQueues[frame] = _renderer->CreateBuffer(_chunkQueues[frame], desc);

            desc.name = "Model Transparent Visibility Records " + std::to_string(frame);
            desc.size = sizeof(VisibilityRecord) * _binCapacity * MODEL_TRANSPARENT_BIN_COUNT;
            _visibilityRecords[frame] = _renderer->CreateBuffer(_visibilityRecords[frame], desc);
        }
        ++_generation;
        return true;
    }

    void ModelTransparentWorkResources::ReadbackStats(u8 frameIndex)
    {
        if (!_hasReadback[frameIndex])
            return;
        const void* memory = _renderer->MapBuffer(_statsReadbacks[frameIndex]);
        if (memory)
            std::memcpy(&_stats, memory, sizeof(_stats));
        _renderer->UnmapBuffer(_statsReadbacks[frameIndex]);
    }
} // namespace ModelView
