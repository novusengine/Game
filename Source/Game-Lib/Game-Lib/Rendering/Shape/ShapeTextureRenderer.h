#pragma once
#include <Base/Types.h>
#include <Base/Math/Color.h>

#include <Renderer/DescriptorSet.h>
#include <Renderer/GPUVector.h>
#include <Renderer/Descriptors/SamplerDesc.h>
#include <Renderer/Descriptors/TextureArrayDesc.h>
#include <Renderer/Descriptors/TextureDesc.h>

#include <robinhood/robinhood.h>

#include <limits>
#include <string>
#include <string_view>
#include <vector>

struct RenderResources;
class GameRenderer;

namespace Renderer
{
    class Renderer;
    class RenderGraph;
}

// Draws 2D vector shapes (lines, curves, rects, circles, grids) into offscreen textures that any
// system can consume, e.g. as a panel background via PanelTemplate::backgroundRT. A surface only
// re-renders when its contents change (coalesced dirty flag), so static content costs nothing per
// frame. All coordinates are in surface pixels with a bottom-left origin (+Y up, matching the UI
// convention — the backend's flipped viewport makes NDC -1 the bottom row as displayed).
class ShapeTextureRenderer
{
public:
    using SurfaceID = u32;
    static constexpr SurfaceID INVALID_SURFACE_ID = std::numeric_limits<u32>::max();

    enum class CapStyle : u8
    {
        Round = 0, // capsule ends; doubles as the join shape
        Butt,      // cut flush at the endpoints
        Square     // extended by half the width, square corners
    };

    struct StrokeStyle
    {
    public:
        f32 width = 1.0f;
        Color color = Color::White;
        f32 dashLength = 0.0f; // 0 = solid
        f32 gapLength = 0.0f;
        f32 dashOffset = 0.0f;

        // Gradient: color at the stroke's start blends to endColor at its end (along the path for
        // lines/polylines/beziers, along the angular span for arcs)
        Color endColor = Color::White;
        bool useEndColor = false;

        // Single lines only; polylines/beziers render as paths with round caps/joins
        CapStyle capStyle = CapStyle::Round;
    };

    ShapeTextureRenderer(Renderer::Renderer* renderer, GameRenderer* gameRenderer);

