#include "MaterialResolveResources.h"

#include <Renderer/Renderer.h>

#include <bit>
#include <cstring>

namespace MaterialRendering
{
    MaterialResolveResources::MaterialResolveResources(Renderer::Renderer* renderer, const uvec2& dimensions,
                                                       Renderer::ImageDimensionType dimensionType)
        : _renderer(renderer)
    {
        Renderer::ImageDesc image;
        image.debugName = "Material IDs";
        image.dimensions = dimensionType == Renderer::ImageDimensionType::DIMENSION_ABSOLUTE ? vec2(dimensions) : vec2(1.0f);
        image.dimensionType = dimensionType;
        image.format = Renderer::ImageFormat::R16_UINT;
        image.clearUInts = uvec4(0xFFFFu);
        _materialIDs = renderer->CreateImage(image);

        for (u32 frame = 0; frame < _counters.Num; ++frame)
        {
            Renderer::BufferDesc buffer;
            buffer.name = "Material Classification Counters " + std::to_string(frame);
            buffer.size = sizeof(MaterialClassificationStats);
            buffer.usage = Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::TRANSFER_SOURCE |
                           Renderer::BufferUsage::TRANSFER_DESTINATION;
            _counters.Get(frame) = renderer->CreateAndFillBuffer(_counters.Get(frame), buffer,
                [](void* memory, size_t size) {
                std::memset(memory, 0, size);
            });

            buffer.name = "Material Indirect Arguments " + std::to_string(frame);
            buffer.size = sizeof(u32) * FileFormat::Material::ABI::EXECUTION_GROUP_COUNT * MATERIAL_DISPATCH_ARGUMENT_COUNT;
            buffer.usage = Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::INDIRECT_ARGUMENT_BUFFER |
                           Renderer::BufferUsage::TRANSFER_DESTINATION;
            _arguments.Get(frame) = renderer->CreateAndFillBuffer(_arguments.Get(frame), buffer,
                [](void* memory, size_t size) {
                std::memset(memory, 0, size);
            });

            buffer.name = "Material Classification Readback " + std::to_string(frame);
            buffer.size = sizeof(MaterialClassificationStats);
            buffer.usage = Renderer::BufferUsage::TRANSFER_DESTINATION;
            buffer.cpuAccess = Renderer::BufferCPUAccess::ReadOnly;
            _readbacks.Get(frame) = renderer->CreateBuffer(_readbacks.Get(frame), buffer);
        }
        EnsureTileCapacity(1);
    }

    MaterialResolveResources::~MaterialResolveResources()
    {
        for (u32 frame = 0; frame < _tileQueues.Num; ++frame)
        {
            _renderer->QueueDestroyBuffer(_tileQueues.Get(frame));
            _renderer->QueueDestroyBuffer(_counters.Get(frame));
            _renderer->QueueDestroyBuffer(_arguments.Get(frame));
            _renderer->QueueDestroyBuffer(_readbacks.Get(frame));
        }
    }

    bool MaterialResolveResources::EnsureTileCapacity(u32 tileCount)
    {
        if (tileCount <= _tileCapacity)
            return false;
        _tileCapacity = std::bit_ceil(tileCount);
        Renderer::BufferDesc buffer;
        buffer.size = sizeof(u32) * _tileCapacity * FileFormat::Material::ABI::EXECUTION_GROUP_COUNT;
        buffer.usage = Renderer::BufferUsage::STORAGE_BUFFER;
        for (u32 frame = 0; frame < _tileQueues.Num; ++frame)
        {
            buffer.name = "Material Tile Queue " + std::to_string(frame);
            _tileQueues.Get(frame) = _renderer->CreateBuffer(_tileQueues.Get(frame), buffer);
        }
        ++_generation;
        return true;
    }

    void MaterialResolveResources::ReadbackStats(u8 frameIndex)
    {
        if (!_hasReadback.Get(frameIndex))
            return;
        const Renderer::BufferID readback = _readbacks.Get(frameIndex);
        const void* memory = _renderer->MapBuffer(readback);
        if (memory)
            std::memcpy(&_stats, memory, sizeof(_stats));
        _renderer->UnmapBuffer(readback);
    }
} // namespace MaterialRendering
