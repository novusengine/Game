#include "ShapeSurface.h"

#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Scripting/Zenith.h>

#include <algorithm>
#include <vector>

namespace Scripting::UI
{
    namespace
    {
        ShapeTextureRenderer* GetShapeTextureRenderer()
        {
            return ServiceLocator::GetGameRenderer()->GetShapeTextureRenderer();
        }

        // Finding a surface by name goes through the renderer, which invalidates its name map on
        // destruction; surfaces (and their textures) deliberately outlive script reloads
        uvec2 CheckSurfaceSize(Zenith* zenith, const vec3& size)
        {
            if (size.x < 1.0f || size.y < 1.0f || size.x > 16384.0f || size.y > 16384.0f)
            {
                luaL_error(zenith->state, "Surface size must be between 1x1 and 16384x16384 (got %.0fx%.0f)", size.x, size.y);
            }
            return uvec2(size.x, size.y);
        }

        // Reads "width, color, alpha?, dashLength?, gapLength?, dashOffset?, endColor?, endAlpha?"
        // starting at argIndex. An endColor turns the stroke into a start->end gradient
        ShapeTextureRenderer::StrokeStyle ReadStrokeStyle(Zenith* zenith, i32 argIndex)
        {
            ShapeTextureRenderer::StrokeStyle style;
            style.width = zenith->CheckVal<f32>(argIndex);

            vec3 color = zenith->CheckVal<vec3>(argIndex + 1);
            f32 alpha = zenith->IsNumber(argIndex + 2) ? zenith->Get<f32>(argIndex + 2) : 1.0f;
            style.color = Color(color.x, color.y, color.z, alpha);

            style.dashLength = zenith->IsNumber(argIndex + 3) ? zenith->Get<f32>(argIndex + 3) : 0.0f;
            style.gapLength = zenith->IsNumber(argIndex + 4) ? zenith->Get<f32>(argIndex + 4) : 0.0f;
            style.dashOffset = zenith->IsNumber(argIndex + 5) ? zenith->Get<f32>(argIndex + 5) : 0.0f;

            if (zenith->IsVector(argIndex + 6))
            {
                vec3 endColor = zenith->CheckVal<vec3>(argIndex + 6);
                f32 endAlpha = zenith->IsNumber(argIndex + 7) ? zenith->Get<f32>(argIndex + 7) : style.color.a;
                style.endColor = Color(endColor.x, endColor.y, endColor.z, endAlpha);
                style.useEndColor = true;
            }

            if (zenith->IsNumber(argIndex + 8)) // 0 = round, 1 = butt, 2 = square (single lines only)
            {
                style.capStyle = static_cast<ShapeTextureRenderer::CapStyle>(std::min(zenith->Get<u32>(argIndex + 8), 2u));
            }
            return style;
        }

        // Reads a table of vec points into outPoints; errors on invalid entries
        i32 ReadPointTable(Zenith* zenith, i32 argIndex, vec2* outPoints, i32 maxPoints, i32 minPoints)
        {
            luaL_checktype(zenith->state, argIndex, LUA_TTABLE);
            i32 numPoints = static_cast<i32>(lua_objlen(zenith->state, argIndex));
            if (numPoints < minPoints)
            {
                luaL_error(zenith->state, "Expected a table of at least %d vec points", minPoints);
            }
            if (numPoints > maxPoints)
            {
                luaL_error(zenith->state, "At most %d points are supported", maxPoints);
            }

            for (i32 i = 0; i < numPoints; i++)
            {
                lua_rawgeti(zenith->state, argIndex, i + 1);
                const f32* value = lua_tovector(zenith->state, -1);
                if (value == nullptr)
                {
                    luaL_error(zenith->state, "Point %d is not a vec", i + 1);
                }
                outPoints[i] = vec2(value[0], value[1]);
                lua_pop(zenith->state, 1);
            }
            return numPoints;
        }

