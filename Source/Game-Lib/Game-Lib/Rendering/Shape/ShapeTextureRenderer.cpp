#include "ShapeTextureRenderer.h"
#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Rendering/RenderResources.h"
#include "Game-Lib/Util/AssetPath.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Base/Util/StringUtils.h>

#include <Filesystem/PactStorage.h>

#include <Renderer/Renderer.h>
#include <Renderer/RenderGraph.h>
#include <Renderer/Descriptors/RenderPassDesc.h>

#include <algorithm>
#include <bit>
#include <cmath>

ShapeTextureRenderer::ShapeTextureRenderer(Renderer::Renderer* renderer, GameRenderer* gameRenderer)
    : _gameRenderer(gameRenderer)
    , _shapeDescriptorSet(Renderer::DescriptorSetSlot::PER_PASS)
{
    ZoneScoped;

    _renderer = renderer;

    CreatePermanentResources();
}

void ShapeTextureRenderer::CreatePermanentResources()
{
    ZoneScoped;

    CreatePipelines();
    InitDescriptorSets();

    _shapes.SetDebugName("ShapeDrawDatas");
    _shapes.SetUsage(Renderer::BufferUsage::TRANSFER_DESTINATION | Renderer::BufferUsage::STORAGE_BUFFER);
    _shapes.SyncToGPU(_renderer);
    _shapeDescriptorSet.Bind("_shapes", _shapes.GetBuffer());

    _clipRects.SetDebugName("ShapeClipRects");
    _clipRects.SetUsage(Renderer::BufferUsage::TRANSFER_DESTINATION | Renderer::BufferUsage::STORAGE_BUFFER);
    _clipRects.SyncToGPU(_renderer);
    _shapeDescriptorSet.Bind("_shapeClipRects", _clipRects.GetBuffer());

    _pathPoints.SetDebugName("ShapePathPoints");
    _pathPoints.SetUsage(Renderer::BufferUsage::TRANSFER_DESTINATION | Renderer::BufferUsage::STORAGE_BUFFER);
    _pathPoints.SyncToGPU(_renderer);
    _shapeDescriptorSet.Bind("_shapePathPoints", _pathPoints.GetBuffer());

    // Own texture array for DrawImage; deliberately separate from CanvasRenderer's so shape
    // surfaces stay usable for non-UI purposes
    Renderer::TextureArrayDesc textureArrayDesc;
    textureArrayDesc.size = 1024;
    _textures = _renderer->CreateTextureArray(textureArrayDesc);
    _shapeDescriptorSet.Bind("_shapeTextures", _textures);

    // Slot 0: white fallback, so failed loads tint-multiply to a visible solid instead of garbage
    Renderer::DataTextureDesc whiteTextureDesc;
    whiteTextureDesc.width = 1;
    whiteTextureDesc.height = 1;
    whiteTextureDesc.format = Renderer::ImageFormat::R8G8B8A8_UNORM_SRGB;
    whiteTextureDesc.data = new u8[4]{ 255, 255, 255, 255 };
    whiteTextureDesc.debugName = "ShapeTextureFallback";

    size_t arrayIndex = 0;
    _renderer->CreateDataTextureIntoArray(whiteTextureDesc, _textures, arrayIndex);

    Renderer::SamplerDesc samplerDesc;
    samplerDesc.enabled = true;
    samplerDesc.filter = Renderer::SamplerFilter::MIN_MAG_MIP_LINEAR;
    samplerDesc.addressU = Renderer::TextureAddressMode::CLAMP;
    samplerDesc.addressV = Renderer::TextureAddressMode::CLAMP;
    samplerDesc.addressW = Renderer::TextureAddressMode::CLAMP;
    samplerDesc.shaderVisibility = Renderer::ShaderVisibility::PIXEL;

    _sampler = _renderer->CreateSampler(samplerDesc);
    _shapeDescriptorSet.Bind("_shapeSampler"_h, _sampler);
}

size_t ShapeTextureRenderer::AddTexture(Renderer::TextureID textureID)
{
    Renderer::TextureID::type typedID = static_cast<Renderer::TextureID::type>(textureID);
    if (_textureIDToIndex.contains(typedID))
    {
        return _textureIDToIndex[typedID];
    }

    size_t textureIndex = _renderer->AddTextureToArray(textureID, _textures);
    _textureIDToIndex[typedID] = textureIndex;
    return textureIndex;
}

size_t ShapeTextureRenderer::LoadTexture(std::string_view path)
{
    u64 textureNameHash = Util::AssetPath::Hash(path);
    if (_textureNameHashToIndex.contains(textureNameHash))
    {
        return _textureNameHashToIndex[textureNameHash];
    }

    auto* pactStorage = ServiceLocator::GetPactStorage();

    PACT::PactFileHandle fileHandle;
    if (pactStorage->ReadFile(textureNameHash, fileHandle) != PACT::PactReadResult::Success)
        return 0;

    Renderer::DataTextureDesc textureDesc;
    textureDesc.hash = textureNameHash;
    textureDesc.data = reinterpret_cast<const u8*>(fileHandle.GetData());
    textureDesc.size = fileHandle.GetSize();

    size_t textureIndex;
    _renderer->LoadDataTextureIntoArray(textureDesc, _textures, textureIndex);

    _textureNameHashToIndex[textureNameHash] = textureIndex;
    return textureIndex;
}

void ShapeTextureRenderer::CreatePipelines()
{
    Renderer::GraphicsPipelineDesc pipelineDesc;
    pipelineDesc.debugName = "Shape Texture Pipeline";

    // Rasterizer state
    pipelineDesc.states.rasterizerState.cullMode = Renderer::CullMode::NONE;

    // Render targets. Surface textures are created with the swapchain format
    pipelineDesc.states.renderTargetFormats[0] = _renderer->GetSwapChainImageFormat();

    // Shader
    Renderer::VertexShaderDesc vertexShaderDesc;
    vertexShaderDesc.shaderEntry = _gameRenderer->GetShaderEntry("Shape/ShapeTexture.vs"_h, "Shape/ShapeTexture.vs");
    pipelineDesc.shaderStages = Renderer::VertexPipelineStages{ .vertexShader = _renderer->LoadShader(vertexShaderDesc) };

    Renderer::PixelShaderDesc pixelShaderDesc;
    pixelShaderDesc.shaderEntry = _gameRenderer->GetShaderEntry("Shape/ShapeTexture.ps"_h, "Shape/ShapeTexture.ps");
    pipelineDesc.states.pixelShader = _renderer->LoadShader(pixelShaderDesc);

    // Blending: shapes output premultiplied alpha, and the UI compositing that consumes these
    // textures assumes premultiplied content
    pipelineDesc.states.blendState.renderTargets[0].blendEnable = true;
    pipelineDesc.states.blendState.renderTargets[0].srcBlend = Renderer::BlendMode::ONE;
    pipelineDesc.states.blendState.renderTargets[0].destBlend = Renderer::BlendMode::INV_SRC_ALPHA;
    pipelineDesc.states.blendState.renderTargets[0].srcBlendAlpha = Renderer::BlendMode::ONE;
    pipelineDesc.states.blendState.renderTargets[0].destBlendAlpha = Renderer::BlendMode::INV_SRC_ALPHA;

    pipelineDesc.states.primitiveTopology = Renderer::PrimitiveTopology::Triangles;
    _shapePipeline = _renderer->CreatePipeline(pipelineDesc);
}

