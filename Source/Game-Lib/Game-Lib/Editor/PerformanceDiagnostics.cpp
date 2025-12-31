#include "PerformanceDiagnostics.h"

#include <Base/Util/CPUInfo.h>
#include <Base/CVarSystem/CVarSystemPrivate.h>

#include <Game-Lib/Util/ServiceLocator.h>
#include <Game-Lib/Rendering/GameRenderer.h>
#include <Game-Lib/Rendering/Model/ModelRenderer.h>
#include <Game-Lib/Rendering/Terrain/TerrainRenderer.h>
#include <Game-Lib/Rendering/Liquid/LiquidRenderer.h>
#include <Game-Lib/Rendering/Debug/JoltDebugRenderer.h>
#include <Game-Lib/Application/EnttRegistries.h>
#include <Game-Lib/ECS/Singletons/EngineStats.h>
#include <Game-Lib/Util/ImguiUtil.h>

#include <entt/entt.hpp>
#include <imgui/imgui.h>
#include <imgui/implot.h>

#include <string>

namespace Editor
{
    PerformanceDiagnostics::PerformanceDiagnostics()
        : BaseEditor(GetName())
    {

    }

    void PerformanceDiagnostics::OnModeUpdate(bool mode)
    {
        SetIsVisible(mode);
    }