        Color ReadColor(Zenith* zenith, i32 argIndex)
        {
            vec3 color = zenith->CheckVal<vec3>(argIndex);
            f32 alpha = zenith->IsNumber(argIndex + 1) ? zenith->Get<f32>(argIndex + 1) : 1.0f;
            return Color(color.x, color.y, color.z, alpha);
        }
    }

    void ShapeSurface::Register(Zenith* zenith)
    {
        LuaMetaTable<ShapeSurface>::Register(zenith, "ShapeSurfaceMetaTable");
        LuaMetaTable<ShapeSurface>::Set(zenith, shapeSurfaceMethods);
    }

    i32 ShapeSurface::GetShapeSurface(Zenith* zenith)
    {
        const char* name = zenith->CheckVal<const char*>(1);
        if (name == nullptr)
        {
            zenith->Push();
            return 1;
        }

        uvec2 size = CheckSurfaceSize(zenith, zenith->CheckVal<vec3>(2));

        ShapeTextureRenderer* shapeTextureRenderer = GetShapeTextureRenderer();

        ShapeTextureRenderer::SurfaceID surfaceID = shapeTextureRenderer->FindSurface(name);
        if (surfaceID != ShapeTextureRenderer::INVALID_SURFACE_ID)
        {
            shapeTextureRenderer->ClearSurface(surfaceID); // Fresh content when a reloaded script re-requests its surface
            shapeTextureRenderer->ResizeSurface(surfaceID, size); // No-op when the size matches
        }
        else
        {
            surfaceID = shapeTextureRenderer->CreateSurface(size, name);
        }

        ShapeSurface* pushSurface = zenith->PushUserData<ShapeSurface>([](void* x) { });
        pushSurface->type = WidgetType::ShapeSurface;
        pushSurface->surfaceID = surfaceID;
        luaL_getmetatable(zenith->state, "ShapeSurfaceMetaTable");
        lua_setmetatable(zenith->state, -2);
        return 1;
    }

    namespace ShapeSurfaceMethods
    {
        i32 Clear(Zenith* zenith, ShapeSurface* surface)
        {
            GetShapeTextureRenderer()->ClearSurface(surface->surfaceID);
            return 0;
        }

        i32 ForceRefresh(Zenith* zenith, ShapeSurface* surface)
        {
            GetShapeTextureRenderer()->ForceRefresh(surface->surfaceID);
            return 0;
        }

        i32 GetSize(Zenith* zenith, ShapeSurface* surface)
        {
            uvec2 size = GetShapeTextureRenderer()->GetSurfaceSize(surface->surfaceID);
            zenith->Push(vec2(size));
            return 1;
        }

        i32 Resize(Zenith* zenith, ShapeSurface* surface)
        {
            uvec2 size = CheckSurfaceSize(zenith, zenith->CheckVal<vec3>(2));
            GetShapeTextureRenderer()->ResizeSurface(surface->surfaceID, size);
            return 0;
        }

        i32 SetClearColor(Zenith* zenith, ShapeSurface* surface)
        {
            Color color = ReadColor(zenith, 2);
            GetShapeTextureRenderer()->SetClearColor(surface->surfaceID, color);
            return 0;
        }

        i32 PushClipRect(Zenith* zenith, ShapeSurface* surface)
        {
            const vec3 min = zenith->CheckVal<vec3>(2);
            const vec3 max = zenith->CheckVal<vec3>(3);
            GetShapeTextureRenderer()->PushClipRect(surface->surfaceID, vec2(min.x, min.y), vec2(max.x, max.y));
            return 0;
        }

        i32 PopClipRect(Zenith* zenith, ShapeSurface* surface)
        {
            GetShapeTextureRenderer()->PopClipRect(surface->surfaceID);
            return 0;
        }

