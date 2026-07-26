#include "PerfHandler.h"
#include "Game-Lib/Application/EnttRegistries.h"
#include "Game-Lib/ECS/Singletons/EngineStats.h"
#include "Game-Lib/Rendering/CullingResources.h"
#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Rendering/Debug/DebugRenderer.h"
#include "Game-Lib/Rendering/Debug/JoltDebugRenderer.h"
#include "Game-Lib/Rendering/Liquid/LiquidRenderer.h"
#include "Game-Lib/Rendering/Model/ModelRenderer.h"
#include "Game-Lib/Rendering/Terrain/TerrainRenderer.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Base/CVarSystem/CVarSystem.h>
#include <Base/Math/Color.h>
#include <Base/Util/CPUInfo.h>

#include <Renderer/Renderer.h>
#include <Renderer/RenderSettings.h>

#include <glm/glm.hpp>

#include <Scripting/LuaManager.h>
#include <Scripting/Zenith.h>

#include <entt/entt.hpp>
#include <lualib.h>

namespace Scripting::Perf
{
    static const i32 AVG_FRAMES = 120;
    static const u32 MAIN_VIEW = 0;

    // The five frame-time series the graph plots (mirrors the ImGui PerformanceDiagnostics graph). One
    // source of truth shared by DrawFrameGraph (the draw) and GetGraphSeries (the Lua legend).
    struct GraphSeries { const char* label; Color color; };
    static const GraphSeries GRAPH_SERIES[5] =
    {
        { "Total",    Color(0.25f, 0.55f, 0.95f) },
        { "Update",   Color(0.95f, 0.50f, 0.20f) },
        { "Render",   Color(0.35f, 0.80f, 0.40f) },
        { "CPU Wait", Color(0.90f, 0.80f, 0.30f) },
        { "GPU",      Color(0.75f, 0.45f, 0.90f) },
    };

    static f32 SeriesMs(const ECS::Singletons::FrameTimes& f, i32 series)
    {
        switch (series)
        {
            case 0:  return f.deltaTimeS * 1000.0f;
            case 1:  return f.simulationFrameTimeS * 1000.0f;
            case 2:  return f.renderFrameTimeS * 1000.0f;
            case 3:  return f.renderWaitTimeS * 1000.0f;
            default: return f.gpuFrameTimeMS;
        }
    }

    void PerfHandler::Register(Zenith* zenith)
    {
        LuaManager* luaManager = ServiceLocator::GetLuaManager();
        const bool inDeveloperMode = luaManager && luaManager->IsDeveloperMode();
        const Scripting::LuaMethodFlags excludeFlags = inDeveloperMode
            ? Scripting::LuaMethodFlags::None
            : Scripting::LuaMethodFlags::DeveloperOnly;

        LuaMethodTable::Set(zenith, perfGlobalMethods, "Perf", excludeFlags);
    }

    static ECS::Singletons::EngineStats& GetStats()
    {
        EnttRegistries* registries = ServiceLocator::GetEnttRegistries();
        return registries->gameRegistry->ctx().get<ECS::Singletons::EngineStats>();
    }

    i32 PerfHandler::GetCPUName(Zenith* zenith)
    {
        zenith->Push(CPUInfo::Get().GetPrettyName().c_str());
        return 1;
    }

    i32 PerfHandler::GetGPUName(Zenith* zenith)
    {
        zenith->Push(ServiceLocator::GetGameRenderer()->GetGPUName().c_str());
        return 1;
    }

    // The graph's series labels + colors, in draw order, so the Lua legend matches DrawFrameGraph exactly.
    i32 PerfHandler::GetGraphSeries(Zenith* zenith)
    {
        zenith->CreateTable();
        for (i32 i = 0; i < 5; i++)
        {
            zenith->CreateTable();
            zenith->AddTableField("label", GRAPH_SERIES[i].label);
            zenith->AddTableField("r", GRAPH_SERIES[i].color.r);
            zenith->AddTableField("g", GRAPH_SERIES[i].color.g);
            zenith->AddTableField("b", GRAPH_SERIES[i].color.b);
            zenith->SetTableKey(i + 1);
        }
        return 1;
    }