    void PerformanceDiagnostics::DrawImGui()
    {
        GameRenderer* gameRenderer = ServiceLocator::GetGameRenderer();

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        ECS::Singletons::EngineStats& stats = registry->ctx().get<ECS::Singletons::EngineStats>();

        ECS::Singletons::FrameTimes average = stats.AverageFrame(240);

        TerrainRenderer* terrainRenderer = gameRenderer->GetTerrainRenderer();

        // Draw hardware info
        CPUInfo cpuInfo = CPUInfo::Get();
        Renderer::Renderer* renderer = gameRenderer->GetRenderer();

        const std::string& cpuName = cpuInfo.GetPrettyName();
        const std::string& gpuName = gameRenderer->GetGPUName();

        if (ImGui::Begin(GetName(), &IsVisible()))
        {
            static bool enableStretching = true;

            if (OpenMenu("Settings"))
            {
                if (ImGui::BeginMenu("View"))
                {
                    ImGui::Checkbox("Surviving DrawCalls", &_showSurvivingDrawCalls);
                    ImGui::Checkbox("Surviving Triangle", &_showSurvivingTriangle);
                    ImGui::Checkbox("Frame Times", &_showFrameTime);
                    ImGui::Checkbox("Render Pass", &_showRenderPass);
                    ImGui::Checkbox("Frame Graph", &_showFrameGraph);

                    ImGui::EndMenu();
                }

                ImGui::Checkbox("Enable Stretching", &enableStretching);
                CloseMenu();
            }

            ImGui::Text("CPU: %s", cpuName.c_str());
            if (IsHorizontal())
                ImGui::SameLine();
            ImGui::Text("GPU: %s", gpuName.c_str());

            const std::string rightHeaderText = "Survived / Total (%)";
            static ImGuiTableFlags flags = ImGuiTableFlags_SizingFixedFit /*| ImGuiTableFlags_BordersOuter*/ | ImGuiTableFlags_BordersV | ImGuiTableFlags_BordersH | ImGuiTableFlags_ContextMenuInBody;

            u32 numCascades = *CVarSystem::Get()->GetIntCVar(CVarCategory::Client | CVarCategory::Rendering, "shadowCascadeNum"_h);

            ImGui::Spacing();

            if (_showSurvivingDrawCalls || _showSurvivingTriangle)
                ImGui::Checkbox("Surviving information only for main view", &_drawCallStatsOnlyForMainView);

            const std::vector<bool> sectionOrder =
            {
                _showSurvivingDrawCalls,
                _showSurvivingTriangle,
                _showFrameTime,
                _showRenderPass,
                _showFrameGraph
            };

            i32 howManySectionToDraw = 0;
            for (const auto& showSection : sectionOrder)
                howManySectionToDraw += static_cast<i32>(showSection);

            if (howManySectionToDraw == 0)
            {
                ImGui::End();
                return;
            }

            if (IsHorizontal())
            {
                std::vector<f32> newWidthProportions = (enableStretching) ?
                    CalculateProportions(_sectionParamHorizontal, sectionOrder) : _sectionParamHorizontal;

                ProportionsFix(newWidthProportions);

                if (howManySectionToDraw > 1)
                {
                    f32 heightConstraint = ImGui::GetContentRegionAvail().y * 0.9f;
                    ImGui::Columns(howManySectionToDraw, "PerformanceDiagnosticsColumn", false);

                    i32 i = 0;
                    i32 n = 0;
                    for (const auto& showSection : sectionOrder)
                    {
                        if (!showSection)
                        {
                            i++;
                            continue;
                        }

                        ImGui::SetColumnWidth(n, ImGui::GetWindowWidth() * newWidthProportions[i]);
                        switch (i)
                        {
                        case 0:
                            DrawSurvivingDrawCalls(heightConstraint, rightHeaderText, numCascades);
                            break;
                        case 1:
                            DrawSurvivingTriangles(heightConstraint, rightHeaderText, numCascades);
                            break;
                        case 2:
                            DrawFrameTimes(heightConstraint, average, flags);
                            break;
                        case 3:
                            DrawRenderPass(heightConstraint, renderer, stats, flags);
                            break;
                        case 4:
                            DrawFrameTimesGraph(heightConstraint, stats);
                            break;
                        default:
                            break;
                        }

                        i++;
                        n++;

                        if (n == (howManySectionToDraw))
                        {
                            ImGui::Columns(1);
                        }
                        else
                        {
                            ImGui::NextColumn();
                        }
                    }
                }
                else if (howManySectionToDraw == 1)
                {
                    f32 heightConstraint = ImGui::GetContentRegionAvail().y * 0.9f;
                    ImGui::Columns(howManySectionToDraw, "PerformanceDiagnosticsColumn", false);

                    i32 i = 0;
                    for (const auto& showSection : sectionOrder)
                    {
                        if (!showSection)
                        {
                            i++;
                            continue;
                        }

                        f32 widthConstraint = ImGui::GetWindowWidth() * newWidthProportions[i];
                        switch (i)
                        {
                        case 0:
                            DrawSurvivingDrawCalls(heightConstraint, rightHeaderText, numCascades, widthConstraint);
                            break;
                        case 1:
                            DrawSurvivingTriangles(heightConstraint, rightHeaderText, numCascades, widthConstraint);
                            break;
                        case 2:
                            DrawFrameTimes(heightConstraint, average, flags, widthConstraint);
                            break;
                        case 3:
                            DrawRenderPass(heightConstraint, renderer, stats, flags, widthConstraint);
                            break;
                        case 4:
                            DrawFrameTimesGraph(heightConstraint, stats, widthConstraint);
                            break;
                        default:
                            break;
                        }
                    }
                }
            }
            else // IsVertical()
            {
                bool canDisplayByColumn = (ImGui::GetContentRegionAvail().x > 450);

                // force displaying without column
                if (((_showSurvivingDrawCalls != _showSurvivingTriangle) && (_showFrameTime != _showRenderPass))
                    || howManySectionToDraw == 1)
                {
                    canDisplayByColumn = false;
                }

                if (canDisplayByColumn) // column display
                {
                    // fakeOrder is to calculate the real proportions if only half item in a row
                    // is showed, like that he can stretch horizontally, this is an easy fix
                    std::vector<bool> fakeOrder = {false, false, false, false, true};
                    if (_showSurvivingTriangle || _showSurvivingDrawCalls)
                        fakeOrder[0] = true;
                    if (_showFrameTime || _showRenderPass)
                        fakeOrder[2] = true;

                    std::vector<f32> newHeightProportions = (enableStretching) ?
                        CalculateProportions(_sectionParamVerticalMultiColumn, fakeOrder) : _sectionParamVerticalMultiColumn;

                    if (enableStretching)
                    {
                        newHeightProportions[1] = newHeightProportions[0];
                        newHeightProportions[3] = newHeightProportions[2];
                    }

                    ProportionsFix(newHeightProportions);

                    for (f32& height : newHeightProportions)
                    {
                        height *= ImGui::GetContentRegionAvail().y;
                    }

                    if (ImGui::BeginTable("##PerformanceDiagnosticMultipleColumn", 2))
                    {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        DrawSurvivingDrawCalls(newHeightProportions[0], rightHeaderText, numCascades);
                        ImGui::TableSetColumnIndex(1);
                        DrawSurvivingTriangles(newHeightProportions[1], rightHeaderText, numCascades);

                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        DrawFrameTimes(newHeightProportions[2], average, flags);
                        ImGui::TableSetColumnIndex(1);
                        DrawRenderPass(newHeightProportions[3], renderer, stats, flags);

                        ImGui::EndTable();
                    }

                    DrawFrameTimesGraph(newHeightProportions[4], stats);
                }
                else // row display
                {
                    std::vector<f32> newHeightProportions = (enableStretching) ?
                        CalculateProportions(_sectionParamVertical, sectionOrder) : _sectionParamVertical;

                    ProportionsFix(newHeightProportions);

                    for (f32& height : newHeightProportions)
                    {
                        height *= ImGui::GetContentRegionAvail().y;
                    }

                    DrawSurvivingDrawCalls(newHeightProportions[0], rightHeaderText, numCascades);
                    DrawSurvivingTriangles(newHeightProportions[1], rightHeaderText, numCascades);
                    DrawFrameTimes(newHeightProportions[2], average, flags);
                    DrawRenderPass(newHeightProportions[3], renderer, stats, flags);
                    DrawFrameTimesGraph(newHeightProportions[4], stats);
                }
            }
        }

        ImGui::End();
    }

