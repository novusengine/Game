
#include "Shape/ShapeTextureShared.inc.slang"

struct PSInput
{
    float4 position : SV_Position;
    float2 pixelPos : TEXCOORD0;
    uint shapeID : TEXCOORD1;
};

static const float SHAPE_TWO_PI = 6.28318530718f;

// Signed distances are in pixels, so a fixed 1px smoothing window gives resolution-independent AA
float SDFToAlpha(float sd)
{
    return saturate(0.5f - sd);
}

float DashMask(float distAlong, float dashLength, float gapLength, float dashOffset)
{
    if (dashLength <= 0.0f)
        return 1.0f;

    float period = dashLength + gapLength;
    float m = fmod(distAlong + dashOffset, period);
    m += period * (m < 0.0f ? 1.0f : 0.0f);

    // Signed distance to the nearest gap edge, positive inside the dash
    float sd = min(m, dashLength - m);
    return saturate(sd + 0.5f);
}

float SDRoundedBox(float2 p, float2 center, float2 halfExtents, float radius)
{
    float r = min(radius, min(halfExtents.x, halfExtents.y));
    float2 q = abs(p - center) - (halfExtents - r);
    return length(max(q, 0.0f)) + min(max(q.x, q.y), 0.0f) - r;
}

float2 RotateAround(float2 p, float2 center, float angle)
{
    float c = cos(angle);
    float s = sin(angle);
    float2 local = p - center;
    return center + float2(local.x * c - local.y * s, local.x * s + local.y * c);
}

// iq's ellipse distance approximation; exact enough for UI-scale antialiasing
float SDEllipse(float2 p, float2 radii)
{
    float k0 = length(p / radii);
    float k1 = length(p / (radii * radii));
    return k0 * (k0 - 1.0f) / max(k1, 1e-6f);
}