    i32 PerfHandler::GetShadowCascadeNum(Zenith* zenith)
    {
        i32* value = CVarSystem::Get()->GetIntCVar(CVarCategory::Client | CVarCategory::Rendering, "shadowCascadeNum"_h);
        zenith->Push(value ? *value : 0);
        return 1;
    }

    i32 PerfHandler::GetFrameStats(Zenith* zenith)
    {
        ECS::Singletons::FrameTimes avg = GetStats().AverageFrame(AVG_FRAMES);

        zenith->CreateTable();
        zenith->AddTableField("deltaMs", avg.deltaTimeS * 1000.0f);
        zenith->AddTableField("simMs", avg.simulationFrameTimeS * 1000.0f);
        zenith->AddTableField("renderMs", avg.renderFrameTimeS * 1000.0f);
        zenith->AddTableField("waitMs", avg.renderWaitTimeS * 1000.0f);
        zenith->AddTableField("gpuMs", avg.gpuFrameTimeMS);
        zenith->AddTableField("fps", avg.deltaTimeS > 0.0f ? 1.0f / avg.deltaTimeS : 0.0f);
        return 1;
    }

    i32 PerfHandler::GetRenderPasses(Zenith* zenith)
    {
        ECS::Singletons::EngineStats& stats = GetStats();
        Renderer::Renderer* renderer = ServiceLocator::GetGameRenderer()->GetRenderer();

        zenith->CreateTable();
        i32 index = 0;
        for (Renderer::TimeQueryID queryID : renderer->GetFrameTimeQueries())
        {
            const std::string& name = renderer->GetTimeQueryName(queryID);
            f32 averageMS = 0.0f;
            stats.AverageNamed(name, AVG_FRAMES, averageMS);

            zenith->CreateTable();
            zenith->AddTableField("name", name.c_str());
            zenith->AddTableField("ms", averageMS);
            zenith->SetTableKey(++index);
        }
        return 1;
    }

    // One subsystem entry: total / surviving-occluder / surviving-geometry, for both draw-calls and
    // triangles (main view). showOccluder gates whether the Occluders row is meaningful (two-step culling).
    static void PushCulling(Zenith* zenith, i32& index, const char* name, bool showOccluder,
                            u32 dcTotal, u32 dcOccluder, u32 dcGeometry,
                            u32 triTotal, u32 triOccluder, u32 triGeometry)
    {
        zenith->CreateTable();
        zenith->AddTableField("name", name);
        zenith->AddTableField("showOccluder", showOccluder);
        zenith->AddTableField("dcTotal", dcTotal);
        zenith->AddTableField("dcOccluder", dcOccluder);
        zenith->AddTableField("dcGeometry", dcGeometry);
        zenith->AddTableField("triTotal", triTotal);
        zenith->AddTableField("triOccluder", triOccluder);
        zenith->AddTableField("triGeometry", triGeometry);
        zenith->SetTableKey(++index);
    }

    static void PushCullingResources(Zenith* zenith, i32& index, const char* name, CullingResourcesBase& cr)
    {
        PushCulling(zenith, index, name, cr.HasSupportForTwoStepCulling(),
            cr.GetNumInstances(), cr.GetNumSurvivingOccluderInstances(), cr.GetNumSurvivingInstances(MAIN_VIEW),
            cr.GetNumTriangles(), cr.GetNumSurvivingOccluderTriangles(), cr.GetNumSurvivingTriangles(MAIN_VIEW));
    }