void ShapeTextureRenderer::InitDescriptorSets()
{
    _shapeDescriptorSet.RegisterPipeline(_renderer, _shapePipeline);
    _shapeDescriptorSet.Init(_renderer);
}

void ShapeTextureRenderer::Update(f32 deltaTime)
{
    ZoneScoped;

    // Coalesce: no matter how many draws touched a surface since last frame, it renders once
    _drawRequests.clear();

    bool anyDirty = false;
    for (Surface& surface : _surfaces)
    {
        anyDirty |= surface.inUse && surface.dirty;
    }

    if (!anyDirty)
        return;

    // Rebuild the GPU shape/clip/path buffers with only the surfaces that redraw this frame.
    // Per-surface clip and path indices are surface-local at record time and remapped to their
    // global buffer positions here
    _shapes.Clear();
    _clipRects.Clear();
    _pathPoints.Clear();
    _clipRects.Add(vec4(-1e6f, -1e6f, 1e6f, 1e6f)); // slot 0: the "no clip" sentinel

    for (u32 surfaceIndex = 0; surfaceIndex < _surfaces.size(); surfaceIndex++)
    {
        Surface& surface = _surfaces[surfaceIndex];
        if (!surface.inUse || !surface.dirty)
            continue;

        surface.dirty = false;

        u32 clipBase = _clipRects.Count();
        for (const vec4& clipRect : surface.clipRects)
        {
            _clipRects.Add(clipRect);
        }

        u32 pathBase = _pathPoints.Count();
        for (const vec2& pathPoint : surface.pathPoints)
        {
            _pathPoints.Add(pathPoint);
        }

        // A non-transparent clear color renders as an implicit backdrop rect beneath the shapes
        bool hasBackdrop = surface.clearColor.a > 0.0f;
        u32 numShapes = static_cast<u32>(surface.shapes.size()) + (hasBackdrop ? 1u : 0u);
        u32 firstShape = 0;
        if (numShapes > 0)
        {
            firstShape = _shapes.AddCount(numShapes);
            u32 writeIndex = firstShape;
            if (hasBackdrop)
            {
                ShapeDrawData backdrop;
                backdrop.packed0.x = static_cast<u32>(ShapeType::Rect);
                backdrop.packed0.y = surface.clearColor.ToRGBA32();
                backdrop.data0 = vec4(0.0f, 0.0f, static_cast<f32>(surface.size.x), static_cast<f32>(surface.size.y));
                _shapes[writeIndex++] = backdrop;
            }
            for (const ShapeDrawData& shape : surface.shapes)
            {
                ShapeDrawData remapped = shape;
                if (remapped.packed1.x > 0)
                {
                    remapped.packed1.x = clipBase + remapped.packed1.x - 1;
                }
                if (remapped.packed0.x == static_cast<u32>(ShapeType::Path))
                {
                    remapped.data1.y += static_cast<f32>(pathBase);
                }
                _shapes[writeIndex++] = remapped;
            }
        }

        // Requests with zero shapes still render, to clear the texture
        _drawRequests.push_back({ surfaceIndex, firstShape, numShapes });
    }

    if (_shapes.SyncToGPU(_renderer))
    {
        _shapeDescriptorSet.Bind("_shapes", _shapes.GetBuffer());
    }
    if (_clipRects.SyncToGPU(_renderer))
    {
        _shapeDescriptorSet.Bind("_shapeClipRects", _clipRects.GetBuffer());
    }
    if (_pathPoints.SyncToGPU(_renderer))
    {
        _shapeDescriptorSet.Bind("_shapePathPoints", _pathPoints.GetBuffer());
    }
}

void ShapeTextureRenderer::AddShapePass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex)
{
    if (_drawRequests.empty())
        return;

    struct Data
    {
    public:
        Renderer::DescriptorSetResource shapeSet;
    };

    renderGraph->AddPass<Data>("ShapeTextures",
        [this](Data& data, Renderer::RenderGraphBuilder& builder) // Setup
        {
            using BufferUsage = Renderer::BufferPassUsage;

            builder.Read(_shapes.GetBuffer(), BufferUsage::GRAPHICS);
            builder.Read(_clipRects.GetBuffer(), BufferUsage::GRAPHICS);
            builder.Read(_pathPoints.GetBuffer(), BufferUsage::GRAPHICS);

            data.shapeSet = builder.Use(_shapeDescriptorSet);

            return true;// Return true from setup to enable this pass, return false to disable it
        },
        [this, frameIndex](Data& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList) // Execute
        {
            GPU_SCOPED_PROFILER_ZONE(commandList, ShapeTextures);

            for (const DrawRequest& request : _drawRequests)
            {
                Surface& surface = _surfaces[request.surfaceID];
                if (!surface.inUse)
                    continue;

                commandList.SetViewport(0, 0, static_cast<f32>(surface.size.x), static_cast<f32>(surface.size.y), 0.0f, 1.0f);
                commandList.SetScissorRect(0, surface.size.x, 0, surface.size.y);

                Renderer::TextureRenderPassDesc renderPassDesc;
                renderPassDesc.renderTargets[0] = surface.texture;
                renderPassDesc.clearRenderTargets[0] = true;

                commandList.PushMarker("Shape Surface: " + surface.name + " (" + std::to_string(request.numShapes) + " shapes)", Color::PastelBlue);
                commandList.BeginRenderPass(renderPassDesc);

                if (request.numShapes > 0)
                {
                    commandList.BeginPipeline(_shapePipeline);

                    vec2 viewportSize(static_cast<f32>(surface.size.x), static_cast<f32>(surface.size.y));
                    commandList.PushConstant(&viewportSize, 0, sizeof(viewportSize));

                    commandList.BindDescriptorSet(data.shapeSet, frameIndex);

                    commandList.Draw(6, request.numShapes, 0, request.firstShape);

                    commandList.EndPipeline(_shapePipeline);
                }

                commandList.EndRenderPass(renderPassDesc);
                commandList.PopMarker();
            }

            _drawRequests.clear();
        }, Renderer::RenderPassFlags::SideEffect);
}

