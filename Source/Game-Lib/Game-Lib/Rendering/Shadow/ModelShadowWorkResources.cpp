#include "ModelShadowWorkResources.h"

#include <Renderer/Renderer.h>

#include <algorithm>
#include <bit>
#include <cstring>

namespace ShadowRendering
{
    ModelShadowWorkResources::ModelShadowWorkResources(Renderer::Renderer* renderer) : _renderer(renderer)
    {
        for (u32 frame = 0; frame < MODEL_SHADOW_FRAME_COUNT; ++frame)
        {
            _chunkQueues[frame] = Renderer::BufferID::Invalid();
            _chunkArguments[frame] = Renderer::BufferID::Invalid();
            _records[frame] = Renderer::BufferID::Invalid();
            _arguments[frame] = Renderer::BufferID::Invalid();
            _statsBuffers[frame] = Renderer::BufferID::Invalid();
            _statsReadbacks[frame] = Renderer::BufferID::Invalid();
            for (Renderer::BufferID& queue : _queues[frame])
                queue = Renderer::BufferID::Invalid();
        }

        Renderer::BufferDesc desc;
        desc.usage = Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::INDIRECT_ARGUMENT_BUFFER |
                     Renderer::BufferUsage::TRANSFER_SOURCE | Renderer::BufferUsage::TRANSFER_DESTINATION;
        desc.size = sizeof(u32) * MODEL_SHADOW_DISPATCH_ARGUMENT_COUNT;
        for (u32 frame = 0; frame < MODEL_SHADOW_FRAME_COUNT; ++frame)
        {
            desc.name = "Model Shadow Chunk Arguments " + std::to_string(frame);
            _chunkArguments[frame] = _renderer->CreateAndFillBuffer(
                _chunkArguments[frame], desc, [](void* memory, size_t size) { std::memset(memory, 0, size); });
            desc.name = "Model Shadow Raster Arguments " + std::to_string(frame);
            desc.size = sizeof(u32) * MODEL_SHADOW_DISPATCH_ARGUMENT_COUNT * MODEL_SHADOW_QUEUE_COUNT;
            _arguments[frame] = _renderer->CreateAndFillBuffer(
                _arguments[frame], desc, [](void* memory, size_t size) { std::memset(memory, 0, size); });
            desc.size = sizeof(u32) * MODEL_SHADOW_DISPATCH_ARGUMENT_COUNT;
        }

        desc.usage = Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::TRANSFER_SOURCE |
                     Renderer::BufferUsage::TRANSFER_DESTINATION;
        desc.size = sizeof(ModelShadowStats);
        for (u32 frame = 0; frame < MODEL_SHADOW_FRAME_COUNT; ++frame)
        {
            desc.name = "Model Shadow Stats " + std::to_string(frame);
            _statsBuffers[frame] = _renderer->CreateAndFillBuffer(
                _statsBuffers[frame], desc, [](void* memory, size_t size) { std::memset(memory, 0, size); });
        }

        desc.usage = Renderer::BufferUsage::TRANSFER_DESTINATION;
        desc.cpuAccess = Renderer::BufferCPUAccess::ReadOnly;
        for (u32 frame = 0; frame < MODEL_SHADOW_FRAME_COUNT; ++frame)
        {
            desc.name = "Model Shadow Stats Readback " + std::to_string(frame);
            _statsReadbacks[frame] = _renderer->CreateBuffer(_statsReadbacks[frame], desc);
        }
        EnsureCapacity(1);
    }

    ModelShadowWorkResources::~ModelShadowWorkResources()
    {
        for (u32 frame = 0; frame < MODEL_SHADOW_FRAME_COUNT; ++frame)
        {
            _renderer->QueueDestroyBuffer(_chunkQueues[frame]);
            _renderer->QueueDestroyBuffer(_chunkArguments[frame]);
            for (Renderer::BufferID queue : _queues[frame])
                _renderer->QueueDestroyBuffer(queue);
            _renderer->QueueDestroyBuffer(_records[frame]);
            _renderer->QueueDestroyBuffer(_arguments[frame]);
            _renderer->QueueDestroyBuffer(_statsBuffers[frame]);
            _renderer->QueueDestroyBuffer(_statsReadbacks[frame]);
        }
    }

    bool ModelShadowWorkResources::EnsureCapacity(u32 capacity)
    {
        if (capacity <= _capacity)
            return false;
        _capacity = std::bit_ceil(std::max(capacity, 1u));

        Renderer::BufferDesc desc;
        desc.usage = Renderer::BufferUsage::STORAGE_BUFFER;
        for (u32 frame = 0; frame < MODEL_SHADOW_FRAME_COUNT; ++frame)
        {
            desc.name = "Model Shadow Chunk Queue " + std::to_string(frame);
            desc.size = sizeof(ModelShadowChunk) * _capacity;
            _chunkQueues[frame] = _renderer->CreateBuffer(_chunkQueues[frame], desc);

            desc.name = "Model Shadow Records " + std::to_string(frame);
            desc.size = sizeof(ModelShadowRecord) * _capacity;
            _records[frame] = _renderer->CreateBuffer(_records[frame], desc);

            desc.size = sizeof(u32) * _capacity;
            for (u32 queueIndex = 0; queueIndex < MODEL_SHADOW_QUEUE_COUNT; ++queueIndex)
            {
                desc.name = "Model Shadow Queue " + std::to_string(queueIndex) + " " + std::to_string(frame);
                _queues[frame][queueIndex] = _renderer->CreateBuffer(_queues[frame][queueIndex], desc);
            }
        }
        ++_generation;
        return true;
    }

    void ModelShadowWorkResources::ReadbackStats(u8 frameIndex)
    {
        if (!_hasReadback[frameIndex])
            return;
        const void* memory = _renderer->MapBuffer(_statsReadbacks[frameIndex]);
        if (memory)
            std::memcpy(&_stats, memory, sizeof(_stats));
        _renderer->UnmapBuffer(_statsReadbacks[frameIndex]);
    }
} // namespace ShadowRendering