    void Update(f32 deltaTime);
    void AddShapePass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);

    SurfaceID CreateSurface(const uvec2& size, const std::string& name);
    void DestroySurface(SurfaceID surfaceID);
    // Looks up a surface by its creation name; destruction invalidates the entry.
    // Returns INVALID_SURFACE_ID when no live surface has the name
    SurfaceID FindSurface(std::string_view name) const;

    // Recreates the surface texture at a new size and schedules a redraw. The TextureID changes,
    // so consumers (e.g. panel backgrounds) must re-fetch and re-bind it. The old texture leaks
    // until the renderer gains a texture destruction path
    void ResizeSurface(SurfaceID surfaceID, const uvec2& size);

    // Opaque/translucent backdrop rendered beneath the surface's shapes (default: fully
    // transparent). Rendered as an implicit full-surface rect
    void SetClearColor(SurfaceID surfaceID, Color color);

    // Registers a texture in this renderer's own texture array (deliberately not shared with
    // CanvasRenderer, so surfaces stay usable outside the UI) and returns its index for DrawImage.
    // Results are cached; a failed load returns 0 (the default white texture)
    size_t AddTexture(Renderer::TextureID textureID);
    size_t LoadTexture(std::string_view path);
    Renderer::TextureID GetTextureID(SurfaceID surfaceID) const;
    uvec2 GetSurfaceSize(SurfaceID surfaceID) const;

    // Empties the surface's shape list and schedules a redraw (clears the texture to transparent)
    void ClearSurface(SurfaceID surfaceID);
    // Schedules a redraw of the retained shape list without changing it
    void ForceRefresh(SurfaceID surfaceID);

    void DrawLine(SurfaceID surfaceID, const vec2& from, const vec2& to, const StrokeStyle& style);
    void DrawPolyline(SurfaceID surfaceID, const vec2* points, u32 numPoints, const StrokeStyle& style);
    void DrawBezier(SurfaceID surfaceID, const vec2& p0, const vec2& c0, const vec2& c1, const vec2& p1, const StrokeStyle& style);

    // cornerRadii = (bottomLeft, bottomRight, topLeft, topRight), each stored as a whole pixel
    // count (0-255). rotation spins the rect around its center, in radians CCW
    void DrawRect(SurfaceID surfaceID, const vec2& min, const vec2& max, Color color, f32 cornerRadius = 0.0f, f32 rotation = 0.0f);
    void DrawRect(SurfaceID surfaceID, const vec2& min, const vec2& max, Color color, const vec4& cornerRadii, f32 rotation = 0.0f);
    void DrawRectGradient(SurfaceID surfaceID, const vec2& min, const vec2& max, Color bottomColor, Color topColor, f32 cornerRadius = 0.0f, f32 rotation = 0.0f);
    void DrawRectOutline(SurfaceID surfaceID, const vec2& min, const vec2& max, const StrokeStyle& style, f32 cornerRadius = 0.0f, f32 rotation = 0.0f);

    void DrawCircle(SurfaceID surfaceID, const vec2& center, f32 radius, Color color);
    void DrawCircleGradient(SurfaceID surfaceID, const vec2& center, f32 radius, Color centerColor, Color edgeColor);
    void DrawEllipse(SurfaceID surfaceID, const vec2& center, const vec2& radii, Color color, f32 rotation = 0.0f);
    // Dash patterns are supported; the phase is uniform in normalized angle, so dashes stretch
    // slightly along the flatter sides of eccentric ellipses
    void DrawEllipseOutline(SurfaceID surfaceID, const vec2& center, const vec2& radii, const StrokeStyle& style, f32 rotation = 0.0f);
    void DrawCircleOutline(SurfaceID surfaceID, const vec2& center, f32 radius, const StrokeStyle& style);
    void DrawArc(SurfaceID surfaceID, const vec2& center, f32 radius, f32 startAngle, f32 endAngle, const StrokeStyle& style);
    void DrawPie(SurfaceID surfaceID, const vec2& center, f32 radius, f32 startAngle, f32 endAngle, Color color);
    void DrawPieGradient(SurfaceID surfaceID, const vec2& center, f32 radius, f32 startAngle, f32 endAngle, Color startColor, Color endColor);

    // Filled polygon (convex or concave, no self-intersection), ear-clipped into triangles on the
    // CPU. Interior edges are hard analytic cuts so the triangles partition the polygon exactly
    // (no seams, translucent fills included); only the polygon boundary is antialiased
    void DrawPolygon(SurfaceID surfaceID, const vec2* points, u32 numPoints, Color color);

    void DrawGrid(SurfaceID surfaceID, const vec2& origin, const vec2& spacing, f32 lineWidth, Color color, u32 majorEvery = 0, f32 majorLineWidth = 0.0f, Color majorColor = Color::White);
    void DrawCheckerboard(SurfaceID surfaceID, const vec2& origin, const vec2& cellSize, Color colorA, Color colorB);

    // Filled polygon with holes: the outer boundary and each hole are bridged and ear-clipped
    // together; outer and hole edges antialias, bridge edges cut hard. Holes must lie inside the
    // outer polygon and not touch each other
    void DrawPolygonWithHoles(SurfaceID surfaceID, const vec2* outerPoints, u32 numOuterPoints, const std::vector<std::vector<vec2>>& holes, Color color);

    // Stamps a registered texture (see AddTexture/LoadTexture) into the surface, tinted and
    // optionally uv-cropped and rotated around the rect's center
    void DrawImage(SurfaceID surfaceID, const vec2& min, const vec2& max, u32 textureIndex, Color tint = Color::White, const vec2& uvMin = vec2(0.0f, 0.0f), const vec2& uvMax = vec2(1.0f, 1.0f), f32 rotation = 0.0f);

    // Record-time clip rects: shapes drawn while a clip is pushed are clipped (antialiased) to it.
    // Nested pushes intersect. Clip rects are surface-pixel axis-aligned
    void PushClipRect(SurfaceID surfaceID, const vec2& min, const vec2& max);
    void PopClipRect(SurfaceID surfaceID);

    // Record-time transform stack: shapes drawn while a transform is pushed have their positions,
    // sizes and angles transformed (translate, uniform scale, rotate; nested pushes compose).
    // Grids and checkerboards ignore the rotation component
    void PushTransform(SurfaceID surfaceID, const vec2& translation, f32 scale = 1.0f, f32 rotation = 0.0f);
    void PopTransform(SurfaceID surfaceID);

    // Analytic drop shadow for a rounded rect (Gaussian-like falloff over blurRadius, evaluated
    // per pixel; no extra passes). Draw it before the shape it sits under. offset shifts the
    // shadow, e.g. vec2(3, -4) for a light from the upper left
    void DrawRectShadow(SurfaceID surfaceID, const vec2& min, const vec2& max, f32 cornerRadius, f32 blurRadius, Color color, const vec2& offset = vec2(0.0f, 0.0f), f32 rotation = 0.0f);