    f32 PerformanceDiagnostics::CalculateTotalParam(const std::vector<f32>& params, const std::vector<bool>& shownItems)
    {
        f32 totalWidth = 0.0f;
        for (i32 i = 0; i < shownItems.size(); ++i)
        {
            if (shownItems[i])
            {
                totalWidth += params[i];
            }
        }
        return totalWidth;
    }

    std::vector<f32> PerformanceDiagnostics::CalculateProportions(const std::vector<f32>& params, const std::vector<bool>& shownItems)
    {
        std::vector<f32> widthProportions(shownItems.size(), 0.0f);

        f32 totalWidth = CalculateTotalParam(params, shownItems);

        if (totalWidth > 0.0f)
        {
            for (i32 i = 0; i < shownItems.size(); ++i)
            {
                if (shownItems[i])
                {
                    widthProportions[i] = params[i] / totalWidth;
                }
            }
        }

        return widthProportions;
    }

    void PerformanceDiagnostics::ProportionsFix(std::vector<f32>& proportions)
    {
        for (f32& val : proportions)
        {
            val = (val == 0.f) ? 0.1f : val;
        }
    }

    void PerformanceDiagnostics::DrawSurvivingDrawCalls(f32 constraint, const std::string& text, u32 numCascades, f32 widthConstraint)
    {
        if (!_showSurvivingDrawCalls)
            return;

        ImGui::SetNextWindowSizeConstraints(ImVec2(-1.f, -1.f), ImVec2(widthConstraint, constraint));
        if (ImGui::BeginChild("##SurvivingInstances", ImVec2(0, (constraint < 0.f) ? 0 : constraint), true, ImGuiWindowFlags_HorizontalScrollbar))
        {
            f32 textWidth = ImGui::CalcTextSize(text.c_str()).x;
            f32 windowWidth = ImGui::GetWindowContentRegionMax().x;
            f32 textPos = windowWidth - textWidth;

            ImGui::Text("Instances");
            ImGui::SameLine();
            if (textPos > ImGui::GetCursorPosX())
            {
                ImGui::SameLine(textPos);
            }
            else
            {
                ImGui::NewLine();
                ImGui::SetCursorPosX(textPos);
            }

            ImGui::Text("%s", text.c_str());
            ImGui::Separator();

            u32 totalDrawCalls = 0;
            u32 totalDrawCallsSurvived = 0;

            // Main view
            DrawCullingDrawCallStatsView(0, textPos, totalDrawCalls, totalDrawCallsSurvived);

            if (!_drawCallStatsOnlyForMainView)
            {
                for (u32 i = 1; i < numCascades + 1; i++)
                {
                    DrawCullingDrawCallStatsView(i, textPos, totalDrawCalls, totalDrawCallsSurvived);
                }
            }

            // Always draw Total, if we are collapsed it will go on the collapsable header
            DrawCullingStatsEntry("Total", totalDrawCalls, totalDrawCallsSurvived);

            ImGui::EndChild();
        }
    }

