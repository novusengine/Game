
#include "Shape/ShapeTextureShared.inc.slang"

struct VSInput
{
    uint vertexID : SV_VulkanVertexID;
    uint shapeID : SV_VulkanInstanceID;
};

struct VSOutput
{
    float4 position : SV_Position;
    float2 pixelPos : TEXCOORD0;
    uint shapeID : TEXCOORD1;
};

static const float2 QUAD_CORNERS[6] =
{
    float2(0.0f, 0.0f), float2(1.0f, 0.0f), float2(0.0f, 1.0f),
    float2(0.0f, 1.0f), float2(1.0f, 0.0f), float2(1.0f, 1.0f)
};

// Padding around each shape's bounding quad so the SDF antialiasing falloff isn't clipped
static const float AA_PAD = 2.0f;

[shader("vertex")]
VSOutput main(VSInput input)
{
    ShapeDrawData shape = _shapes[input.shapeID];
    uint type = shape.packed0.x;

    // GRID and CHECKERBOARD cover the whole surface
    float2 bbMin = float2(0.0f, 0.0f);
    float2 bbMax = _constants.viewportSize;

    if (type == SHAPE_TYPE_LINE || type == SHAPE_TYPE_PATH)
    {
        float pad = shape.data1.x * 0.5f + AA_PAD;
        bbMin = min(shape.data0.xy, shape.data0.zw) - pad;
        bbMax = max(shape.data0.xy, shape.data0.zw) + pad;
    }
    else if (type == SHAPE_TYPE_RECT || type == SHAPE_TYPE_RECT_OUTLINE)
    {
        float pad = shape.data1.y * 0.5f + AA_PAD;
        float2 center = (shape.data0.xy + shape.data0.zw) * 0.5f;
        float2 extents = (shape.data0.zw - shape.data0.xy) * 0.5f + pad;

        float rotation = shape.data1.w;
        if (rotation != 0.0f)
        {
            float c = abs(cos(rotation));
            float s = abs(sin(rotation));
            extents = float2(c * extents.x + s * extents.y, s * extents.x + c * extents.y);
        }
        bbMin = center - extents;
        bbMax = center + extents;
    }
    else if (type == SHAPE_TYPE_ELLIPSE || type == SHAPE_TYPE_ELLIPSE_OUTLINE)
    {
        // Rotation-safe: bound by the larger radius in both axes
        float pad = max(shape.data0.z, shape.data0.w) + shape.data1.x * 0.5f + AA_PAD;
        bbMin = shape.data0.xy - pad;
        bbMax = shape.data0.xy + pad;
    }
    else if (type == SHAPE_TYPE_CIRCLE || type == SHAPE_TYPE_CIRCLE_OUTLINE || type == SHAPE_TYPE_PIE)
    {
        float pad = shape.data0.z + shape.data0.w * 0.5f + AA_PAD;
        bbMin = shape.data0.xy - pad;
        bbMax = shape.data0.xy + pad;
    }
    else if (type == SHAPE_TYPE_IMAGE || type == SHAPE_TYPE_RECT_SHADOW)
    {
        float pad = (type == SHAPE_TYPE_RECT_SHADOW ? shape.data1.y : 0.0f) + AA_PAD;
        float2 center = (shape.data0.xy + shape.data0.zw) * 0.5f;
        float2 extents = (shape.data0.zw - shape.data0.xy) * 0.5f + pad;

        float rotation = type == SHAPE_TYPE_IMAGE ? shape.data2.x : shape.data1.w;
        if (rotation != 0.0f)
        {
            float c = abs(cos(rotation));
            float s = abs(sin(rotation));
            extents = float2(c * extents.x + s * extents.y, s * extents.x + c * extents.y);
        }
        bbMin = center - extents;
        bbMax = center + extents;
    }
    else if (type == SHAPE_TYPE_TRIANGLE)
    {
        bbMin = min(min(shape.data0.xy, shape.data0.zw), shape.data1.xy) - AA_PAD;
        bbMax = max(max(shape.data0.xy, shape.data0.zw), shape.data1.xy) + AA_PAD;
    }

    // Clipped shapes shrink their quad to the clip rect so clipped-away pixels never shade
    if (shape.packed1.x != 0u)
    {
        float4 clipRect = _shapeClipRects[shape.packed1.x];
        bbMin = max(bbMin, clipRect.xy - AA_PAD);
        bbMax = min(bbMax, clipRect.zw + AA_PAD);
        bbMax = max(bbMax, bbMin); // fully clipped shapes collapse to a degenerate quad
    }

    float2 pixelPos = lerp(bbMin, bbMax, QUAD_CORNERS[input.vertexID]);
    float2 ndc = (pixelPos / _constants.viewportSize) * 2.0f - 1.0f;

    VSOutput output;
    output.position = float4(ndc, 0.0f, 1.0f);
    output.pixelPos = pixelPos;
    output.shapeID = input.shapeID;
    return output;
}
