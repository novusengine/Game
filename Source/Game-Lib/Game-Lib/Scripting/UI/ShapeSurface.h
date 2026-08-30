#pragma once
#include "Game-Lib/Rendering/Shape/ShapeTextureRenderer.h"
#include "Game-Lib/Scripting/UI/Widget.h"

#include <Base/Types.h>

#include <Scripting/Defines.h>
#include <Scripting/LuaMethodTable.h>

namespace Scripting::UI
{
    // Lua handle to a ShapeTextureRenderer surface. Starts with WidgetType (like Widget) so
    // Panel::SetBackground can discriminate the userdata before casting. Surfaces are cached by
    // name and survive script reloads (their textures cannot be destroyed); reusing a name clears
    // the surface's retained shapes so a re-run script starts from a clean texture.
    //
    // Draw methods take UI-convention coordinates (bottom-left origin, +Y up, angles CCW from
    // +X), which the renderer shares — coordinates pass through unchanged.
    struct ShapeSurface
    {
    public:
        static void Register(Zenith* zenith);

        // UI.GetShapeSurface(name, size) -> ShapeSurface
        static i32 GetShapeSurface(Zenith* zenith);

    public:
        WidgetType type;
        ShapeTextureRenderer::SurfaceID surfaceID;
    };

    namespace ShapeSurfaceMethods
    {
        i32 Clear(Zenith* zenith, ShapeSurface* surface);
        i32 ForceRefresh(Zenith* zenith, ShapeSurface* surface);
        i32 GetSize(Zenith* zenith, ShapeSurface* surface);
        i32 Resize(Zenith* zenith, ShapeSurface* surface);
        i32 SetClearColor(Zenith* zenith, ShapeSurface* surface);
        i32 PushClipRect(Zenith* zenith, ShapeSurface* surface);
        i32 PopClipRect(Zenith* zenith, ShapeSurface* surface);
        i32 PushTransform(Zenith* zenith, ShapeSurface* surface);
        i32 PopTransform(Zenith* zenith, ShapeSurface* surface);

        i32 DrawLine(Zenith* zenith, ShapeSurface* surface);
        i32 DrawPolyline(Zenith* zenith, ShapeSurface* surface);
        i32 DrawBezier(Zenith* zenith, ShapeSurface* surface);
        i32 DrawRect(Zenith* zenith, ShapeSurface* surface);
        i32 DrawRectOutline(Zenith* zenith, ShapeSurface* surface);
        i32 DrawCircle(Zenith* zenith, ShapeSurface* surface);
        i32 DrawCircleOutline(Zenith* zenith, ShapeSurface* surface);
        i32 DrawEllipse(Zenith* zenith, ShapeSurface* surface);
        i32 DrawEllipseOutline(Zenith* zenith, ShapeSurface* surface);
        i32 DrawArc(Zenith* zenith, ShapeSurface* surface);
        i32 DrawPie(Zenith* zenith, ShapeSurface* surface);
        i32 DrawPolygon(Zenith* zenith, ShapeSurface* surface);
        i32 DrawGrid(Zenith* zenith, ShapeSurface* surface);
        i32 DrawCheckerboard(Zenith* zenith, ShapeSurface* surface);
        i32 DrawImage(Zenith* zenith, ShapeSurface* surface);
        i32 DrawRectShadow(Zenith* zenith, ShapeSurface* surface);
    }

    static LuaRegister<ShapeSurface> shapeSurfaceMethods[] =
    {
        { "Clear", ShapeSurfaceMethods::Clear },
        { "ForceRefresh", ShapeSurfaceMethods::ForceRefresh },
        { "GetSize", ShapeSurfaceMethods::GetSize },
        { "Resize", ShapeSurfaceMethods::Resize },
        { "SetClearColor", ShapeSurfaceMethods::SetClearColor },
        { "PushClipRect", ShapeSurfaceMethods::PushClipRect },
        { "PopClipRect", ShapeSurfaceMethods::PopClipRect },
        { "PushTransform", ShapeSurfaceMethods::PushTransform },
        { "PopTransform", ShapeSurfaceMethods::PopTransform },

        { "DrawLine", ShapeSurfaceMethods::DrawLine },
        { "DrawPolyline", ShapeSurfaceMethods::DrawPolyline },
        { "DrawBezier", ShapeSurfaceMethods::DrawBezier },
        { "DrawRect", ShapeSurfaceMethods::DrawRect },
        { "DrawRectOutline", ShapeSurfaceMethods::DrawRectOutline },
        { "DrawCircle", ShapeSurfaceMethods::DrawCircle },
        { "DrawCircleOutline", ShapeSurfaceMethods::DrawCircleOutline },
        { "DrawEllipse", ShapeSurfaceMethods::DrawEllipse },
        { "DrawEllipseOutline", ShapeSurfaceMethods::DrawEllipseOutline },
        { "DrawArc", ShapeSurfaceMethods::DrawArc },
        { "DrawPie", ShapeSurfaceMethods::DrawPie },
        { "DrawPolygon", ShapeSurfaceMethods::DrawPolygon },
        { "DrawGrid", ShapeSurfaceMethods::DrawGrid },
        { "DrawCheckerboard", ShapeSurfaceMethods::DrawCheckerboard },
        { "DrawImage", ShapeSurfaceMethods::DrawImage },
        { "DrawRectShadow", ShapeSurfaceMethods::DrawRectShadow }
    };
}