    void PerformanceDiagnostics::DrawSurvivingTriangles(f32 constraint, const std::string& text, u32 numCascades, f32 widthConstraint)
    {
        if (!_showSurvivingTriangle)
            return;

        ImGui::SetNextWindowSizeConstraints(ImVec2(-1.f, -1.f), ImVec2(widthConstraint, constraint));
        if (ImGui::BeginChild("##SurvivingTriangles", ImVec2(0, (constraint < 0.f) ? 0 : constraint), true, ImGuiWindowFlags_HorizontalScrollbar))
        {
            f32 textWidth = ImGui::CalcTextSize(text.c_str()).x;
            f32 windowWidth = ImGui::GetWindowContentRegionMax().x;
            f32 textPos = windowWidth - textWidth;

            ImGui::Text("Triangles");
            ImGui::SameLine();
            if (textPos > ImGui::GetCursorPosX())
            {
                ImGui::SameLine(textPos);
            }
            else
            {
                ImGui::NewLine();
                ImGui::SetCursorPosX(textPos);
            }

            ImGui::Text("%s", text.c_str());
            ImGui::Separator();

            u32 totalTriangles = 0;
            u32 totalTrianglesSurvived = 0;

            // Main view
            DrawCullingTriangleStatsView(0, textPos, totalTriangles, totalTrianglesSurvived);

            if (!_drawCallStatsOnlyForMainView)
            {
                for (u32 i = 1; i < numCascades + 1; i++)
                {
                    DrawCullingTriangleStatsView(i, textPos, totalTriangles, totalTrianglesSurvived);
                }
            }

            DrawCullingStatsEntry("Total", totalTriangles, totalTrianglesSurvived);

            ImGui::EndChild();
        }
    }