ShapeTextureRenderer::SurfaceID ShapeTextureRenderer::CreateSurface(const uvec2& size, const std::string& name)
{
    SurfaceID surfaceID;
    if (!_freeSurfaceIDs.empty())
    {
        surfaceID = _freeSurfaceIDs.back();
        _freeSurfaceIDs.pop_back();
    }
    else
    {
        surfaceID = static_cast<SurfaceID>(_surfaces.size());
        _surfaces.emplace_back();
    }

    Surface& surface = _surfaces[surfaceID];
    surface.name = name;
    CreateSurfaceTexture(surface, size);
    surface.clearColor = Color(0.0f, 0.0f, 0.0f, 0.0f);
    ResetSurfaceContent(surface);
    surface.dirty = true; // Render once so the texture starts cleared instead of uninitialized
    surface.inUse = true;

    _nameHashToSurfaceID[StringUtils::fnv1a_32(name.c_str(), name.length())] = surfaceID;
    return surfaceID;
}

// Clears everything a redraw replays: retained shapes plus the record-time clip, path and
// transform state. Every surface lifecycle transition must go through this so recycled slots
// cannot inherit a previous owner's state
void ShapeTextureRenderer::ResetSurfaceContent(Surface& surface)
{
    surface.shapes.clear();
    surface.clipRects.clear();
    surface.clipStack.clear();
    surface.pathPoints.clear();
    surface.transformStack.clear();
}

ShapeTextureRenderer::SurfaceID ShapeTextureRenderer::FindSurface(std::string_view name) const
{
    auto it = _nameHashToSurfaceID.find(StringUtils::fnv1a_32(name.data(), name.length()));
    if (it == _nameHashToSurfaceID.end())
        return INVALID_SURFACE_ID;

    return it->second;
}

void ShapeTextureRenderer::CreateSurfaceTexture(Surface& surface, const uvec2& size)
{
    // Defensive bounds; the script bindings validate and error on bad sizes before reaching here
    uvec2 clampedSize = glm::clamp(size, uvec2(1, 1), uvec2(16384, 16384));

    Renderer::DataTextureDesc desc;
    desc.debugName = surface.name;
    desc.format = _renderer->GetSwapChainImageFormat();
    desc.width = clampedSize.x;
    desc.height = clampedSize.y;
    desc.layers = 1;
    desc.mipLevels = 1;
    desc.renderable = true;

    surface.texture = _renderer->CreateDataTexture(desc);
    surface.size = clampedSize;
}

void ShapeTextureRenderer::ResizeSurface(SurfaceID surfaceID, const uvec2& size)
{
    Surface* surface = GetSurface(surfaceID);
    if (surface == nullptr || surface->size == size)
        return;

    // The old texture leaks; see the header note. Consumers must re-fetch GetTextureID
    CreateSurfaceTexture(*surface, size);
    surface->dirty = true;
}

void ShapeTextureRenderer::SetClearColor(SurfaceID surfaceID, Color color)
{
    Surface* surface = GetSurface(surfaceID);
    if (surface == nullptr)
        return;

    surface->clearColor = color;
    surface->dirty = true;
}

void ShapeTextureRenderer::DestroySurface(SurfaceID surfaceID)
{
    Surface* surface = GetSurface(surfaceID);
    if (surface == nullptr)
        return;

    // The texture itself is not destroyed; the renderer has no texture destruction path yet.
    // Slots are recycled so churn is bounded by the number of live surfaces
    _nameHashToSurfaceID.erase(StringUtils::fnv1a_32(surface->name.c_str(), surface->name.length()));
    ResetSurfaceContent(*surface);
    surface->shapes.shrink_to_fit();
    surface->texture = Renderer::TextureID::Invalid();
    surface->dirty = false;
    surface->inUse = false;

    _freeSurfaceIDs.push_back(surfaceID);
}

Renderer::TextureID ShapeTextureRenderer::GetTextureID(SurfaceID surfaceID) const
{
    const Surface* surface = GetSurface(surfaceID);
    return surface != nullptr ? surface->texture : Renderer::TextureID::Invalid();
}

uvec2 ShapeTextureRenderer::GetSurfaceSize(SurfaceID surfaceID) const
{
    const Surface* surface = GetSurface(surfaceID);
    return surface != nullptr ? surface->size : uvec2(0, 0);
}

void ShapeTextureRenderer::ClearSurface(SurfaceID surfaceID)
{
    Surface* surface = GetSurface(surfaceID);
    if (surface == nullptr)
        return;

    ResetSurfaceContent(*surface);
    surface->dirty = true;
}

void ShapeTextureRenderer::PushClipRect(SurfaceID surfaceID, const vec2& min, const vec2& max)
{
    Surface* surface = GetSurface(surfaceID);
    if (surface == nullptr)
        return;

    vec2 clipMin = TransformPoint(*surface, min);
    vec2 clipMax = TransformPoint(*surface, max);
    vec2 orderedMin = glm::min(clipMin, clipMax);
    vec2 orderedMax = glm::max(clipMin, clipMax);

    // Nested clips intersect with the current one
    if (!surface->clipStack.empty() && surface->clipStack.back() > 0)
    {
        const vec4& parent = surface->clipRects[surface->clipStack.back() - 1];
        orderedMin = glm::max(orderedMin, vec2(parent.x, parent.y));
        orderedMax = glm::min(orderedMax, vec2(parent.z, parent.w));
    }

    surface->clipRects.push_back(vec4(orderedMin, orderedMax));
    surface->clipStack.push_back(static_cast<u32>(surface->clipRects.size()));
}

void ShapeTextureRenderer::PopClipRect(SurfaceID surfaceID)
{
    Surface* surface = GetSurface(surfaceID);
    if (surface == nullptr || surface->clipStack.empty())
        return;

    surface->clipStack.pop_back();
}

void ShapeTextureRenderer::PushTransform(SurfaceID surfaceID, const vec2& translation, f32 scale, f32 rotation)
{
    Surface* surface = GetSurface(surfaceID);
    if (surface == nullptr)
        return;

    Transform2D composed;
    composed.translation = TransformPoint(*surface, translation);
    composed.scale = TransformLength(*surface, scale);
    composed.rotation = TransformAngle(*surface, rotation);
    surface->transformStack.push_back(composed);
}

