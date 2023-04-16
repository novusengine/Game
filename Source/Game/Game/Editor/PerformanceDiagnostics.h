#pragma once
#include "BaseEditor.h"

#include <string_view>

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

		void DrawCullingStatsEntry(std::string_view name, u32 drawCalls, u32 survivedDrawCalls, bool isCollapsed);

	private:
		bool _drawCallStatsOnlyForMainView = true;
	};
}