        i32 PushTransform(Zenith* zenith, ShapeSurface* surface)
        {
            const vec3 translation = zenith->CheckVal<vec3>(2);
            f32 scale = zenith->IsNumber(3) ? zenith->Get<f32>(3) : 1.0f;
            f32 rotation = zenith->IsNumber(4) ? zenith->Get<f32>(4) : 0.0f;
            GetShapeTextureRenderer()->PushTransform(surface->surfaceID, vec2(translation.x, translation.y), scale, rotation);
            return 0;
        }

        i32 PopTransform(Zenith* zenith, ShapeSurface* surface)
        {
            GetShapeTextureRenderer()->PopTransform(surface->surfaceID);
            return 0;
        }

        i32 DrawLine(Zenith* zenith, ShapeSurface* surface)
        {
            const vec3 from = zenith->CheckVal<vec3>(2);
            const vec3 to = zenith->CheckVal<vec3>(3);
            ShapeTextureRenderer::StrokeStyle style = ReadStrokeStyle(zenith, 4);

            GetShapeTextureRenderer()->DrawLine(surface->surfaceID, vec2(from.x, from.y), vec2(to.x, to.y), style);
            return 0;
        }

        i32 DrawPolyline(Zenith* zenith, ShapeSurface* surface)
        {
            constexpr i32 MAX_POLYLINE_POINTS = 256;
            vec2 points[MAX_POLYLINE_POINTS];
            // Fewer than 2 points draws nothing rather than erroring; callers commonly build point
            // lists up progressively (e.g. a chart with one sample on its first frame)
            i32 numPoints = ReadPointTable(zenith, 2, points, MAX_POLYLINE_POINTS, 0);
            if (numPoints < 2)
                return 0;

            ShapeTextureRenderer::StrokeStyle style = ReadStrokeStyle(zenith, 3);
            GetShapeTextureRenderer()->DrawPolyline(surface->surfaceID, points, static_cast<u32>(numPoints), style);
            return 0;
        }

        i32 DrawBezier(Zenith* zenith, ShapeSurface* surface)
        {
            const vec3 p0 = zenith->CheckVal<vec3>(2);
            const vec3 c0 = zenith->CheckVal<vec3>(3);
            const vec3 c1 = zenith->CheckVal<vec3>(4);
            const vec3 p1 = zenith->CheckVal<vec3>(5);
            ShapeTextureRenderer::StrokeStyle style = ReadStrokeStyle(zenith, 6);

            GetShapeTextureRenderer()->DrawBezier(surface->surfaceID, vec2(p0.x, p0.y), vec2(c0.x, c0.y), vec2(c1.x, c1.y), vec2(p1.x, p1.y), style);
            return 0;
        }

        // Argument 4 is either a uniform corner radius or a table of four radii
        // { bottomLeft, bottomRight, topLeft, topRight }
        i32 DrawRect(Zenith* zenith, ShapeSurface* surface)
        {
            const vec3 min = zenith->CheckVal<vec3>(2);
            const vec3 max = zenith->CheckVal<vec3>(3);
            Color color = ReadColor(zenith, 5); // color + optional alpha at 6
            f32 rotation = zenith->IsNumber(9) ? zenith->Get<f32>(9) : 0.0f;

            if (lua_istable(zenith->state, 4))
            {
                vec4 cornerRadii = vec4(0.0f);
                for (i32 i = 0; i < 4; i++)
                {
                    lua_rawgeti(zenith->state, 4, i + 1);
                    cornerRadii[i] = static_cast<f32>(lua_tonumber(zenith->state, -1));
                    lua_pop(zenith->state, 1);
                }
                GetShapeTextureRenderer()->DrawRect(surface->surfaceID, vec2(min.x, min.y), vec2(max.x, max.y), color, cornerRadii, rotation);
                return 0;
            }

            f32 cornerRadius = zenith->CheckVal<f32>(4);
            if (zenith->IsVector(7)) // endColor + optional endAlpha: vertical bottom->top gradient
            {
                vec3 endColor = zenith->CheckVal<vec3>(7);
                f32 endAlpha = zenith->IsNumber(8) ? zenith->Get<f32>(8) : color.a;
                GetShapeTextureRenderer()->DrawRectGradient(surface->surfaceID, vec2(min.x, min.y), vec2(max.x, max.y), color, Color(endColor.x, endColor.y, endColor.z, endAlpha), cornerRadius, rotation);
            }
            else
            {
                GetShapeTextureRenderer()->DrawRect(surface->surfaceID, vec2(min.x, min.y), vec2(max.x, max.y), color, cornerRadius, rotation);
            }
            return 0;
        }