    // Per-subsystem culling counts for the main view (mirrors the ImGui panel's DrawCalls + Triangles
    // sections). Terrain has its own getters; the rest expose CullingResourcesBase. Lua sums the grand
    // total and formats the survived/total percentages.
    i32 PerfHandler::GetCullingStats(Zenith* zenith)
    {
        GameRenderer* gameRenderer = ServiceLocator::GetGameRenderer();
        TerrainRenderer* terrainRenderer = gameRenderer->GetTerrainRenderer();
        ModelRenderer* modelRenderer = gameRenderer->GetModelRenderer();
        LiquidRenderer* liquidRenderer = gameRenderer->GetLiquidRenderer();
        JoltDebugRenderer* joltDebugRenderer = gameRenderer->GetJoltDebugRenderer();

        zenith->CreateTable();
        i32 index = 0;

        PushCulling(zenith, index, "Terrain", true,
            terrainRenderer->GetNumDrawCalls(), terrainRenderer->GetNumOccluderDrawCalls(MAIN_VIEW), terrainRenderer->GetNumSurvivingDrawCalls(MAIN_VIEW),
            terrainRenderer->GetNumTriangles(), terrainRenderer->GetNumOccluderTriangles(MAIN_VIEW), terrainRenderer->GetNumSurvivingGeometryTriangles(MAIN_VIEW));

        PushCullingResources(zenith, index, "Model (O)", modelRenderer->GetOpaqueCullingResources());
        PushCullingResources(zenith, index, "Model (T)", modelRenderer->GetTransparentCullingResources());
        PushCullingResources(zenith, index, "Liquid", liquidRenderer->GetCullingResources());
        PushCullingResources(zenith, index, "Jolt Debug Indexed", joltDebugRenderer->GetIndexedCullingResources());
        PushCullingResources(zenith, index, "Jolt Debug", joltDebugRenderer->GetCullingResources());
        return 1;
    }

    // Draws the frame-time history as an immediate-mode polyline via the 2D debug renderer, filling the
    // given rect (in UI world pixels -- the same space GetWorldPosition/the mouse use). This bypasses the
    // retained CanvasRenderer entirely (no widgets), so it costs no per-frame widget churn. Coords are
    // normalized by render size to match DrawBox2D (0..1 screen fraction; the Debug2D shader maps x*2-1,
    // y*2-1, no flip). Must be called every frame while the panel is visible.
    i32 PerfHandler::DrawFrameGraph(Zenith* zenith)
    {
        f32 left = zenith->CheckVal<f32>(1);
        f32 top  = zenith->CheckVal<f32>(2);
        f32 width = zenith->CheckVal<f32>(3);
        f32 height = zenith->CheckVal<f32>(4);

        if (width <= 0.0f || height <= 0.0f)
            return 0;

        ECS::Singletons::EngineStats& stats = GetStats();
        const size_t sampleCount = stats.frameStats.size();
        if (sampleCount < 2)
            return 0;

        DebugRenderer* debugRenderer = ServiceLocator::GetGameRenderer()->GetDebugRenderer();

        // The graph rect comes in UI/canvas (reference) space; normalize by the UI reference size so the
        // line maps to NDC exactly like the CanvasRenderer maps the panel, keeping them aligned.
        const vec2 refSize = vec2(static_cast<f32>(Renderer::Settings::UI_REFERENCE_WIDTH),
                                  static_cast<f32>(Renderer::Settings::UI_REFERENCE_HEIGHT));

        const f32 maxMs = 33.0f; // 0..33 ms Y range (matches the ImGui graph); 30 FPS is the top.

        // UI world-Y is up-positive, so a larger py renders higher on screen -> higher ms goes UP.
        auto point = [&](size_t i, i32 series) -> vec2
        {
            f32 px = left + (static_cast<f32>(i) / static_cast<f32>(sampleCount - 1)) * width;
            f32 ms = SeriesMs(stats.frameStats[sampleCount - 1 - i], series);
            f32 frac = glm::min(ms / maxMs, 1.0f);
            f32 py = top + frac * height;
            return vec2(px, py) / refSize;
        };

        // 60 FPS (16.67 ms) reference gridline.
        {
            f32 py = top + glm::min((1000.0f / 60.0f) / maxMs, 1.0f) * height;
            debugRenderer->DrawLine2D(vec2(left, py) / refSize, vec2(left + width, py) / refSize, Color(0.35f, 0.35f, 0.45f, 1.0f));
        }

        // frameStats front == newest; draw oldest -> newest, left -> right. One polyline per series.
        for (i32 series = 0; series < 5; series++)
        {
            const Color& color = GRAPH_SERIES[series].color;
            for (size_t i = 0; i + 1 < sampleCount; i++)
            {
                debugRenderer->DrawLine2D(point(i, series), point(i + 1, series), color);
            }
        }
        return 0;
    }
}
