#include "ModelViewWorkResources.h"

#include <Renderer/Renderer.h>

#include <cstring>
#include <bit>

namespace ModelView
{
    ModelViewWorkResources::ModelViewWorkResources(Renderer::Renderer* renderer)
        : _renderer(renderer)
    {
        for (u32 frame = 0; frame < FRAME_COUNT; ++frame)
        {
            _arguments[frame] = Renderer::BufferID::Invalid();
            _chunkQueues[frame] = Renderer::BufferID::Invalid();
            _chunkArguments[frame] = Renderer::BufferID::Invalid();
            _statsBuffers[frame] = Renderer::BufferID::Invalid();
            _statsReadbacks[frame] = Renderer::BufferID::Invalid();
            _visibilityRecords[frame] = Renderer::BufferID::Invalid();
            for (Renderer::BufferID& queue : _queues[frame])
                queue = Renderer::BufferID::Invalid();
        }
        Renderer::BufferDesc desc;
        desc.name = "Model View Indirect Arguments";
        desc.size = sizeof(u32) * MODEL_DISPATCH_ARGUMENT_COUNT * MODEL_RASTER_CLASS_COUNT;
        desc.usage = Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::INDIRECT_ARGUMENT_BUFFER |
                     Renderer::BufferUsage::TRANSFER_SOURCE | Renderer::BufferUsage::TRANSFER_DESTINATION;
        for (u32 frame = 0; frame < FRAME_COUNT; ++frame)
        {
            desc.name = "Model View Indirect Arguments " + std::to_string(frame);
            _arguments[frame] = _renderer->CreateAndFillBuffer(_arguments[frame], desc, [](void* memory, size_t size) {
                std::memset(memory, 0, size);
            });

            desc.name = "Model View Chunk Indirect Arguments " + std::to_string(frame);
            desc.size = sizeof(u32) * MODEL_DISPATCH_ARGUMENT_COUNT;
            _chunkArguments[frame] =
                _renderer->CreateAndFillBuffer(_chunkArguments[frame], desc, [](void* memory, size_t size) {
                    std::memset(memory, 0, size);
                });
            desc.size = sizeof(u32) * MODEL_DISPATCH_ARGUMENT_COUNT * MODEL_RASTER_CLASS_COUNT;
        }

        desc.name = "Model View Work Stats";
        desc.size = sizeof(WorkStats);
        desc.usage = Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::TRANSFER_SOURCE |
                     Renderer::BufferUsage::TRANSFER_DESTINATION;
        for (u32 frame = 0; frame < FRAME_COUNT; ++frame)
        {
            desc.name = "Model View Work Stats " + std::to_string(frame);
            _statsBuffers[frame] = _renderer->CreateAndFillBuffer(_statsBuffers[frame], desc, [](void* memory, size_t size) {
                std::memset(memory, 0, size);
            });
        }

        desc.name = "Model View Work Stats Readback";
        desc.size = sizeof(WorkStats) + sizeof(_argumentSnapshot);
        desc.usage = Renderer::BufferUsage::TRANSFER_DESTINATION;
        desc.cpuAccess = Renderer::BufferCPUAccess::ReadOnly;
        for (u32 frame = 0; frame < FRAME_COUNT; ++frame)
        {
            desc.name = "Model View Work Stats Readback " + std::to_string(frame);
            _statsReadbacks[frame] = _renderer->CreateBuffer(_statsReadbacks[frame], desc);
        }
        EnsureQueueCapacity(1);
    }

    ModelViewWorkResources::~ModelViewWorkResources()
    {
        for (u32 frame = 0; frame < FRAME_COUNT; ++frame)
        {
            for (Renderer::BufferID queue : _queues[frame])
                if (queue != Renderer::BufferID::Invalid())
                    _renderer->QueueDestroyBuffer(queue);
            if (_visibilityRecords[frame] != Renderer::BufferID::Invalid())
                _renderer->QueueDestroyBuffer(_visibilityRecords[frame]);
            if (_chunkQueues[frame] != Renderer::BufferID::Invalid())
                _renderer->QueueDestroyBuffer(_chunkQueues[frame]);
            _renderer->QueueDestroyBuffer(_chunkArguments[frame]);
            _renderer->QueueDestroyBuffer(_arguments[frame]);
            _renderer->QueueDestroyBuffer(_statsBuffers[frame]);
            _renderer->QueueDestroyBuffer(_statsReadbacks[frame]);
        }
    }

    bool ModelViewWorkResources::EnsureQueueCapacity(u32 meshletCount)
    {
        if (meshletCount <= _queueCapacity)
            return false;

        _queueCapacity = std::bit_ceil(meshletCount);
        Renderer::BufferDesc desc;
        desc.size = sizeof(u32) * _queueCapacity;
        desc.usage = Renderer::BufferUsage::STORAGE_BUFFER;
        for (u32 frame = 0; frame < FRAME_COUNT; ++frame)
        {
            desc.name = "Model View Chunk Queue " + std::to_string(frame);
            desc.size = sizeof(MeshletChunk) * _queueCapacity;
            _chunkQueues[frame] = _renderer->CreateBuffer(_chunkQueues[frame], desc);

            desc.name = "Model Visibility Records " + std::to_string(frame);
            desc.size = sizeof(VisibilityRecord) * _queueCapacity;
            _visibilityRecords[frame] = _renderer->CreateBuffer(_visibilityRecords[frame], desc);

            desc.size = sizeof(u32) * _queueCapacity;
            for (u32 rasterClass = 0; rasterClass < MODEL_RASTER_CLASS_COUNT; ++rasterClass)
            {
                desc.name = (rasterClass == 0 ? "Model View One Sided Queue " : "Model View Two Sided Queue ") +
                            std::to_string(frame);
                _queues[frame][rasterClass] = _renderer->CreateBuffer(_queues[frame][rasterClass], desc);
            }
        }
        ++_queueGeneration;
        return true;
    }

    void ModelViewWorkResources::ReadbackStats(u8 frameIndex)
    {
        if (!_hasReadback[frameIndex])
            return;
        const void* memory = _renderer->MapBuffer(_statsReadbacks[frameIndex]);
        if (memory)
        {
            std::memcpy(&_stats, memory, sizeof(_stats));
            std::memcpy(_argumentSnapshot, static_cast<const u8*>(memory) + sizeof(_stats),
                        sizeof(_argumentSnapshot));
        }
        _renderer->UnmapBuffer(_statsReadbacks[frameIndex]);
    }
} // namespace ModelView