        i32 DrawRectOutline(Zenith* zenith, ShapeSurface* surface)
        {
            const vec3 min = zenith->CheckVal<vec3>(2);
            const vec3 max = zenith->CheckVal<vec3>(3);
            f32 cornerRadius = zenith->CheckVal<f32>(4);
            ShapeTextureRenderer::StrokeStyle style = ReadStrokeStyle(zenith, 5); // through arg 13 (capStyle slot; ignored on closed outlines)
            f32 rotation = zenith->IsNumber(14) ? zenith->Get<f32>(14) : 0.0f;

            GetShapeTextureRenderer()->DrawRectOutline(surface->surfaceID, vec2(min.x, min.y), vec2(max.x, max.y), style, cornerRadius, rotation);
            return 0;
        }

        i32 DrawCircle(Zenith* zenith, ShapeSurface* surface)
        {
            const vec3 center = zenith->CheckVal<vec3>(2);
            f32 radius = zenith->CheckVal<f32>(3);
            Color color = ReadColor(zenith, 4); // color + optional alpha at 5

            if (zenith->IsVector(6)) // edgeColor + optional edgeAlpha: radial center->edge gradient
            {
                vec3 edgeColor = zenith->CheckVal<vec3>(6);
                f32 edgeAlpha = zenith->IsNumber(7) ? zenith->Get<f32>(7) : color.a;
                GetShapeTextureRenderer()->DrawCircleGradient(surface->surfaceID, vec2(center.x, center.y), radius, color, Color(edgeColor.x, edgeColor.y, edgeColor.z, edgeAlpha));
            }
            else
            {
                GetShapeTextureRenderer()->DrawCircle(surface->surfaceID, vec2(center.x, center.y), radius, color);
            }
            return 0;
        }

        i32 DrawEllipse(Zenith* zenith, ShapeSurface* surface)
        {
            const vec3 center = zenith->CheckVal<vec3>(2);
            const vec3 radii = zenith->CheckVal<vec3>(3);
            Color color = ReadColor(zenith, 4); // color + optional alpha at 5
            f32 rotation = zenith->IsNumber(6) ? zenith->Get<f32>(6) : 0.0f;

            GetShapeTextureRenderer()->DrawEllipse(surface->surfaceID, vec2(center.x, center.y), vec2(radii.x, radii.y), color, rotation);
            return 0;
        }

        i32 DrawEllipseOutline(Zenith* zenith, ShapeSurface* surface)
        {
            const vec3 center = zenith->CheckVal<vec3>(2);
            const vec3 radii = zenith->CheckVal<vec3>(3);
            ShapeTextureRenderer::StrokeStyle style = ReadStrokeStyle(zenith, 4); // width..dashOffset at 4..9 (endColor unsupported)
            f32 rotation = zenith->IsNumber(10) ? zenith->Get<f32>(10) : 0.0f;

            GetShapeTextureRenderer()->DrawEllipseOutline(surface->surfaceID, vec2(center.x, center.y), vec2(radii.x, radii.y), style, rotation);
            return 0;
        }

        i32 DrawCircleOutline(Zenith* zenith, ShapeSurface* surface)
        {
            const vec3 center = zenith->CheckVal<vec3>(2);
            f32 radius = zenith->CheckVal<f32>(3);
            ShapeTextureRenderer::StrokeStyle style = ReadStrokeStyle(zenith, 4);

            GetShapeTextureRenderer()->DrawCircleOutline(surface->surfaceID, vec2(center.x, center.y), radius, style);
            return 0;
        }