void ShapeTextureRenderer::PopTransform(SurfaceID surfaceID)
{
    Surface* surface = GetSurface(surfaceID);
    if (surface == nullptr || surface->transformStack.empty())
        return;

    surface->transformStack.pop_back();
}

vec2 ShapeTextureRenderer::TransformPoint(const Surface& surface, const vec2& point)
{
    if (surface.transformStack.empty())
        return point;

    const Transform2D& transform = surface.transformStack.back();
    vec2 scaled = point * transform.scale;
    f32 cos = std::cos(transform.rotation);
    f32 sin = std::sin(transform.rotation);
    return transform.translation + vec2(scaled.x * cos - scaled.y * sin, scaled.x * sin + scaled.y * cos);
}

f32 ShapeTextureRenderer::TransformLength(const Surface& surface, f32 length)
{
    return surface.transformStack.empty() ? length : length * surface.transformStack.back().scale;
}

f32 ShapeTextureRenderer::TransformAngle(const Surface& surface, f32 angle)
{
    return surface.transformStack.empty() ? angle : angle + surface.transformStack.back().rotation;
}

void ShapeTextureRenderer::ForceRefresh(SurfaceID surfaceID)
{
    Surface* surface = GetSurface(surfaceID);
    if (surface == nullptr)
        return;

    surface->dirty = true;
}

void ShapeTextureRenderer::DrawLine(SurfaceID surfaceID, const vec2& from, const vec2& to, const StrokeStyle& style)
{
    Surface* surface = GetSurface(surfaceID);
    if (surface == nullptr)
        return;

    vec2 transformedFrom = TransformPoint(*surface, from);
    vec2 transformedTo = TransformPoint(*surface, to);
    f32 scale = TransformLength(*surface, 1.0f);

    ShapeDrawData* shape = AddShape(surfaceID, ShapeType::Line);
    if (shape == nullptr)
        return;

    shape->packed0.y = style.color.ToRGBA32();
    shape->packed0.z = style.endColor.ToRGBA32();
    shape->data0 = vec4(transformedFrom, transformedTo);
    shape->data1 = vec4(style.width * scale, 0.0f, style.useEndColor ? glm::length(transformedTo - transformedFrom) : 0.0f, static_cast<f32>(style.capStyle));
    shape->data2 = vec4(style.dashLength * scale, style.gapLength * scale, style.dashOffset * scale, 0.0f);
}

void ShapeTextureRenderer::DrawPolyline(SurfaceID surfaceID, const vec2* points, u32 numPoints, const StrokeStyle& style)
{
    if (points == nullptr || numPoints < 2)
        return;

    Surface* surface = GetSurface(surfaceID);
    if (surface == nullptr)
        return;

    f32 scale = TransformLength(*surface, 1.0f);
    f32 width = style.width * scale;

    // One Path shape per segment with a tight bounding quad. Each pixel is owned by its nearest
    // segment (the shader tests this segment against its two neighbors), so joins partition
    // exactly: no double-blend dots on translucent strokes and no fat all-covering quads
    u32 pathBase = static_cast<u32>(surface->pathPoints.size());
    f32 totalLength = 0.0f;
    for (u32 i = 0; i < numPoints; i++)
    {
        vec2 transformed = TransformPoint(*surface, points[i]);
        surface->pathPoints.push_back(transformed);
        if (i > 0)
        {
            totalLength += glm::length(transformed - surface->pathPoints[pathBase + i - 1]);
        }
    }

    f32 pad = width * 0.5f + 2.0f;
    f32 distanceAlong = 0.0f;
    for (u32 i = 0; i < numPoints - 1; i++)
    {
        const vec2& a = surface->pathPoints[pathBase + i];
        const vec2& b = surface->pathPoints[pathBase + i + 1];

        ShapeDrawData* shape = AddShape(surfaceID, ShapeType::Path);
        if (shape == nullptr)
            return;

        u32 neighborFlags = (i > 0 ? 1u : 0u) | (i + 2 < numPoints ? 2u : 0u);
        shape->packed0.y = style.color.ToRGBA32();
        shape->packed0.z = style.endColor.ToRGBA32();
        shape->packed1.y = std::bit_cast<u32>(style.useEndColor ? totalLength : 0.0f);
        shape->data0 = vec4(glm::min(a, b) - pad, glm::max(a, b) + pad);
        shape->data1 = vec4(width, static_cast<f32>(pathBase + i), distanceAlong, static_cast<f32>(neighborFlags));
        shape->data2 = vec4(style.dashLength * scale, style.gapLength * scale, style.dashOffset * scale + distanceAlong, 0.0f);

        distanceAlong += glm::length(b - a);
    }
}

void ShapeTextureRenderer::DrawBezier(SurfaceID surfaceID, const vec2& p0, const vec2& c0, const vec2& c1, const vec2& p1, const StrokeStyle& style)
{
    // Flatten the cubic into line segments; the capsule round caps act as joins. Segment count
    // scales with the control polygon length so curves stay smooth at any size
    f32 controlPolygonLength = glm::length(c0 - p0) + glm::length(c1 - c0) + glm::length(p1 - c1);
    u32 numSegments = std::clamp(static_cast<u32>(controlPolygonLength / 8.0f), 8u, 64u);

    vec2 flattened[65];
    flattened[0] = p0;
    for (u32 i = 1; i <= numSegments; i++)
    {
        f32 t = static_cast<f32>(i) / static_cast<f32>(numSegments);
        f32 u = 1.0f - t;
        flattened[i] = (u * u * u) * p0 + (3.0f * u * u * t) * c0 + (3.0f * u * t * t) * c1 + (t * t * t) * p1;
    }

    DrawPolyline(surfaceID, flattened, numSegments + 1, style);
}

// Applies the surface's record-time transform to an axis-aligned rect: the center translates,
// extents scale uniformly, and the transform's rotation folds into the shape's own rotation
static void TransformRect(const vec2& min, const vec2& max, f32 scale, const vec2& transformedCenter, vec2& outMin, vec2& outMax)
{
    vec2 halfExtents = (max - min) * 0.5f * scale;
    outMin = transformedCenter - halfExtents;
    outMax = transformedCenter + halfExtents;
}

void ShapeTextureRenderer::DrawRect(SurfaceID surfaceID, const vec2& min, const vec2& max, Color color, f32 cornerRadius, f32 rotation)
{
    Surface* surface = GetSurface(surfaceID);
    if (surface == nullptr)
        return;

    f32 scale = TransformLength(*surface, 1.0f);
    vec2 rectMin, rectMax;
    TransformRect(min, max, scale, TransformPoint(*surface, (min + max) * 0.5f), rectMin, rectMax);

    ShapeDrawData* shape = AddShape(surfaceID, ShapeType::Rect);
    if (shape == nullptr)
        return;

    shape->packed0.y = color.ToRGBA32();
    shape->data0 = vec4(rectMin, rectMax);
    shape->data1 = vec4(cornerRadius * scale, 0.0f, 0.0f, TransformAngle(*surface, rotation));
}