[shader("fragment")]
float4 main(PSInput input) : SV_Target0
{
    ShapeDrawData shape = _shapes[input.shapeID];
    uint type = shape.packed0.x;

    float2 p = input.pixelPos;
    float4 color = UnpackShapeColor(shape.packed0.y);
    float alpha = 0.0f;

    // Record-time clip rect, applied as an antialiased mask to every path through this shader
    float clipMask = 1.0f;
    if (shape.packed1.x != 0u)
    {
        float4 clipRect = _shapeClipRects[shape.packed1.x];
        float2 clipDist = max(clipRect.xy - p, p - clipRect.zw);
        clipMask = SDFToAlpha(max(clipDist.x, clipDist.y));
    }

    if (type == SHAPE_TYPE_LINE)
    {
        float2 a = shape.data0.xy;
        float2 b = shape.data0.zw;
        float halfWidth = shape.data1.x * 0.5f;

        float2 pa = p - a;
        float2 ba = b - a;
        float segmentLengthSq = max(dot(ba, ba), 1e-6f);
        float segmentLength = sqrt(segmentLengthSq);
        float h = saturate(dot(pa, ba) / segmentLengthSq);

        uint capStyle = uint(shape.data1.w);
        float sd;
        if (capStyle == 0u)
        {
            // Round: capsule ends
            sd = length(pa - ba * h) - halfWidth;
        }
        else
        {
            // Butt/square: perpendicular band, cut (or square-extended) at the endpoints
            float alongPixels = dot(pa, ba) / segmentLength;
            float perpendicular = abs(pa.x * ba.y - pa.y * ba.x) / segmentLength;
            float axisOutside = max(-alongPixels, alongPixels - segmentLength) - (capStyle == 2u ? halfWidth : 0.0f);
            sd = max(perpendicular - halfWidth, axisOutside);
        }

        alpha = SDFToAlpha(sd);
        alpha *= DashMask(h * segmentLength, shape.data2.x, shape.data2.y, shape.data2.z);

        // Gradient along the line
        if (shape.data1.z > 0.0f)
        {
            color = lerp(color, UnpackShapeColor(shape.packed0.z), saturate(h * segmentLength / shape.data1.z));
        }
    }
    else if (type == SHAPE_TYPE_PATH)
    {
        // One polyline segment; a pixel only renders from its NEAREST segment (checked against
        // the two neighbors), so joins partition exactly with no double blending
        uint pointIndex = uint(shape.data1.y);
        uint neighborFlags = uint(shape.data1.w);
        float halfWidth = shape.data1.x * 0.5f;

        float2 a = _shapePathPoints[pointIndex];
        float2 b = _shapePathPoints[pointIndex + 1];
        float2 pa = p - a;
        float2 ba = b - a;
        float segmentLengthSq = max(dot(ba, ba), 1e-6f);
        float h = saturate(dot(pa, ba) / segmentLengthSq);
        float distance = length(pa - ba * h);

        bool owned = true;
        if ((neighborFlags & 1u) != 0u) // previous segment keeps ties
        {
            float2 prev = _shapePathPoints[pointIndex - 1];
            float2 pPrev = p - prev;
            float2 aPrev = a - prev;
            float hPrev = saturate(dot(pPrev, aPrev) / max(dot(aPrev, aPrev), 1e-6f));
            owned = owned && distance < length(pPrev - aPrev * hPrev);
        }
        if ((neighborFlags & 2u) != 0u) // this segment keeps ties against the next
        {
            float2 next = _shapePathPoints[pointIndex + 2];
            float2 pb = p - b;
            float2 nb = next - b;
            float hNext = saturate(dot(pb, nb) / max(dot(nb, nb), 1e-6f));
            owned = owned && distance <= length(pb - nb * hNext);
        }

        if (!owned)
        {
            discard;
        }

        float segmentLength = sqrt(segmentLengthSq);
        float along = h * segmentLength;
        alpha = SDFToAlpha(distance - halfWidth);
        alpha *= DashMask(along, shape.data2.x, shape.data2.y, shape.data2.z);

        float gradientTotal = asfloat(shape.packed1.y);
        if (gradientTotal > 0.0f)
        {
            color = lerp(color, UnpackShapeColor(shape.packed0.z), saturate((shape.data1.z + along) / gradientTotal));
        }
    }
    else if (type == SHAPE_TYPE_RECT || type == SHAPE_TYPE_RECT_OUTLINE)
    {
        float2 center = (shape.data0.xy + shape.data0.zw) * 0.5f;
        float2 halfExtents = (shape.data0.zw - shape.data0.xy) * 0.5f;

        // Rotation: evaluate in the rect's local (unrotated) frame
        float2 pLocal = shape.data1.w != 0.0f ? RotateAround(p, center, -shape.data1.w) : p;

        // Per-corner radii (4x u8 in packed0.w: bl, br, tl, tr); 0 = uniform data1.x
        float cornerRadius = shape.data1.x;
        if (shape.packed0.w != 0u)
        {
            uint packedRadii = shape.packed0.w;
            bool rightHalf = pLocal.x >= center.x;
            bool topHalf = pLocal.y >= center.y;
            uint shift = (topHalf ? 16u : 0u) + (rightHalf ? 8u : 0u);
            cornerRadius = float((packedRadii >> shift) & 0xFFu);
        }

        float sd = SDRoundedBox(pLocal, center, halfExtents, cornerRadius);

        // Vertical gradient, bottom -> top in the local frame
        if (shape.data1.z > 0.0f)
        {
            float t = saturate((pLocal.y - shape.data0.y) / max(shape.data0.w - shape.data0.y, 1e-3f));
            color = lerp(color, UnpackShapeColor(shape.packed0.z), t);
        }

        if (type == SHAPE_TYPE_RECT_OUTLINE)
        {
            sd = abs(sd) - shape.data1.y * 0.5f;

            // Dash phase from the distance along the perimeter, via projection onto the nearest
            // edge (exact on straight edges, approximate around rounded corners)
            float2 local = pLocal - shape.data0.xy;
            float2 size = shape.data0.zw - shape.data0.xy;
            float dRight = size.x - local.x;
            float dTop = size.y - local.y;
            float minEdgeDist = min(min(local.x, dRight), min(local.y, dTop));

            float perimeterT;
            if (minEdgeDist == local.y) // bottom
            {
                perimeterT = local.x;
            }
            else if (minEdgeDist == dRight) // right
            {
                perimeterT = size.x + local.y;
            }
            else if (minEdgeDist == dTop) // top
            {
                perimeterT = size.x + size.y + (size.x - local.x);
            }
            else // left
            {
                perimeterT = 2.0f * size.x + size.y + (size.y - local.y);
            }

            alpha = SDFToAlpha(sd) * DashMask(perimeterT, shape.data2.x, shape.data2.y, shape.data2.z);
            alpha *= clipMask;
            return float4(color.rgb * color.a * alpha, color.a * alpha);
        }
        alpha = SDFToAlpha(sd);
    }
    else if (type == SHAPE_TYPE_CIRCLE || type == SHAPE_TYPE_CIRCLE_OUTLINE)
    {
        float2 toP = p - shape.data0.xy;
        float radius = shape.data0.z;

        float sd = length(toP) - radius;
        if (type == SHAPE_TYPE_CIRCLE_OUTLINE)
        {
            sd = abs(sd) - shape.data0.w * 0.5f;

            float startAngle = shape.data1.x;
            float span = shape.data1.y - startAngle;
            float rel = fmod(atan2(toP.y, toP.x) - startAngle, SHAPE_TWO_PI);
            rel += SHAPE_TWO_PI * (rel < 0.0f ? 1.0f : 0.0f);

            // Arc: mask off the angular range outside [startAngle, endAngle]
            if (span < SHAPE_TWO_PI - 1e-3f)
            {
                // Angular signed distance to the arc's span, converted to pixels at this radius
                float angularSD = (rel <= span) ? -min(rel, span - rel) : min(rel - span, SHAPE_TWO_PI - rel);
                sd = max(sd, angularSD * radius);
            }

            // Angular gradient along the span (full circles blend around the whole ring)
            if (shape.data1.z > 0.0f)
            {
                float gradientSpan = span < SHAPE_TWO_PI - 1e-3f ? span : SHAPE_TWO_PI;
                color = lerp(color, UnpackShapeColor(shape.packed0.z), saturate(rel / gradientSpan));
            }

            // Dash phase from the arc length along the circle, measured from startAngle
            alpha = SDFToAlpha(sd) * DashMask(rel * radius, shape.data2.x, shape.data2.y, shape.data2.z);
            alpha *= clipMask;
            return float4(color.rgb * color.a * alpha, color.a * alpha);
        }

        // Radial gradient, center -> edge
        if (shape.data1.z > 0.0f)
        {
            color = lerp(color, UnpackShapeColor(shape.packed0.z), saturate(length(toP) / max(radius, 1e-3f)));
        }
        alpha = SDFToAlpha(sd);
    }
    else if (type == SHAPE_TYPE_ELLIPSE || type == SHAPE_TYPE_ELLIPSE_OUTLINE)
    {
        float2 center = shape.data0.xy;
        float2 radii = max(shape.data0.zw, float2(1e-3f, 1e-3f));
        float2 pLocal = shape.data1.w != 0.0f ? RotateAround(p, center, -shape.data1.w) : p;

        float sd = SDEllipse(pLocal - center, radii);
        if (type == SHAPE_TYPE_ELLIPSE_OUTLINE)
        {
            sd = abs(sd) - shape.data1.x * 0.5f;

            // Dash phase: uniform in normalized angle, scaled to the (Ramanujan) perimeter
            float2 normalized = (pLocal - center) / radii;
            float rel = fmod(atan2(normalized.y, normalized.x), SHAPE_TWO_PI);
            rel += SHAPE_TWO_PI * (rel < 0.0f ? 1.0f : 0.0f);
            alpha = SDFToAlpha(sd) * DashMask(rel / SHAPE_TWO_PI * shape.data1.y, shape.data2.x, shape.data2.y, shape.data2.z);
            alpha *= clipMask;
            return float4(color.rgb * color.a * alpha, color.a * alpha);
        }
        else if (shape.data1.z > 0.0f)
        {
            // Radial gradient, center -> edge
            color = lerp(color, UnpackShapeColor(shape.packed0.z), saturate(length((pLocal - center) / radii)));
        }
        alpha = SDFToAlpha(sd);
    }
    else if (type == SHAPE_TYPE_GRID)
    {
        float2 lp = p - shape.data0.xy;
        float2 spacing = max(shape.data0.zw, float2(1e-3f, 1e-3f));

        float2 nearestIndex = round(lp / spacing);
        float2 distToLine = abs(lp - nearestIndex * spacing);

        float minorWidth = shape.data1.x;
        int majorEvery = int(shape.data1.y);
        float majorWidth = shape.data1.z;
        float4 minorColor = color;
        float4 majorColor = UnpackShapeColor(shape.packed0.z);

        bool2 isMajor = bool2(false, false);
        if (majorEvery > 0)
        {
            isMajor.x = (int(nearestIndex.x) % majorEvery) == 0;
            isMajor.y = (int(nearestIndex.y) % majorEvery) == 0;
        }

        // Vertical lines (constant x), then horizontal lines composited premultiplied over them
        float alphaX = SDFToAlpha(distToLine.x - (isMajor.x ? majorWidth : minorWidth) * 0.5f);
        float alphaY = SDFToAlpha(distToLine.y - (isMajor.y ? majorWidth : minorWidth) * 0.5f);
        float4 colorX = isMajor.x ? majorColor : minorColor;
        float4 colorY = isMajor.y ? majorColor : minorColor;

        float3 premulX = colorX.rgb * colorX.a * alphaX;
        float aX = colorX.a * alphaX;
        float3 premulY = colorY.rgb * colorY.a * alphaY;
        float aY = colorY.a * alphaY;

        aX *= clipMask;
        aY *= clipMask;
        premulX *= clipMask;
        premulY *= clipMask;
        return float4(premulX + premulY * (1.0f - aX), aX + aY * (1.0f - aX));
    }
    else if (type == SHAPE_TYPE_PIE)
    {
        float2 toP = p - shape.data0.xy;
        float radius = shape.data0.z;

        float sd = length(toP) - radius;

        float startAngle = shape.data1.x;
        float span = shape.data1.y - startAngle;
        float rel = fmod(atan2(toP.y, toP.x) - startAngle, SHAPE_TWO_PI);
        rel += SHAPE_TWO_PI * (rel < 0.0f ? 1.0f : 0.0f);

        if (span < SHAPE_TWO_PI - 1e-3f)
        {
            // Angular signed distance to the span, scaled by the local radius so the wedge edges
            // stay ~1px-crisp at any distance from the tip
            float angularSD = (rel <= span) ? -min(rel, span - rel) : min(rel - span, SHAPE_TWO_PI - rel);
            sd = max(sd, angularSD * length(toP));
        }

        // Angular gradient along the span
        if (shape.data1.z > 0.0f)
        {
            float gradientSpan = span < SHAPE_TWO_PI - 1e-3f ? span : SHAPE_TWO_PI;
            color = lerp(color, UnpackShapeColor(shape.packed0.z), saturate(rel / gradientSpan));
        }
        alpha = SDFToAlpha(sd);
    }
    else if (type == SHAPE_TYPE_TRIANGLE)
    {
        // CCW triangle from a polygon fill. Hull edges (mask bit set) are antialiased; interior
        // clip diagonals are hard analytic cuts, so adjacent triangles partition the polygon
        // exactly and translucent fills show no seams
        float2 v[3] = { shape.data0.xy, shape.data0.zw, shape.data1.xy };
        uint hullMask = shape.packed0.w;

        alpha = 1.0f;
        [unroll]
        for (uint edge = 0; edge < 3; edge++)
        {
            float2 edgeStart = v[edge];
            float2 edgeVector = v[(edge + 1) % 3] - edgeStart;
            float2 toPixel = p - edgeStart;

            // Signed distance to the edge line, positive outside the CCW triangle
            float distOutside = (toPixel.x * edgeVector.y - toPixel.y * edgeVector.x) / max(length(edgeVector), 1e-6f);
            bool isHull = (hullMask & (1u << edge)) != 0u;
            alpha = min(alpha, isHull ? saturate(0.5f - distOutside) : (distOutside <= 0.0f ? 1.0f : 0.0f));
        }
    }
    else if (type == SHAPE_TYPE_IMAGE)
    {
        float2 center = (shape.data0.xy + shape.data0.zw) * 0.5f;
        float2 pLocal = shape.data2.x != 0.0f ? RotateAround(p, center, -shape.data2.x) : p;

        // Rect coverage with 1px AA at the borders
        float2 halfExtents = (shape.data0.zw - shape.data0.xy) * 0.5f;
        float2 q = abs(pLocal - center) - halfExtents;
        float sd = length(max(q, 0.0f)) + min(max(q.x, q.y), 0.0f);
        alpha = SDFToAlpha(sd);

        // Surface pixel space is bottom-left origin while texture v runs top-down, so flip v
        float2 t = (pLocal - shape.data0.xy) / max(shape.data0.zw - shape.data0.xy, float2(1e-3f, 1e-3f));
        float2 uv = lerp(shape.data1.xy, shape.data1.zw, float2(t.x, 1.0f - t.y));

        float4 sampled = _shapeTextures[shape.packed0.z].Sample(_shapeSampler, saturate(uv));
        color *= sampled;
    }
    else if (type == SHAPE_TYPE_RECT_SHADOW)
    {
        float2 center = (shape.data0.xy + shape.data0.zw) * 0.5f;
        float2 pLocal = shape.data1.w != 0.0f ? RotateAround(p, center, -shape.data1.w) : p;
        float2 halfExtents = (shape.data0.zw - shape.data0.xy) * 0.5f;

        // Analytic soft shadow: smooth falloff of the rounded-box SDF over the blur radius.
        // Squared smoothstep gives a Gaussian-like tail without any extra passes
        float sd = SDRoundedBox(pLocal, center, halfExtents, shape.data1.x);
        float t = saturate(0.5f - sd / (2.0f * shape.data1.y));
        alpha = t * t * (3.0f - 2.0f * t);
    }
    else if (type == SHAPE_TYPE_CHECKERBOARD)
    {
        float2 cellSize = max(shape.data0.zw, float2(1e-3f, 1e-3f));
        float2 cell = floor((p - shape.data0.xy) / cellSize);

        float parity = abs(fmod(cell.x + cell.y, 2.0f));
        if (parity > 0.5f)
        {
            color = UnpackShapeColor(shape.packed0.z);
        }
        alpha = 1.0f;
    }

    // Premultiplied alpha, composited with ONE / INV_SRC_ALPHA
    alpha *= clipMask;
    return float4(color.rgb * color.a * alpha, color.a * alpha);
}