        i32 DrawArc(Zenith* zenith, ShapeSurface* surface)
        {
            const vec3 center = zenith->CheckVal<vec3>(2);
            f32 radius = zenith->CheckVal<f32>(3);
            f32 startAngle = zenith->CheckVal<f32>(4);
            f32 endAngle = zenith->CheckVal<f32>(5);
            ShapeTextureRenderer::StrokeStyle style = ReadStrokeStyle(zenith, 6);

            GetShapeTextureRenderer()->DrawArc(surface->surfaceID, vec2(center.x, center.y), radius, startAngle, endAngle, style);
            return 0;
        }

        i32 DrawPie(Zenith* zenith, ShapeSurface* surface)
        {
            const vec3 center = zenith->CheckVal<vec3>(2);
            f32 radius = zenith->CheckVal<f32>(3);
            f32 startAngle = zenith->CheckVal<f32>(4);
            f32 endAngle = zenith->CheckVal<f32>(5);
            Color color = ReadColor(zenith, 6); // color + optional alpha at 7

            if (zenith->IsVector(8)) // endColor + optional endAlpha: angular gradient along the span
            {
                vec3 endColor = zenith->CheckVal<vec3>(8);
                f32 endAlpha = zenith->IsNumber(9) ? zenith->Get<f32>(9) : color.a;
                GetShapeTextureRenderer()->DrawPieGradient(surface->surfaceID, vec2(center.x, center.y), radius, startAngle, endAngle, color, Color(endColor.x, endColor.y, endColor.z, endAlpha));
            }
            else
            {
                GetShapeTextureRenderer()->DrawPie(surface->surfaceID, vec2(center.x, center.y), radius, startAngle, endAngle, color);
            }
            return 0;
        }

        i32 DrawPolygon(Zenith* zenith, ShapeSurface* surface)
        {
            constexpr i32 MAX_POLYGON_POINTS = 128;
            vec2 points[MAX_POLYGON_POINTS];
            i32 numPoints = ReadPointTable(zenith, 2, points, MAX_POLYGON_POINTS, 3);

            Color color = ReadColor(zenith, 3); // color + optional alpha at 4

            if (lua_istable(zenith->state, 5)) // optional holes: a table of point tables
            {
                std::vector<std::vector<vec2>> holes;
                i32 numHoles = static_cast<i32>(lua_objlen(zenith->state, 5));
                for (i32 holeIndex = 1; holeIndex <= numHoles; holeIndex++)
                {
                    lua_rawgeti(zenith->state, 5, holeIndex);
                    i32 holeTableIndex = lua_gettop(zenith->state);

                    vec2 holePoints[MAX_POLYGON_POINTS];
                    i32 numHolePoints = ReadPointTable(zenith, holeTableIndex, holePoints, MAX_POLYGON_POINTS, 3);
                    holes.emplace_back(holePoints, holePoints + numHolePoints);
                    lua_pop(zenith->state, 1);
                }

                GetShapeTextureRenderer()->DrawPolygonWithHoles(surface->surfaceID, points, static_cast<u32>(numPoints), holes, color);
                return 0;
            }

            GetShapeTextureRenderer()->DrawPolygon(surface->surfaceID, points, static_cast<u32>(numPoints), color);
            return 0;
        }

        i32 DrawGrid(Zenith* zenith, ShapeSurface* surface)
        {
            const vec3 origin = zenith->CheckVal<vec3>(2);
            const vec3 spacing = zenith->CheckVal<vec3>(3);
            f32 lineWidth = zenith->CheckVal<f32>(4);
            Color color = ReadColor(zenith, 5); // color + optional alpha at 6

            u32 majorEvery = zenith->IsNumber(7) ? zenith->Get<u32>(7) : 0;
            f32 majorLineWidth = zenith->IsNumber(8) ? zenith->Get<f32>(8) : lineWidth;
            Color majorColor = zenith->IsVector(9) ? ReadColor(zenith, 9) : color; // majorColor + optional alpha at 10

            GetShapeTextureRenderer()->DrawGrid(surface->surfaceID, vec2(origin.x, origin.y), vec2(spacing.x, spacing.y), lineWidth, color, majorEvery, majorLineWidth, majorColor);
            return 0;
        }