// Per-corner radii as whole pixels 0-255, packed one byte each: (bottomLeft, bottomRight, topLeft, topRight)
static u32 PackCornerRadii(const vec4& cornerRadii)
{
    u32 bl = static_cast<u32>(std::clamp(cornerRadii.x, 0.0f, 255.0f));
    u32 br = static_cast<u32>(std::clamp(cornerRadii.y, 0.0f, 255.0f));
    u32 tl = static_cast<u32>(std::clamp(cornerRadii.z, 0.0f, 255.0f));
    u32 tr = static_cast<u32>(std::clamp(cornerRadii.w, 0.0f, 255.0f));
    return bl | (br << 8) | (tl << 16) | (tr << 24);
}

void ShapeTextureRenderer::DrawRect(SurfaceID surfaceID, const vec2& min, const vec2& max, Color color, const vec4& cornerRadii, f32 rotation)
{
    Surface* surface = GetSurface(surfaceID);
    if (surface == nullptr)
        return;

    f32 scale = TransformLength(*surface, 1.0f);
    vec2 rectMin, rectMax;
    TransformRect(min, max, scale, TransformPoint(*surface, (min + max) * 0.5f), rectMin, rectMax);

    ShapeDrawData* shape = AddShape(surfaceID, ShapeType::Rect);
    if (shape == nullptr)
        return;

    shape->packed0.y = color.ToRGBA32();
    shape->packed0.w = PackCornerRadii(cornerRadii * scale);
    shape->data0 = vec4(rectMin, rectMax);
    shape->data1 = vec4(0.0f, 0.0f, 0.0f, TransformAngle(*surface, rotation));
}

void ShapeTextureRenderer::DrawRectGradient(SurfaceID surfaceID, const vec2& min, const vec2& max, Color bottomColor, Color topColor, f32 cornerRadius, f32 rotation)
{
    Surface* surface = GetSurface(surfaceID);
    if (surface == nullptr)
        return;

    f32 scale = TransformLength(*surface, 1.0f);
    vec2 rectMin, rectMax;
    TransformRect(min, max, scale, TransformPoint(*surface, (min + max) * 0.5f), rectMin, rectMax);

    ShapeDrawData* shape = AddShape(surfaceID, ShapeType::Rect);
    if (shape == nullptr)
        return;

    shape->packed0.y = bottomColor.ToRGBA32();
    shape->packed0.z = topColor.ToRGBA32();
    shape->data0 = vec4(rectMin, rectMax);
    shape->data1 = vec4(cornerRadius * scale, 0.0f, 1.0f, TransformAngle(*surface, rotation));
}

void ShapeTextureRenderer::DrawRectOutline(SurfaceID surfaceID, const vec2& min, const vec2& max, const StrokeStyle& style, f32 cornerRadius, f32 rotation)
{
    Surface* surface = GetSurface(surfaceID);
    if (surface == nullptr)
        return;

    f32 scale = TransformLength(*surface, 1.0f);
    vec2 rectMin, rectMax;
    TransformRect(min, max, scale, TransformPoint(*surface, (min + max) * 0.5f), rectMin, rectMax);

    ShapeDrawData* shape = AddShape(surfaceID, ShapeType::RectOutline);
    if (shape == nullptr)
        return;

    shape->packed0.y = style.color.ToRGBA32();
    shape->packed0.z = style.endColor.ToRGBA32();
    shape->data0 = vec4(rectMin, rectMax);
    shape->data1 = vec4(cornerRadius * scale, style.width * scale, style.useEndColor ? 1.0f : 0.0f, TransformAngle(*surface, rotation));
    shape->data2 = vec4(style.dashLength * scale, style.gapLength * scale, style.dashOffset * scale, 0.0f);
}

void ShapeTextureRenderer::DrawCircle(SurfaceID surfaceID, const vec2& center, f32 radius, Color color)
{
    Surface* surface = GetSurface(surfaceID);
    if (surface == nullptr)
        return;

    ShapeDrawData* shape = AddShape(surfaceID, ShapeType::Circle);
    if (shape == nullptr)
        return;

    shape->packed0.y = color.ToRGBA32();
    shape->data0 = vec4(TransformPoint(*surface, center), TransformLength(*surface, radius), 0.0f);
}

void ShapeTextureRenderer::DrawCircleGradient(SurfaceID surfaceID, const vec2& center, f32 radius, Color centerColor, Color edgeColor)
{
    Surface* surface = GetSurface(surfaceID);
    if (surface == nullptr)
        return;

    ShapeDrawData* shape = AddShape(surfaceID, ShapeType::Circle);
    if (shape == nullptr)
        return;

    shape->packed0.y = centerColor.ToRGBA32();
    shape->packed0.z = edgeColor.ToRGBA32();
    shape->data0 = vec4(TransformPoint(*surface, center), TransformLength(*surface, radius), 0.0f);
    shape->data1.z = 1.0f;
}

void ShapeTextureRenderer::DrawEllipse(SurfaceID surfaceID, const vec2& center, const vec2& radii, Color color, f32 rotation)
{
    Surface* surface = GetSurface(surfaceID);
    if (surface == nullptr)
        return;

    ShapeDrawData* shape = AddShape(surfaceID, ShapeType::Ellipse);
    if (shape == nullptr)
        return;

    shape->packed0.y = color.ToRGBA32();
    shape->data0 = vec4(TransformPoint(*surface, center), radii * TransformLength(*surface, 1.0f));
    shape->data1 = vec4(0.0f, 0.0f, 0.0f, TransformAngle(*surface, rotation));
}