    void PerformanceDiagnostics::DrawFrameTimes(f32 constraint, const ECS::Singletons::FrameTimes& average, const ImGuiTableFlags& flags, f32 widthConstraint)
    {
        if (!_showFrameTime)
            return;

        ImGui::SetNextWindowSizeConstraints(ImVec2(-1.f, -1.f), ImVec2(widthConstraint, constraint));
        if (ImGui::BeginChild("##FrameTimes", ImVec2(0, (constraint < 0.f) ? 0 : constraint), true, ImGuiWindowFlags_HorizontalScrollbar))
        {
            ImGui::Text("Frametimes (ms)");
            if (ImGui::BeginTable("frametimes", 2, flags))
            {
                ImGui::TableNextColumn();
                ImGui::Text("Total");
                ImGui::TableNextColumn();
                ImGui::Text("%.3f", average.deltaTimeS * 1000);
                ImGui::TableNextColumn();

                ImGui::Text("   Update");
                ImGui::TableNextColumn();
                ImGui::Text("%.3f", average.simulationFrameTimeS * 1000);
                ImGui::TableNextColumn();

                ImGui::Text("   Render CPU");
                ImGui::TableNextColumn();
                ImGui::Text("%.3f", average.renderFrameTimeS * 1000);
                ImGui::TableNextColumn();

                ImGui::Text("       CPU wait for GPU");
                ImGui::TableNextColumn();
                ImGui::Text("%.3f", average.renderWaitTimeS * 1000);
                ImGui::TableNextColumn();

                // Separate GPU from Total above since it does not add to it (it would be a part of CPU wait for GPU)
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(200, 200, 200, 255));
                ImGui::Dummy(vec2(0, 1));
                ImGui::TableNextColumn();
                ImGui::Dummy(vec2(0, 1));
                ImGui::TableNextColumn();

                ImGui::Text("GPU frame time");
                ImGui::TableNextColumn();
                ImGui::Text("%.3f", average.gpuFrameTimeMS);
                ImGui::TableNextColumn();

                ImGui::EndTable();
            }

            ImGui::EndChild();
        }
    }

    void PerformanceDiagnostics::DrawRenderPass(f32 constraint, Renderer::Renderer* renderer, ECS::Singletons::EngineStats& stats, const ImGuiTableFlags& flags, f32 widthConstraint)
    {
        if (!_showRenderPass)
            return;

        ImGui::SetNextWindowSizeConstraints(ImVec2(-1.f, -1.f), ImVec2(widthConstraint, constraint));
        if (ImGui::BeginChild("##RenderPass", ImVec2(0, (constraint < 0.f) ? 0 : constraint), true, ImGuiWindowFlags_HorizontalScrollbar))
        {
            const std::vector<Renderer::TimeQueryID> frameTimeQueries = renderer->GetFrameTimeQueries();

            if (frameTimeQueries.size() > 0)
            {
                ImGui::Text("Render Passes (GPU)");
                if (ImGui::BeginTable("passtimes", 2, flags))
                {
                    for (u32 i = 0; i < frameTimeQueries.size(); i++)
                    {
                        const std::string& name = renderer->GetTimeQueryName(frameTimeQueries[i]);

                        f32 averageMS = 0.0f;
                        if (stats.AverageNamed(name, 240, averageMS))
                        {
                            ImGui::TableNextColumn();
                            ImGui::Text("%s", name.c_str());
                            ImGui::TableNextColumn();
                            ImGui::Text("%.3f", averageMS);
                        }
                    }

                    ImGui::EndTable();
                }
            }

            ImGui::EndChild();
        }
    }

    void PerformanceDiagnostics::DrawFrameTimesGraph(f32 constraint, const ECS::Singletons::EngineStats& stats, f32 widthConstraint)
    {
        if (!_showFrameGraph)
            return;

        ImGui::SetNextWindowSizeConstraints(ImVec2(-1.f, -1.f), ImVec2(widthConstraint, constraint));
        if (ImGui::BeginChild("##FrameTimseGraph", ImVec2(0, (constraint < 0.f) ? 0 : constraint), true, ImGuiWindowFlags_HorizontalScrollbar))
        {
            f32 widthAvailable = ImGui::GetContentRegionAvail().x;
            widthAvailable = std::max(300.f, widthAvailable);

            // Read the frame buffer to gather timings for the histograms
            std::vector<f32> totalTimes;
            totalTimes.reserve(stats.frameStats.size());

            std::vector<f32> updateTimes;
            updateTimes.reserve(stats.frameStats.size());

            std::vector<f32> renderTimes;
            renderTimes.reserve(stats.frameStats.size());

            std::vector<f32> waitTimes;
            waitTimes.reserve(stats.frameStats.size());

            std::vector<f32> gpuTimes;
            gpuTimes.reserve(stats.frameStats.size());

            for (i32 i = 0; i < stats.frameStats.size(); i++)
            {
                totalTimes.push_back(stats.frameStats[i].deltaTimeS * 1000);
                updateTimes.push_back(stats.frameStats[i].simulationFrameTimeS * 1000);
                renderTimes.push_back(stats.frameStats[i].renderFrameTimeS * 1000);
                waitTimes.push_back(stats.frameStats[i].renderWaitTimeS * 1000);
                gpuTimes.push_back(stats.frameStats[i].gpuFrameTimeMS);
            }

            ImPlot::SetNextAxesLimits(0.0, ECS::Singletons::EngineStats::MAX_ENTRIES, 0, 33.0, ImGuiCond_Always);

            //lock minimum Y to 0 (cant have negative ms)
            //lock X completely as its fixed 120 frames
            if (ImPlot::BeginPlot("Timing", ImVec2(widthAvailable, 300)))
            {
                ImPlot::SetupAxis(ImAxis_X1, "frame", ImPlotAxisFlags_Lock);
                ImPlot::SetupAxis(ImAxis_Y1, "ms", ImPlotAxisFlags_LockMin); // ImPlotAxisFlags_LockMin

                ImPlot::PlotLine("Total", totalTimes.data(), static_cast<i32>(totalTimes.size()));
                ImPlot::PlotLine("Update", updateTimes.data(), static_cast<i32>(updateTimes.size()));
                ImPlot::PlotLine("Render", renderTimes.data(), static_cast<i32>(renderTimes.size()));
                ImPlot::PlotLine("CPU wait For GPU", waitTimes.data(), static_cast<i32>(waitTimes.size()));
                ImPlot::PlotLine("GPU", gpuTimes.data(), static_cast<i32>(gpuTimes.size()));
                ImPlot::EndPlot();
            }

            ImGui::EndChild();
        }
    }

    void PerformanceDiagnostics::DrawCullingDrawCallStatsView(u32 viewID, f32 textPos, u32& totalDrawCalls, u32& totalSurvivingDrawcalls)
    {
        GameRenderer* gameRenderer = ServiceLocator::GetGameRenderer();
        TerrainRenderer* terrainRenderer = gameRenderer->GetTerrainRenderer();
        ModelRenderer* modelRenderer = gameRenderer->GetModelRenderer();
        LiquidRenderer* liquidRenderer = gameRenderer->GetLiquidRenderer();
        JoltDebugRenderer* joltDebugRenderer = gameRenderer->GetJoltDebugRenderer();
        
        const std::string rightHeaderText = "Survived / Total (%)";

        std::string viewName = "Main View Instances";
        if (viewID > 0)
        {
            viewName = "Shadow Cascade " + std::to_string(viewID - 1) + " Drawcalls";
        }

        if (!_drawCallStatsOnlyForMainView)
        {
            ImGui::Text("%s", viewName.c_str());
            ImGui::SameLine();
            if (textPos > ImGui::GetCursorPosX())
            {
                ImGui::SameLine(textPos);
            }
            else
            {
                ImGui::NewLine();
                ImGui::SetCursorPosX(textPos);
            }

            ImGui::Text("%s", rightHeaderText.c_str());
            ImGui::Separator();
        }
        
        u32 viewDrawCalls = 0;
        u32 viewDrawCallsSurvived = 0;

        bool viewSupportsTerrainOcclusionCulling = true;
        bool viewSupportsModelsOcclusionCulling = viewID == 0;

        bool viewRendersTerrainCulling = true;
        bool viewRendersOpaqueModelsCulling = true;
        bool viewRendersTransparentModelsCulling = viewID == 0; // Only main view supports transparent cmodel culling so far
        bool viewRendersLiquidCulling = viewID == 0; // Only main view supports mapObjectgs culling so far
        bool viewRendersJoltDebug = viewID == 0;

        // Terrain
        if (viewRendersTerrainCulling)
        {
            u32 drawCalls = terrainRenderer->GetNumDrawCalls();
            viewDrawCalls += drawCalls;

            // Occluders
            if (viewSupportsTerrainOcclusionCulling)
            {
                u32 drawCallsSurvived = terrainRenderer->GetNumOccluderDrawCalls(viewID);
                DrawCullingStatsEntry("Terrain Occluders", drawCalls, drawCallsSurvived);
                viewDrawCallsSurvived += drawCallsSurvived;
            }

            // Geometry
            {
                u32 drawCallsSurvived = terrainRenderer->GetNumSurvivingDrawCalls(viewID);
                DrawCullingStatsEntry("Terrain Geometry", drawCalls, drawCallsSurvived);
                viewDrawCallsSurvived += drawCallsSurvived;
            };
        }

        // Opaque Models
        if (viewRendersOpaqueModelsCulling)
        {
            CullingResourcesBase& cullingResources = modelRenderer->GetOpaqueCullingResources();
            DrawCullingResourcesDrawCalls("Model (O)", viewID, cullingResources, viewSupportsModelsOcclusionCulling, viewDrawCalls, viewDrawCallsSurvived);
        }

        // Transparent Models
        if (viewRendersTransparentModelsCulling)
        {
            CullingResourcesBase& cullingResources = modelRenderer->GetTransparentCullingResources();
            DrawCullingResourcesDrawCalls("Model (T)", viewID, cullingResources, viewSupportsModelsOcclusionCulling, viewDrawCalls, viewDrawCallsSurvived);
        }

        // Liquid
        if (viewRendersLiquidCulling)
        {
            CullingResourcesBase& cullingResources = liquidRenderer->GetCullingResources();
            DrawCullingResourcesDrawCalls("Liquid", viewID, cullingResources, viewSupportsModelsOcclusionCulling, viewDrawCalls, viewDrawCallsSurvived);
        }

        // Jolt Debug
        if (viewRendersJoltDebug)
        {
            CullingResourcesBase& indexedCullingResources = joltDebugRenderer->GetIndexedCullingResources();
            DrawCullingResourcesDrawCalls("Jolt Debug Indexed", viewID, indexedCullingResources, viewSupportsModelsOcclusionCulling, viewDrawCalls, viewDrawCallsSurvived);

            CullingResourcesBase& cullingResources = joltDebugRenderer->GetCullingResources();
            DrawCullingResourcesDrawCalls("Jolt Debug", viewID, cullingResources, viewSupportsModelsOcclusionCulling, viewDrawCalls, viewDrawCallsSurvived);
        }

        ImGui::Separator();
        ImGui::Separator();

        // If showDrawcalls we always want to draw Total, if we are collapsed it will go on the collapsable header
        //DrawCullingStatsEntry("View Total", viewDrawCalls, viewDrawCallsSurvived, !showView);

        totalDrawCalls += viewDrawCalls;
        totalSurvivingDrawcalls += viewDrawCallsSurvived;
    }

    void PerformanceDiagnostics::DrawCullingTriangleStatsView(u32 viewID, f32 textPos, u32& totalTriangles, u32& totalSurvivingTriangles)
    {
        GameRenderer* gameRenderer = ServiceLocator::GetGameRenderer();
        TerrainRenderer* terrainRenderer = gameRenderer->GetTerrainRenderer();
        ModelRenderer* modelRenderer = gameRenderer->GetModelRenderer();
        LiquidRenderer* liquidRenderer = gameRenderer->GetLiquidRenderer();
        JoltDebugRenderer* joltDebugRenderer = gameRenderer->GetJoltDebugRenderer();

        const std::string rightHeaderText = "Survived / Total (%)";

        std::string viewName = "Main View Triangles";
        if (viewID > 0)
        {
            viewName = "Shadow Cascade " + std::to_string(viewID - 1) + " Triangles";
        }

        if (!_drawCallStatsOnlyForMainView)
        {
            ImGui::Text("%s", viewName.c_str());
            ImGui::SameLine();
            if (textPos > ImGui::GetCursorPosX())
            {
                ImGui::SameLine(textPos);
            }
            else
            {
                ImGui::NewLine();
                ImGui::SetCursorPosX(textPos);
            }

            ImGui::Text("%s", rightHeaderText.c_str());
            ImGui::Separator();
        }

        u32 viewTriangles = 0;
        u32 viewTrianglesSurvived = 0;

        bool viewSupportsTerrainOcclusionCulling = true;
        bool viewSupportsModelsOcclusionCulling = viewID == 0;

        bool viewRendersTerrainCulling = true; // Only main view supports terrain culling so far
        bool viewRendersOpaqueModelsCulling = true;
        bool viewRendersTransparentModelsCulling = viewID == 0; // Only main view supports transparent cmodel culling so far
        bool viewRendersLiquidCulling = viewID == 0; // Only main view supports liquid culling so far
        bool viewRendersJoltDebug = viewID == 0;

        // Terrain
        if (viewRendersTerrainCulling)
        {
            u32 triangles = terrainRenderer->GetNumTriangles();
            viewTriangles += triangles;

            // Occluders
            if (viewSupportsTerrainOcclusionCulling)
            {
                u32 trianglesSurvived = terrainRenderer->GetNumOccluderTriangles(viewID);
                DrawCullingStatsEntry("Terrain Occluders", triangles, trianglesSurvived);
                viewTrianglesSurvived += trianglesSurvived;
            }

            // Geometry
            {
                u32 trianglesSurvived = terrainRenderer->GetNumSurvivingGeometryTriangles(viewID);
                DrawCullingStatsEntry("Terrain Geometry", triangles, trianglesSurvived);
                viewTrianglesSurvived += trianglesSurvived;
            }
        }

        // Opaque Models
        if (viewRendersOpaqueModelsCulling)
        {
            CullingResourcesBase& cullingResources = modelRenderer->GetOpaqueCullingResources();
            DrawCullingResourcesTriangle("Model (O)", viewID, cullingResources, true, viewSupportsModelsOcclusionCulling, viewTriangles, viewTrianglesSurvived);
        }

        // Transparent Models
        if (viewRendersTransparentModelsCulling)
        {
            CullingResourcesBase& cullingResources = modelRenderer->GetTransparentCullingResources();
            DrawCullingResourcesTriangle("Model (T)", viewID, cullingResources, true, viewSupportsModelsOcclusionCulling, viewTriangles, viewTrianglesSurvived);
        }

        // Liquid
        if (viewRendersLiquidCulling)
        {
            CullingResourcesBase& cullingResources = liquidRenderer->GetCullingResources();
            DrawCullingResourcesTriangle("Liquid", viewID, cullingResources, true, viewSupportsModelsOcclusionCulling, viewTriangles, viewTrianglesSurvived);
        }

        // Jolt Debug
        if (viewRendersJoltDebug)
        {
            CullingResourcesBase& indexedCullingResources = joltDebugRenderer->GetIndexedCullingResources();
            DrawCullingResourcesTriangle("Jolt Debug Indexed", viewID, indexedCullingResources, true, viewSupportsModelsOcclusionCulling, viewTriangles, viewTrianglesSurvived);

            CullingResourcesBase& cullingResources = joltDebugRenderer->GetCullingResources();
            DrawCullingResourcesTriangle("Jolt Debug", viewID, cullingResources, true, viewSupportsModelsOcclusionCulling, viewTriangles, viewTrianglesSurvived);
        }

        // If showTriangles we always want to draw Total, if we are collapsed it will go on the collapsable header
        DrawCullingStatsEntry("View Total", viewTriangles, viewTrianglesSurvived);

        totalTriangles += viewTriangles;
        totalSurvivingTriangles += viewTrianglesSurvived;
    }

    void PerformanceDiagnostics::DrawCullingResourcesDrawCalls(std::string prefix, u32 viewID, CullingResourcesBase& cullingResources, bool viewSupportsOcclusionCulling, u32& viewDrawCalls, u32& viewDrawCallsSurvived)
    {
        u32 drawCalls = cullingResources.GetNumInstances();
        viewDrawCalls += drawCalls;

        // Occluders
        if (viewSupportsOcclusionCulling && cullingResources.HasSupportForTwoStepCulling())
        {
            u32 drawCallsSurvived = cullingResources.GetNumSurvivingOccluderInstances();
            DrawCullingStatsEntry(prefix + " Occluders", drawCalls, drawCallsSurvived);
            viewDrawCallsSurvived += drawCallsSurvived;
        }

        // Geometry
        {
            u32 drawCallsSurvived = cullingResources.GetNumSurvivingInstances(viewID);
            DrawCullingStatsEntry(prefix + " Geometry", drawCalls, drawCallsSurvived);
            viewDrawCallsSurvived += drawCallsSurvived;
        };
    }

    void PerformanceDiagnostics::DrawCullingResourcesTriangle(std::string prefix, u32 viewID, CullingResourcesBase& cullingResources, bool showView, bool viewSupportsOcclusionCulling, u32& viewTriangles, u32& viewTrianglesSurvived)
    {
        u32 triangles = cullingResources.GetNumTriangles();
        viewTriangles += triangles;

        // Occluders
        if (viewSupportsOcclusionCulling && cullingResources.HasSupportForTwoStepCulling())
        {
            u32 trianglesSurvived = cullingResources.GetNumSurvivingOccluderTriangles();

            if (showView)
            {
                DrawCullingStatsEntry(prefix + " Occluders", triangles, trianglesSurvived);
            }
            viewTrianglesSurvived += trianglesSurvived;
        }

        // Geometry
        {
            u32 trianglesSurvived = cullingResources.GetNumSurvivingTriangles(viewID);

            if (showView)
            {
                DrawCullingStatsEntry(prefix + " Geometry", triangles, trianglesSurvived);
            }
            viewTrianglesSurvived += trianglesSurvived;
        }
    }

    void PerformanceDiagnostics::DrawCullingStatsEntry(std::string_view name, u32 drawCalls, u32 survivedDrawCalls)
    {
        f32 percent = 0;
        if (drawCalls > 0) // Avoid division by 0 NaNs
        {
            percent = static_cast<f32>(survivedDrawCalls) / static_cast<f32>(drawCalls) * 100.0f;
        }

        char str[50];
        i32 strLength = StringUtils::FormatString(str, sizeof(str), "%s / %s (%.0f%%)", StringUtils::FormatThousandSeparator(survivedDrawCalls).c_str(), StringUtils::FormatThousandSeparator(drawCalls).c_str(), percent);

        f32 textWidth = ImGui::CalcTextSize(str).x;
        f32 windowWidth = ImGui::GetWindowContentRegionMax().x;

        f32 textPos = windowWidth - textWidth;

        ImGui::Separator();
        ImGui::Text("%.*s:", static_cast<i32>(name.length()), name.data());

        ImGui::SameLine();
        if (textPos > ImGui::GetCursorPosX())
        {
            ImGui::SameLine(textPos);
        }
        else
        {
            ImGui::NewLine();
            ImGui::SetCursorPosX(textPos);
        }

        ImGui::Text("%s", str);
    }
}