        i32 DrawImage(Zenith* zenith, ShapeSurface* surface)
        {
            const vec3 min = zenith->CheckVal<vec3>(2);
            const vec3 max = zenith->CheckVal<vec3>(3);
            const char* texturePath = zenith->CheckVal<const char*>(4);
            Color tint = zenith->IsVector(5) ? ReadColor(zenith, 5) : Color::White; // tint + optional alpha at 6

            vec2 uvMin = vec2(0.0f, 0.0f);
            vec2 uvMax = vec2(1.0f, 1.0f);
            if (zenith->IsVector(7))
            {
                const vec3 uvMinArg = zenith->CheckVal<vec3>(7);
                uvMin = vec2(uvMinArg.x, uvMinArg.y);
            }
            if (zenith->IsVector(8))
            {
                const vec3 uvMaxArg = zenith->CheckVal<vec3>(8);
                uvMax = vec2(uvMaxArg.x, uvMaxArg.y);
            }
            f32 rotation = zenith->IsNumber(9) ? zenith->Get<f32>(9) : 0.0f;

            ShapeTextureRenderer* shapeTextureRenderer = GetShapeTextureRenderer();
            u32 textureIndex = static_cast<u32>(shapeTextureRenderer->LoadTexture(texturePath));
            shapeTextureRenderer->DrawImage(surface->surfaceID, vec2(min.x, min.y), vec2(max.x, max.y), textureIndex, tint, uvMin, uvMax, rotation);
            return 0;
        }

        i32 DrawRectShadow(Zenith* zenith, ShapeSurface* surface)
        {
            const vec3 min = zenith->CheckVal<vec3>(2);
            const vec3 max = zenith->CheckVal<vec3>(3);
            f32 cornerRadius = zenith->CheckVal<f32>(4);
            f32 blurRadius = zenith->CheckVal<f32>(5);
            Color color = ReadColor(zenith, 6); // color + optional alpha at 7

            vec2 offset = vec2(0.0f, 0.0f);
            if (zenith->IsVector(8))
            {
                const vec3 offsetArg = zenith->CheckVal<vec3>(8);
                offset = vec2(offsetArg.x, offsetArg.y);
            }
            f32 rotation = zenith->IsNumber(9) ? zenith->Get<f32>(9) : 0.0f;

            GetShapeTextureRenderer()->DrawRectShadow(surface->surfaceID, vec2(min.x, min.y), vec2(max.x, max.y), cornerRadius, blurRadius, color, offset, rotation);
            return 0;
        }

        i32 DrawCheckerboard(Zenith* zenith, ShapeSurface* surface)
        {
            const vec3 origin = zenith->CheckVal<vec3>(2);
            const vec3 cellSize = zenith->CheckVal<vec3>(3);
            const vec3 colorA = zenith->CheckVal<vec3>(4);
            const vec3 colorB = zenith->CheckVal<vec3>(5);
            f32 alphaA = zenith->IsNumber(6) ? zenith->Get<f32>(6) : 1.0f;
            f32 alphaB = zenith->IsNumber(7) ? zenith->Get<f32>(7) : 1.0f;

            GetShapeTextureRenderer()->DrawCheckerboard(surface->surfaceID, vec2(origin.x, origin.y), vec2(cellSize.x, cellSize.y), Color(colorA.x, colorA.y, colorA.z, alphaA), Color(colorB.x, colorB.y, colorB.z, alphaB));
            return 0;
        }
    }
}