void ShapeTextureRenderer::DrawEllipseOutline(SurfaceID surfaceID, const vec2& center, const vec2& radiiIn, const StrokeStyle& styleIn, f32 rotation)
{
    Surface* surface = GetSurface(surfaceID);
    if (surface == nullptr)
        return;

    f32 scale = TransformLength(*surface, 1.0f);
    vec2 radii = radiiIn * scale;
    StrokeStyle style = styleIn;
    style.width *= scale;
    style.dashLength *= scale;
    style.gapLength *= scale;
    style.dashOffset *= scale;
    rotation = TransformAngle(*surface, rotation);

    ShapeDrawData* shape = AddShape(surfaceID, ShapeType::EllipseOutline);
    if (shape == nullptr)
        return;

    // Ramanujan's perimeter approximation drives the dash phase; quantize the pattern period to
    // it so the dashes tile seamlessly around the loop
    f32 perimeter = 3.14159265359f * (3.0f * (radii.x + radii.y) - std::sqrt((3.0f * radii.x + radii.y) * (radii.x + 3.0f * radii.y)));
    f32 dashLength = style.dashLength;
    f32 gapLength = style.gapLength;
    f32 period = dashLength + gapLength;
    if (dashLength > 0.0f && period > 0.0f)
    {
        f32 scale = perimeter / (std::max(1.0f, std::round(perimeter / period)) * period);
        dashLength *= scale;
        gapLength *= scale;
    }

    shape->packed0.y = style.color.ToRGBA32();
    shape->data0 = vec4(TransformPoint(*surface, center), radii);
    shape->data1 = vec4(style.width, perimeter, 0.0f, rotation);
    shape->data2 = vec4(dashLength, gapLength, style.dashOffset, 0.0f);
}

void ShapeTextureRenderer::DrawCircleOutline(SurfaceID surfaceID, const vec2& center, f32 radius, const StrokeStyle& style)
{
    DrawArc(surfaceID, center, radius, 0.0f, 6.28318530718f, style);
}

void ShapeTextureRenderer::DrawArc(SurfaceID surfaceID, const vec2& center, f32 radiusIn, f32 startAngle, f32 endAngle, const StrokeStyle& styleIn)
{
    Surface* surface = GetSurface(surfaceID);
    if (surface == nullptr)
        return;

    f32 scale = TransformLength(*surface, 1.0f);
    f32 radius = radiusIn * scale;
    StrokeStyle style = styleIn;
    style.width *= scale;
    style.dashLength *= scale;
    style.gapLength *= scale;
    style.dashOffset *= scale;
    startAngle = TransformAngle(*surface, startAngle);
    endAngle = TransformAngle(*surface, endAngle);

    ShapeDrawData* shape = AddShape(surfaceID, ShapeType::CircleOutline);
    if (shape == nullptr)
        return;

    // On a full circle the dash pattern wraps at startAngle; fit an integer number of periods
    // around the circumference so it tiles seamlessly instead of leaving one irregular dash
    f32 dashLength = style.dashLength;
    f32 gapLength = style.gapLength;
    f32 period = dashLength + gapLength;
    constexpr f32 twoPi = 6.28318530718f;
    if (dashLength > 0.0f && period > 0.0f && endAngle - startAngle >= twoPi - 1e-3f)
    {
        f32 circumference = twoPi * radius;
        f32 scale = circumference / (std::max(1.0f, std::round(circumference / period)) * period);
        dashLength *= scale;
        gapLength *= scale;
    }

    shape->packed0.y = style.color.ToRGBA32();
    shape->packed0.z = style.endColor.ToRGBA32();
    shape->data0 = vec4(TransformPoint(*surface, center), radius, style.width);
    shape->data1 = vec4(startAngle, endAngle, style.useEndColor ? 1.0f : 0.0f, 0.0f);
    shape->data2 = vec4(dashLength, gapLength, style.dashOffset, 0.0f);
}

void ShapeTextureRenderer::DrawPie(SurfaceID surfaceID, const vec2& center, f32 radius, f32 startAngle, f32 endAngle, Color color)
{
    Surface* surface = GetSurface(surfaceID);
    if (surface == nullptr)
        return;

    ShapeDrawData* shape = AddShape(surfaceID, ShapeType::Pie);
    if (shape == nullptr)
        return;

    shape->packed0.y = color.ToRGBA32();
    shape->data0 = vec4(TransformPoint(*surface, center), TransformLength(*surface, radius), 0.0f);
    shape->data1 = vec4(TransformAngle(*surface, startAngle), TransformAngle(*surface, endAngle), 0.0f, 0.0f);
}

void ShapeTextureRenderer::DrawPieGradient(SurfaceID surfaceID, const vec2& center, f32 radius, f32 startAngle, f32 endAngle, Color startColor, Color endColor)
{
    Surface* surface = GetSurface(surfaceID);
    if (surface == nullptr)
        return;

    ShapeDrawData* shape = AddShape(surfaceID, ShapeType::Pie);
    if (shape == nullptr)
        return;

    shape->packed0.y = startColor.ToRGBA32();
    shape->packed0.z = endColor.ToRGBA32();
    shape->data0 = vec4(TransformPoint(*surface, center), TransformLength(*surface, radius), 0.0f);
    shape->data1 = vec4(TransformAngle(*surface, startAngle), TransformAngle(*surface, endAngle), 1.0f, 0.0f);
}

void ShapeTextureRenderer::DrawPolygon(SurfaceID surfaceID, const vec2* points, u32 numPoints, Color color)
{
    TriangulatePolygon(surfaceID, points, numPoints, nullptr, color);
}

