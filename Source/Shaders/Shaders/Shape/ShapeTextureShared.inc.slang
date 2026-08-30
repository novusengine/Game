#ifndef SHAPE_TEXTURE_SHARED_INCLUDED
#define SHAPE_TEXTURE_SHARED_INCLUDED

#include "Include/Common.inc.slang"

#define SHAPE_TYPE_LINE 0u
#define SHAPE_TYPE_RECT 1u
#define SHAPE_TYPE_RECT_OUTLINE 2u
#define SHAPE_TYPE_CIRCLE 3u
#define SHAPE_TYPE_CIRCLE_OUTLINE 4u
#define SHAPE_TYPE_GRID 5u
#define SHAPE_TYPE_CHECKERBOARD 6u
#define SHAPE_TYPE_PIE 7u
#define SHAPE_TYPE_TRIANGLE 8u
#define SHAPE_TYPE_ELLIPSE 9u
#define SHAPE_TYPE_ELLIPSE_OUTLINE 10u
#define SHAPE_TYPE_IMAGE 11u
#define SHAPE_TYPE_RECT_SHADOW 12u
#define SHAPE_TYPE_PATH 13u

// Data layout per type (all positions/sizes in surface pixels, bottom-left origin / +Y up — the
// flipped viewport maps pixel y=0 to the bottom row as displayed):
// Gradients: colorB = packed0.z; enabled per type as noted (colorA blends to colorB)
// packed1.x on every type: clip rect index into _shapeClipRects (0 = unclipped)
// LINE:           data0 = (from.xy, to.xy), data1 = (width, 0, gradientTotal, capStyle 0=round/1=butt/2=square), data2 = (dashLength, gapLength, dashOffset, 0)
// PATH:           one polyline segment. data0 = tight bbox, data1 = (width, pointIndex of the segment start, distanceAlongAtStart, neighborFlags bit0=hasPrev bit1=hasNext), data2 = (dashLength, gapLength, dashOffset, 0), packed1.y = f32 bits gradientTotal. Pixels are owned by the nearest segment, so joins partition exactly (no double blending)
// RECT:           data0 = (min.xy, max.xy), data1 = (cornerRadius, 0, gradientFlag, rotation), packed0.w = per-corner radii 4x u8 (bl, br, tl, tr; 0 = uniform data1.x). Vertical gradient bottom->top
// RECT_OUTLINE:   data0 = (min.xy, max.xy), data1 = (cornerRadius, strokeWidth, gradientFlag, rotation), data2 = (dashLength, gapLength, dashOffset, 0), packed0.w = per-corner radii
// CIRCLE:         data0 = (center.xy, radius, 0), data1 = (0, 0, gradientFlag, 0). Radial gradient center->edge
// CIRCLE_OUTLINE: data0 = (center.xy, radius, strokeWidth), data1 = (startAngle, endAngle, gradientFlag, 0), data2 = (dashLength, gapLength, dashOffset, 0). Angular gradient along the span
// GRID:           data0 = (origin.xy, spacing.xy), data1 = (lineWidth, majorEvery, majorLineWidth, 0), colorA = minor, colorB = major
// CHECKERBOARD:   data0 = (origin.xy, cellSize.xy), colorA/colorB = the two cell colors
// PIE:            data0 = (center.xy, radius, 0), data1 = (startAngle, endAngle, gradientFlag, 0). Angular gradient along the span
// ELLIPSE:         data0 = (center.xy, radiusX, radiusY), data1 = (0, 0, gradientFlag, rotation). Radial gradient center->edge
// ELLIPSE_OUTLINE: data0 = (center.xy, radiusX, radiusY), data1 = (strokeWidth, perimeter, 0, rotation), data2 = (dashLength, gapLength, dashOffset, 0). Dash phase is uniform in normalized angle
// IMAGE:           data0 = (min.xy, max.xy), data1 = (uvMin.xy, uvMax.xy), data2 = (rotation, 0, 0, 0), packed0.z = textureIndex, colorA = tint
// RECT_SHADOW:     data0 = (min.xy, max.xy) pre-offset, data1 = (cornerRadius, blurRadius, 0, rotation). Analytic soft falloff, no extra passes
// TRIANGLE:       data0 = (v0.xy, v1.xy), data1 = (v2.xy, 0, 0), packed0.w bits 0..2 = edge-is-hull mask (a->b, b->c, c->a). One CCW polygon triangle; hull edges antialias, interior diagonals cut hard
struct ShapeDrawData
{
    uint4 packed0; // x: type, y: colorA, z: colorB, w: type-specific
    uint4 packed1; // x: clipRectIndex (0 = none), y: type-specific (f32 bits), z/w: reserved
    float4 data0;
    float4 data1;
    float4 data2;
};

[[vk::binding(0, PER_PASS)]] StructuredBuffer<ShapeDrawData> _shapes;
[[vk::binding(1, PER_PASS)]] Texture2D<float4> _shapeTextures[1024];
[[vk::binding(2, PER_PASS)]] SamplerState _shapeSampler;
[[vk::binding(3, PER_PASS)]] StructuredBuffer<float4> _shapeClipRects;
[[vk::binding(4, PER_PASS)]] StructuredBuffer<float2> _shapePathPoints;

struct Constants
{
    float2 viewportSize;
};
[[vk::push_constant]] Constants _constants;

// Colors are packed CPU-side with Color::ToRGBA32, matching the widget pipeline's convention
float4 UnpackShapeColor(uint inColor)
{
    return PackedUnormsToFloat4(inColor);
}

#endif // SHAPE_TEXTURE_SHARED_INCLUDED
