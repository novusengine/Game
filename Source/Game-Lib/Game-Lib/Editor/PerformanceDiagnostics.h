#pragma once
#include "BaseEditor.h"

#include <string_view>

class CullingResourcesBase;

namespace Renderer
{
    class Renderer;
}

namespace ECS::Singletons
{
    struct EngineStats;
    struct FrameTimes;
}

typedef int ImGuiTableFlags;

namespace Editor
{
    class PerformanceDiagnostics: public BaseEditor
    {
    public:
        PerformanceDiagnostics();

        virtual const char* GetName() override { return "Performance"; }

        virtual void OnModeUpdate(bool mode) override;
        virtual void DrawImGui() override;

    private:
        void DrawCullingDrawCallStatsView(u32 viewID, f32 textPos, u32& totalDrawCalls, u32& totalSurvivingDrawcalls);
        void DrawCullingTriangleStatsView(u32 viewID, f32 textPos, u32& totalTriangles, u32& totalSurvivingTriangles);

        void DrawCullingResourcesDrawCalls(std::string prefix, u32 viewID, CullingResourcesBase& cullingResources, bool viewSupportsOcclusionCulling, u32& viewDrawCalls, u32& viewDrawCallsSurvived);
        void DrawCullingResourcesTriangle(std::string prefix, u32 viewID, CullingResourcesBase& cullingResources, bool showView, bool viewSupportsOcclusionCulling, u32& viewTriangles, u32& viewTrianglesSurvived);

        void DrawCullingStatsEntry(std::string_view name, u32 drawCalls, u32 survivedDrawCalls);

        void DrawSurvivingDrawCalls(f32 constraint, const std::string& text, u32 numCascades, f32 widthConstraint = -1.f);
        void DrawSurvivingTriangles(f32 constraint, const std::string& text, u32 numCascades, f32 widthConstraint = -1.f);
        void DrawFrameTimes(f32 constraint, const ECS::Singletons::FrameTimes& average, const ImGuiTableFlags& flags, f32 widthConstraint = -1.f);
        void DrawRenderPass(f32 constraint, Renderer::Renderer* renderer, ECS::Singletons::EngineStats& stats, const ImGuiTableFlags& flags, f32 widthConstraint = -1.f);
        void DrawFrameTimesGraph(f32 constraint, const ECS::Singletons::EngineStats& stats, f32 widthConstraint = -1.f);

        f32 CalculateTotalParam(const std::vector<f32>& params, const std::vector<bool>& shownItems);
        std::vector<f32> CalculateProportions(const std::vector<f32>& params, const std::vector<bool>& shownItems);
        void ProportionsFix(std::vector<f32>& proportions);

    private:
        bool _drawCallStatsOnlyForMainView = true;

        bool _showSurvivingDrawCalls = true;
        bool _showSurvivingTriangle = true;
        bool _showFrameTime = true;
        bool _showRenderPass = true;
        bool _showFrameGraph = true;

        const std::vector<f32> _sectionParamHorizontal = {0.175f, 0.175f, 0.15f, 0.15f, 0.3f};
        const std::vector<f32> _sectionParamVerticalMultiColumn = {0.25f, 0.25f, 0.25f, 0.25f, 0.35f};
        const std::vector<f32> _sectionParamVertical = {0.18f, 0.18f, 0.162f, 0.18f, 0.30f};
    };
}