// Bridges each hole into the outer boundary (nearest non-crossing vertex pair), then ear-clips
// the merged polygon. Bridge edges are marked non-hull so only real boundaries antialias
void ShapeTextureRenderer::DrawPolygonWithHoles(SurfaceID surfaceID, const vec2* outerPoints, u32 numOuterPoints, const std::vector<std::vector<vec2>>& holes, Color color)
{
    if (outerPoints == nullptr || numOuterPoints < 3)
        return;

    std::vector<vec2> merged(outerPoints, outerPoints + numOuterPoints);
    std::vector<bool> edgeIsHull(numOuterPoints, true);

    auto signedArea = [](const vec2* areaPoints, size_t count)
    {
        f32 area = 0.0f;
        for (size_t i = 0; i < count; i++)
        {
            const vec2& a = areaPoints[i];
            const vec2& b = areaPoints[(i + 1) % count];
            area += a.x * b.y - a.y * b.x;
        }
        return area;
    };
    f32 outerWinding = signedArea(outerPoints, numOuterPoints);

    auto segmentsIntersect = [](const vec2& a1, const vec2& a2, const vec2& b1, const vec2& b2)
    {
        auto orientation = [](const vec2& p, const vec2& q, const vec2& r)
        {
            f32 cross = (q.x - p.x) * (r.y - p.y) - (q.y - p.y) * (r.x - p.x);
            return (cross > 1e-6f) ? 1 : (cross < -1e-6f ? -1 : 0);
        };
        return orientation(a1, a2, b1) != orientation(a1, a2, b2) && orientation(b1, b2, a1) != orientation(b1, b2, a2);
    };

    for (const std::vector<vec2>& hole : holes)
    {
        if (hole.size() < 3)
            continue;

        // Try hole/outer vertex pairs from nearest to farthest until a bridge crosses nothing
        struct Candidate
        {
        public:
            f32 distanceSq;
            u32 holeIndex;
            u32 mergedIndex;
        };
        std::vector<Candidate> candidates;
        for (u32 holeIndex = 0; holeIndex < hole.size(); holeIndex++)
        {
            for (u32 mergedIndex = 0; mergedIndex < merged.size(); mergedIndex++)
            {
                vec2 delta = hole[holeIndex] - merged[mergedIndex];
                candidates.push_back({ delta.x * delta.x + delta.y * delta.y, holeIndex, static_cast<u32>(mergedIndex) });
            }
        }
        std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) { return a.distanceSq < b.distanceSq; });

        auto bridgeIsClear = [&](const vec2& from, const vec2& to)
        {
            for (size_t i = 0; i < merged.size(); i++)
            {
                const vec2& a = merged[i];
                const vec2& b = merged[(i + 1) % merged.size()];
                if (a == from || a == to || b == from || b == to)
                    continue;
                if (segmentsIntersect(from, to, a, b))
                    return false;
            }
            for (size_t i = 0; i < hole.size(); i++)
            {
                const vec2& a = hole[i];
                const vec2& b = hole[(i + 1) % hole.size()];
                if (a == from || a == to || b == from || b == to)
                    continue;
                if (segmentsIntersect(from, to, a, b))
                    return false;
            }
            return true;
        };

        for (const Candidate& candidate : candidates)
        {
            if (!bridgeIsClear(hole[candidate.holeIndex], merged[candidate.mergedIndex]))
                continue;

            // Splice: ..., M, H, H+1, ..., H-1, H, M, ... — the two M<->H connections are bridges.
            // The hole is walked in reverse so its winding opposes the outer boundary's
            std::vector<vec2> splicedPoints;
            std::vector<bool> splicedHull;
            for (u32 i = 0; i <= candidate.mergedIndex; i++)
            {
                splicedPoints.push_back(merged[i]);
                splicedHull.push_back(edgeIsHull[i]);
            }
            splicedHull.back() = false; // M -> H bridge

            // Walk the hole so its winding opposes the outer boundary's
            bool reverseHole = (signedArea(hole.data(), hole.size()) > 0.0f) == (outerWinding > 0.0f);
            for (u32 i = 0; i <= hole.size(); i++)
            {
                u32 step = i % hole.size();
                u32 holeIndex = reverseHole ? (candidate.holeIndex + static_cast<u32>(hole.size()) - step) % hole.size() : (candidate.holeIndex + step) % hole.size();
                splicedPoints.push_back(hole[holeIndex]);
                splicedHull.push_back(true);
            }
            splicedHull.back() = false; // H -> M bridge

            for (u32 i = candidate.mergedIndex; i < merged.size(); i++)
            {
                splicedPoints.push_back(merged[i]);
                splicedHull.push_back(edgeIsHull[i]);
            }

            merged = std::move(splicedPoints);
            edgeIsHull = std::move(splicedHull);
            break;
        }
        // No clear bridge found (degenerate input): the hole is skipped
    }

    std::vector<u8> hullFlags(edgeIsHull.size());
    for (size_t i = 0; i < edgeIsHull.size(); i++)
    {
        hullFlags[i] = edgeIsHull[i] ? 1 : 0;
    }
    TriangulatePolygon(surfaceID, merged.data(), static_cast<u32>(merged.size()), hullFlags.data(), color);
}

void ShapeTextureRenderer::TriangulatePolygon(SurfaceID surfaceID, const vec2* pointsIn, u32 numPoints, const u8* edgeIsHullInput, Color color)
{
    if (pointsIn == nullptr || numPoints < 3)
        return;

    Surface* surface = GetSurface(surfaceID);
    if (surface == nullptr)
        return;

    std::vector<vec2> transformedPoints(numPoints);
    for (u32 i = 0; i < numPoints; i++)
    {
        transformedPoints[i] = TransformPoint(*surface, pointsIn[i]);
    }
    const vec2* points = transformedPoints.data();

    u32 packedColor = color.ToRGBA32();
    auto cross2 = [](const vec2& a, const vec2& b) { return a.x * b.y - a.y * b.x; };

    // hull flags: which triangle edges (a->b, b->c, c->a) lie on the polygon boundary. Hull edges
    // get antialiasing; interior (diagonal) edges get a hard analytic cut so adjacent triangles
    // partition the polygon exactly — no seams, even for translucent fills. The shader assumes
    // CCW winding, so reorder here and remap the flags when swapping
    auto emitTriangle = [&](vec2 a, vec2 b, vec2 c, bool abIsHull, bool bcIsHull, bool caIsHull)
    {
        if (cross2(b - a, c - a) < 0.0f)
        {
            std::swap(b, c); // edges become a->c(=ca), c->b(=bc), b->a(=ab)
            std::swap(abIsHull, caIsHull);
        }

        ShapeDrawData* shape = AddShape(surfaceID, ShapeType::Triangle);
        if (shape == nullptr)
            return;

        shape->packed0.y = packedColor;
        shape->packed0.w = (abIsHull ? 1u : 0u) | (bcIsHull ? 2u : 0u) | (caIsHull ? 4u : 0u);
        shape->data0 = vec4(a, b);
        shape->data1 = vec4(c, 0.0f, 0.0f);
    };
    auto pointInTriangle = [&](const vec2& p, const vec2& a, const vec2& b, const vec2& c)
    {
        f32 d0 = cross2(b - a, p - a);
        f32 d1 = cross2(c - b, p - b);
        f32 d2 = cross2(a - c, p - c);
        bool hasNegative = (d0 < 0.0f) || (d1 < 0.0f) || (d2 < 0.0f);
        bool hasPositive = (d0 > 0.0f) || (d1 > 0.0f) || (d2 > 0.0f);
        return !(hasNegative && hasPositive);
    };

    // Ear clipping (O(n^2)); fine for the polygon sizes UI shapes use. edgeIsHull[i] tracks
    // whether the edge indices[i] -> indices[i+1] is an original polygon edge or a clip diagonal
    std::vector<u32> indices(numPoints);
    std::vector<bool> edgeIsHull(numPoints, true);
    if (edgeIsHullInput != nullptr)
    {
        for (u32 i = 0; i < numPoints; i++)
        {
            edgeIsHull[i] = edgeIsHullInput[i] != 0;
        }
    }
    for (u32 i = 0; i < numPoints; i++)
    {
        indices[i] = i;
    }

    f32 signedArea = 0.0f;
    for (u32 i = 0; i < numPoints; i++)
    {
        signedArea += cross2(points[i], points[(i + 1) % numPoints]);
    }
    f32 windingSign = signedArea >= 0.0f ? 1.0f : -1.0f;

    while (indices.size() > 3)
    {
        bool clippedEar = false;
        for (size_t i = 0; i < indices.size(); i++)
        {
            size_t prev = (i + indices.size() - 1) % indices.size();
            size_t next = (i + 1) % indices.size();
            const vec2& a = points[indices[prev]];
            const vec2& b = points[indices[i]];
            const vec2& c = points[indices[next]];

            if (cross2(b - a, c - b) * windingSign <= 0.0f) // reflex corner, not an ear
                continue;

            bool containsOtherPoint = false;
            for (size_t j = 0; j < indices.size(); j++)
            {
                if (j == i || j == prev || j == next)
                    continue;

                if (pointInTriangle(points[indices[j]], a, b, c))
                {
                    containsOtherPoint = true;
                    break;
                }
            }
            if (containsOtherPoint)
                continue;

            emitTriangle(a, b, c, edgeIsHull[prev], edgeIsHull[i], false);
            // The diagonal a->c replaces both clipped edges as the remaining polygon's boundary
            edgeIsHull[prev] = false;
            indices.erase(indices.begin() + i);
            edgeIsHull.erase(edgeIsHull.begin() + i);
            clippedEar = true;
            break;
        }

        if (!clippedEar) // degenerate input (self-intersecting or collinear); emit what remains as a fan
            break;
    }

    if (indices.size() == 3)
    {
        emitTriangle(points[indices[0]], points[indices[1]], points[indices[2]], edgeIsHull[0], edgeIsHull[1], edgeIsHull[2]);
    }
    else
    {
        for (size_t i = 1; i + 1 < indices.size(); i++)
        {
            emitTriangle(points[indices[0]], points[indices[i]], points[indices[i + 1]], true, true, true);
        }
    }
}

