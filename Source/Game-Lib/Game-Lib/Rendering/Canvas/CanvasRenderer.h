#pragma once
#include "Game-Lib/ECS/Components/UI/Widget.h"

#include <Base/Types.h>

#include <Renderer/Descriptors/ImageDesc.h>
#include <Renderer/Descriptors/TextureDesc.h>
#include <Renderer/DescriptorSet.h>
#include <Renderer/Font.h>
#include <Renderer/GPUVector.h>
#include <Renderer/RenderStates.h>

#include <robinhood/robinhood.h>

#include <entt/fwd.hpp>

#include <vector>

namespace Renderer
{
    class RenderGraph;
    class Renderer;
}
namespace ECS
{
    namespace Components
    {
        struct Transform2D;
        namespace UI
        {
            struct Panel;
            struct PanelTemplate;
            struct Text;
            struct TextTemplate;
            struct Clipper;
            struct BoundingRect;
            enum class WidgetType : u8;
        }
    }
}
struct RenderResources;
class Window;
class DebugRenderer;
class GameRenderer;

class CanvasRenderer
{
public:
    CanvasRenderer(Renderer::Renderer* renderer, GameRenderer* gameRenderer, DebugRenderer* debugRenderer);
    void Clear();

    void Update(f32 deltaTime);

    //CanvasTextureID AddTexture(Renderer::TextureID textureID);

    u32 ReserveWorldTransform();
    void ReleaseWorldTransform(u32 index);
    void UpdateWorldTransform(u32 index, const vec3& position);

    // Per-widget transform slot. The vertex shader composes the parent chain (_widgetLocalMatrices +
    // _widgetParentSlots) to screen/NDC; the canvas root's local matrix folds in pixel->NDC.
    u32 ReserveMatrixSlot();
    void ReleaseMatrixSlot(u32 index);
    void UpdateLocalMatrix(u32 index, const mat4x4& localMatrix);
    void UpdateParentSlot(u32 index, u32 parentSlot);

    u32 ReserveClipRect();
    void ReleaseClipRect(u32 index);
    void UpdateClipRect(u32 index, const vec4& rect);

    u32 ReserveMaskInfo();
    void ReleaseMaskInfo(u32 index);
    void UpdateMaskInfo(u32 index, const vec4& region, u32 textureIndex);

    // Public for clip-mask consumers; hash-cached so repeat calls are O(1).
    u32 LoadTextureByPath(std::string_view path);

    void AddCanvasPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);

private:
    void CreatePermanentResources();
    void CreatePipelines();
    void InitDescriptorSets();

    void UpdatePanelVertices(const vec2& clipPos, const vec2& clipSize, ECS::Components::UI::Panel& panel, ECS::Components::UI::PanelTemplate& panelTemplate);
    void UpdateTextVertices(ECS::Components::UI::Widget& widget, ECS::Components::Transform2D& transform, ECS::Components::UI::Text& text, ECS::Components::UI::TextTemplate& textTemplate, const vec2& canvasSize);

    void UpdatePanelData(entt::entity entity, ECS::Components::Transform2D& transform, ECS::Components::UI::Panel& panel, ECS::Components::UI::PanelTemplate& panelTemplate);
    void UpdateTextData(entt::entity entity, ECS::Components::UI::Text& text, ECS::Components::UI::TextTemplate& textTemplate);

    vec2 PixelPosToNDC(const vec2& pixelPosition, const vec2& screenSize) const;
    vec2 PixelSizeToNDC(const vec2& pixelPosition, const vec2& screenSize) const;

    u32 AddTexture(Renderer::TextureID textureID);
    u32 LoadTexture(std::string_view path);

    // --- Sortkey machinery (see CanvasRenderer.cpp for bit layout) -----------------
    // Resolves the effective priority for this widget (0 = normal, >0 = promoted for focus/drag/etc).
    u8 ResolvePriority(entt::registry* registry, entt::entity entity) const;

    // Rebuilds the mapping from canvas entity to its 8-bit canvasOrder, based on canvas layer + registry iteration.
    void RebuildCanvasOrder(entt::registry* registry);

    // Walks a canvas's subtree depth-first, writing sortKey to each Widget component. Siblings are sorted by
    // (Transform2D::layer asc, SceneNode2D::siblingIndex asc) before recursion so the order is deterministic
    // and every produced sortKey is unique.
    void DfsAssignSortKey(entt::registry* registry, entt::entity entity, u8 canvasOrder, u32& traversalIndex, u8 inheritedPriority);

    // Gather + sort + upload for one render-pass bucket. Walks the canvas subtree(s), filters
    // visible Panel/Text entries into _sortScratch, std::sorts by sortKey, copies the sorted
    // IndirectDraws into _uploadScratch, and queues a CPU->GPU upload to the bucket's retained
    // finalSortedArgs + finalCount.
    //
    // canvasEntity == entt::null signals "main bucket" (every non-RT canvas merged).
    void RefreshBucketCPU(entt::registry* registry, entt::entity canvasEntity, bool isRT);

    // Patch one text's IndirectDraw entry in its bucket's finalSortedArgs in place (offset upload),
    // skipping the full DFS + gather + sort. No-op if the text isn't bucketed yet or its slot is
    // stale (a later full rebuild covers it). Used when only glyph count / data index changed.
    void PatchTextBucketSlot(entt::registry* registry, entt::entity entity, ECS::Components::UI::Text& text);