private:
    enum class ShapeType : u32
    {
        Line = 0,
        Rect,
        RectOutline,
        Circle,
        CircleOutline,
        Grid,
        Checkerboard,
        Pie,
        Triangle,
        Ellipse,
        EllipseOutline,
        Image,
        RectShadow,
        Path
    };

    // Must match ShapeDrawData in Shape/ShapeTextureShared.inc.slang, including the per-type data layout
    struct ShapeDrawData
    {
    public:
        uvec4 packed0 = uvec4(0); // x: type, y: colorA, z: colorB, w: type-specific
        uvec4 packed1 = uvec4(0); // x: clipRectIndex (0 = none), y: type-specific (f32 bits), z/w: reserved
        vec4 data0 = vec4(0.0f);
        vec4 data1 = vec4(0.0f);
        vec4 data2 = vec4(0.0f);
    };

    struct Transform2D
    {
    public:
        vec2 translation = vec2(0.0f);
        f32 scale = 1.0f;
        f32 rotation = 0.0f;
    };

    struct Surface
    {
    public:
        std::string name;
        Renderer::TextureID texture = Renderer::TextureID::Invalid();
        uvec2 size = uvec2(0, 0);
        Color clearColor = Color(0.0f, 0.0f, 0.0f, 0.0f);
        std::vector<ShapeDrawData> shapes;
        std::vector<vec4> clipRects;       // record-time clip rects; shapes index them (1-based, 0 = none)
        std::vector<u32> clipStack;
        std::vector<vec2> pathPoints;      // waypoints for Path shapes
        std::vector<Transform2D> transformStack;
        bool dirty = false;
        bool inUse = false;
    };

    struct DrawRequest
    {
    public:
        SurfaceID surfaceID;
        u32 firstShape;
        u32 numShapes;
    };

    void CreatePermanentResources();
    void CreatePipelines();
    void InitDescriptorSets();

    Surface* GetSurface(SurfaceID surfaceID);
    const Surface* GetSurface(SurfaceID surfaceID) const;
    static void ResetSurfaceContent(Surface& surface);
    ShapeDrawData* AddShape(SurfaceID surfaceID, ShapeType type);
    void CreateSurfaceTexture(Surface& surface, const uvec2& size);
    void TriangulatePolygon(SurfaceID surfaceID, const vec2* points, u32 numPoints, const u8* edgeIsHullInput, Color color);
    static vec2 TransformPoint(const Surface& surface, const vec2& point);
    static f32 TransformLength(const Surface& surface, f32 length);
    static f32 TransformAngle(const Surface& surface, f32 angle);

private:
    Renderer::Renderer* _renderer = nullptr;
    GameRenderer* _gameRenderer = nullptr;

    std::vector<Surface> _surfaces;
    std::vector<SurfaceID> _freeSurfaceIDs;
    robin_hood::unordered_map<u32, SurfaceID> _nameHashToSurfaceID;

    // Dirty surfaces to render this frame; built in Update() (which coalesces the dirty flags), consumed by the pass
    std::vector<DrawRequest> _drawRequests;

    Renderer::GPUVector<ShapeDrawData> _shapes;
    Renderer::GPUVector<vec4> _clipRects;
    Renderer::GPUVector<vec2> _pathPoints;
    Renderer::DescriptorSet _shapeDescriptorSet;
    Renderer::GraphicsPipelineID _shapePipeline;

    Renderer::TextureArrayID _textures;
    Renderer::SamplerID _sampler;
    robin_hood::unordered_map<Renderer::TextureID::type, size_t> _textureIDToIndex;
    robin_hood::unordered_map<u64, size_t> _textureNameHashToIndex;
};