void ShapeTextureRenderer::DrawImage(SurfaceID surfaceID, const vec2& min, const vec2& max, u32 textureIndex, Color tint, const vec2& uvMin, const vec2& uvMax, f32 rotation)
{
    Surface* surface = GetSurface(surfaceID);
    if (surface == nullptr)
        return;

    vec2 rectMin, rectMax;
    TransformRect(min, max, TransformLength(*surface, 1.0f), TransformPoint(*surface, (min + max) * 0.5f), rectMin, rectMax);

    ShapeDrawData* shape = AddShape(surfaceID, ShapeType::Image);
    if (shape == nullptr)
        return;

    shape->packed0.y = tint.ToRGBA32();
    shape->packed0.z = textureIndex;
    shape->data0 = vec4(rectMin, rectMax);
    shape->data1 = vec4(uvMin, uvMax);
    shape->data2.x = TransformAngle(*surface, rotation);
}

void ShapeTextureRenderer::DrawRectShadow(SurfaceID surfaceID, const vec2& min, const vec2& max, f32 cornerRadius, f32 blurRadius, Color color, const vec2& offset, f32 rotation)
{
    Surface* surface = GetSurface(surfaceID);
    if (surface == nullptr)
        return;

    f32 scale = TransformLength(*surface, 1.0f);
    vec2 rectMin, rectMax;
    TransformRect(min + offset, max + offset, scale, TransformPoint(*surface, (min + max) * 0.5f + offset), rectMin, rectMax);

    ShapeDrawData* shape = AddShape(surfaceID, ShapeType::RectShadow);
    if (shape == nullptr)
        return;

    shape->packed0.y = color.ToRGBA32();
    shape->data0 = vec4(rectMin, rectMax);
    shape->data1 = vec4(cornerRadius * scale, std::max(blurRadius * scale, 0.5f), 0.0f, TransformAngle(*surface, rotation));
}

void ShapeTextureRenderer::DrawGrid(SurfaceID surfaceID, const vec2& origin, const vec2& spacing, f32 lineWidth, Color color, u32 majorEvery, f32 majorLineWidth, Color majorColor)
{
    Surface* surface = GetSurface(surfaceID);
    if (surface == nullptr)
        return;

    // The transform's rotation is ignored for grids; translation and scale apply
    f32 scale = TransformLength(*surface, 1.0f);

    ShapeDrawData* shape = AddShape(surfaceID, ShapeType::Grid);
    if (shape == nullptr)
        return;

    shape->packed0.y = color.ToRGBA32();
    shape->packed0.z = majorColor.ToRGBA32();
    shape->data0 = vec4(TransformPoint(*surface, origin), spacing * scale);
    shape->data1 = vec4(lineWidth * scale, static_cast<f32>(majorEvery), majorLineWidth * scale, 0.0f);
}

void ShapeTextureRenderer::DrawCheckerboard(SurfaceID surfaceID, const vec2& origin, const vec2& cellSize, Color colorA, Color colorB)
{
    Surface* surface = GetSurface(surfaceID);
    if (surface == nullptr)
        return;

    // The transform's rotation is ignored for checkerboards; translation and scale apply
    ShapeDrawData* shape = AddShape(surfaceID, ShapeType::Checkerboard);
    if (shape == nullptr)
        return;

    shape->packed0.y = colorA.ToRGBA32();
    shape->packed0.z = colorB.ToRGBA32();
    shape->data0 = vec4(TransformPoint(*surface, origin), cellSize * TransformLength(*surface, 1.0f));
}

ShapeTextureRenderer::Surface* ShapeTextureRenderer::GetSurface(SurfaceID surfaceID)
{
    if (surfaceID >= _surfaces.size() || !_surfaces[surfaceID].inUse)
        return nullptr;

    return &_surfaces[surfaceID];
}

const ShapeTextureRenderer::Surface* ShapeTextureRenderer::GetSurface(SurfaceID surfaceID) const
{
    if (surfaceID >= _surfaces.size() || !_surfaces[surfaceID].inUse)
        return nullptr;

    return &_surfaces[surfaceID];
}

ShapeTextureRenderer::ShapeDrawData* ShapeTextureRenderer::AddShape(SurfaceID surfaceID, ShapeType type)
{
    Surface* surface = GetSurface(surfaceID);
    if (surface == nullptr)
        return nullptr;

    surface->dirty = true;

    ShapeDrawData& shape = surface->shapes.emplace_back();
    shape.packed0.x = static_cast<u32>(type);
    shape.packed1.x = surface->clipStack.empty() ? 0 : surface->clipStack.back();
    return &shape;
}