public:
    enum class WidgetDrawType : u32
    {
        Panel = 0,
        Text = 1,
    };

private:
    struct WidgetDrawData
    {
    public:
        uvec4 packed0 = uvec4(0, 0, 0, 0xFFFFFFFFu); // x: type, y: vertexBase, z: reserved, w: worldPositionIndex (i32 reinterpret as -1)
        uvec4 packed1 = uvec4(0, 0, 0, 0); // Panel: x: textureIndex|additiveTextureIndex, y: borderColor, z: color, w: textureScaleToWidgetSize (half2). Text: x: fontTextureIndex, z: textColor, w: borderColor
        vec4 texCoord = vec4(0.0f);                  // Panel only
        vec4 slicingCoord = vec4(0.0f);              // Panel only
        vec4 cornerRadiusAndBorder = vec4(0.0f);     // Panel: xy: cornerRadius, zw: borderSize (normalized per-axis). Text: x: borderSize, zw: unitRange
        uvec4 packed2 = uvec4(0, 0, 0, 0); // x: clipBoundsIndex (into _widgetClipRects), y: maskBoundsIndex (into _widgetMaskInfo), z/w: reserved
    };

private:
    Renderer::Renderer* _renderer;
    GameRenderer* _gameRenderer;
    DebugRenderer* _debugRenderer;

    Renderer::GPUVector<vec4> _vertices;
    Renderer::GPUVector<WidgetDrawData> _widgetDrawDatas;

    Renderer::GPUVector<vec4> _widgetWorldPositions;
    Renderer::GPUVector<mat4x4> _widgetLocalMatrices;
    Renderer::GPUVector<u32> _widgetParentSlots; // parent's slot, or UINT_MAX for a root (canvas)
    Renderer::GPUVector<vec4> _widgetClipRects;
    Renderer::GPUVector<uvec4> _widgetMaskInfo;

    Renderer::Font* _font;
    Renderer::SamplerID _sampler;
    Renderer::TextureArrayID _textures;
    robin_hood::unordered_map<u32, u32> _textureNameHashToIndex;
    robin_hood::unordered_map<Renderer::TextureID::type, u32> _textureIDToIndex;

    Renderer::TextureArrayID _fontTextures;
    robin_hood::unordered_map<Renderer::TextureID::type, u32> _textureIDToFontTexturesIndex;

    Renderer::GraphicsPipelineID _widgetPipeline;

    Renderer::DescriptorSet _widgetDescriptorSet;

    // --- Sortkey state ------------------------------------------------------------
    // Assigned canvasOrder (0..255) per canvas entity, refreshed by RebuildCanvasOrder when
    // the canvas SET changes (gated on DirtyCanvasOrderFlag). Read by DfsAssignSortKey to bake
    // canvasOrder into each widget's sortKey.
    robin_hood::unordered_map<entt::entity, u8> _canvasOrderByEntity;

    // Shared scratch for DfsAssignSortKey. Each recursion level appends its children's entities
    // to the tail, sorts only its own [start, end) range, recurses by-value, and resizes back
    // to its start before returning. Net: zero allocations after the first warmup.
    std::vector<entt::entity> _siblingScratch;

    // --- Per-bucket retained indirect-draw state ----------------------------------
    // One BucketResources per render-pass bucket: one per RT canvas that has ever existed,
    // plus one static _mainBucket for all non-RT canvases merged together. finalSortedArgs
    // is retained across frames; it's CPU-sorted and uploaded only when the bucket is dirty,
    // and consumed as-is by DrawIndirectCount every frame.
    struct BucketResources
    {
        Renderer::BufferID finalSortedArgs = Renderer::BufferID::Invalid();
        u32                finalSortedArgsCapacity = 0;
        u32                drawCount = 0;

        // Single-element u32 count buffer for DrawIndirectCount.
        Renderer::BufferID finalCount = Renderer::BufferID::Invalid();

        // Entity owning each slot in finalSortedArgs, for the stale-slot guard when patching a
        // single entry in place (see PatchTextBucketSlot). Rebuilt by RefreshBucketCPU.
        std::vector<entt::entity> slotOwners;
    };

    robin_hood::unordered_map<entt::entity, BucketResources> _rtBuckets; // key: RT canvas entity
    BucketResources _mainBucket;

    // CPU scratch for gather+sort+upload inside RefreshBucketCPU. Reused across refreshes;
    // `.clear()` preserves capacity.
    struct SortEntry { u32 key; Renderer::IndirectDraw draw; entt::entity entity; };
    std::vector<SortEntry>              _sortScratch;
    std::vector<Renderer::IndirectDraw> _uploadScratch;
};