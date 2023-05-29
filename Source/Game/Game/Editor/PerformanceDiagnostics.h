#pragma once
#include "BaseEditor.h"

#include <Game/Rendering/GameRenderer.h>
#include <Game/ECS/Singletons/EngineStats.h>

#include <imgui/imgui.h>

#include <string_view>

class CullingResourcesBase;

namespace Editor
{
	class PerformanceDiagnostics : public BaseEditor
	{
	public:
		PerformanceDiagnostics();

		virtual const char* GetName() override { return "PerformanceDiagnostics"; }

		virtual void DrawImGui() override;

	private:
		void DrawCullingDrawcallStatsView(u32 viewID, f32 textPos, u32& totalDrawCalls, u32& totalSurvivingDrawcalls, bool showDrawcalls);
		void DrawCullingTriangleStatsView(u32 viewID, f32 textPos, u32& totalTriangles, u32& totalSurvivingTriangles, bool showTriangles);

		void DrawCullingResourcesDrawcall(std::string prefix, u32 viewID, CullingResourcesBase& cullingResources, bool showView, bool viewSupportsOcclusionCulling, u32& viewDrawCalls, u32& viewDrawCallsSurvived);
		void DrawCullingResourcesTriangle(std::string prefix, u32 viewID, CullingResourcesBase& cullingResources, bool showView, bool viewSupportsOcclusionCulling, u32& viewTriangles, u32& viewTrianglesSurvived);

		void DrawCullingStatsEntry(std::string_view name, u32 drawCalls, u32 survivedDrawCalls, bool isCollapsed);

		void DrawSurvivingDrawCalls(f32 constraint, const std::string& text, u32 numCascades);
		void DrawSurvivingTriangles(f32 constraint, const std::string& text, u32 numCascades);
		void DrawFrameTimes(f32 constraint, const ECS::Singletons::EngineStats::Frame& average, const ImGuiTableFlags& flags);
		void DrawRenderPass(f32 constraint, Renderer::Renderer* renderer, ECS::Singletons::EngineStats& stats, const ImGuiTableFlags& flags);
		void DrawFrameTimesGraph(f32 constraint, const ECS::Singletons::EngineStats& stats);

	private:
		bool _drawCallStatsOnlyForMainView = true;
	};
}