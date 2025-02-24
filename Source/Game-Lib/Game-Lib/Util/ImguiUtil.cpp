#include "ImguiUtil.h"

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

namespace Util
{
    namespace Imgui
    {
        bool IsDockedToMain()
        {
            ImGuiViewport* viewport = ImGui::GetWindowViewport();
            ImGuiViewport* mainViewport = ImGui::GetMainViewport();

            return viewport->ID == mainViewport->ID;
        }

        bool IsDockedToMain(ImGuiWindow* window)
        {
            ImGuiViewport* mainViewport = ImGui::GetMainViewport();

            return window->Viewport->ID == mainViewport->ID;
        }

        void ItemRowsBackground(f32 lineHeight, const ImColor& color)
        {
            auto* drawList = ImGui::GetWindowDrawList();
            const auto& style = ImGui::GetStyle();

            if (lineHeight < 0)
            {
                lineHeight = ImGui::GetTextLineHeight();
            }
            lineHeight += style.ItemSpacing.y;

            f32 scrollOffsetH = ImGui::GetScrollX();
            f32 scrollOffsetV = ImGui::GetScrollY();
            f32 scrolledOutLines = floorf(scrollOffsetV / lineHeight);
            scrollOffsetV -= lineHeight * scrolledOutLines;

            ImVec2 clipRectMin(ImGui::GetWindowPos().x, ImGui::GetWindowPos().y);
            ImVec2 clipRectMax(clipRectMin.x + ImGui::GetWindowWidth(), clipRectMin.y + ImGui::GetWindowHeight());

            if (ImGui::GetScrollMaxX() > 0)
            {
                clipRectMax.y -= style.ScrollbarSize;
            }

            drawList->PushClipRect(clipRectMin, clipRectMax);

            bool isOdd = (static_cast<i32>(scrolledOutLines) % 2) == 0;

            f32 yMin = clipRectMin.y - scrollOffsetV + ImGui::GetCursorPosY();
            f32 yMax = clipRectMax.y - scrollOffsetV + lineHeight;
            f32 xMin = clipRectMin.x + scrollOffsetH + ImGui::GetWindowContentRegionMin().x;
            f32 xMax = clipRectMin.x + scrollOffsetH + ImGui::GetWindowContentRegionMax().x;

            for (f32 y = yMin; y < yMax; y += lineHeight, isOdd = !isOdd)
            {
                if (isOdd)
                {
                    drawList->AddRectFilled({ xMin, y - style.ItemSpacing.y }, { xMax, y + lineHeight }, color);
                }
            }

            drawList->PopClipRect();
        }

        static ImVector<ImRect> s_GroupPanelLabelStack;
        static bool s_treeOpen = false;
        bool BeginGroupPanel(const char* name, const ImVec2& size)
        {
            ImGui::BeginGroup();

            auto cursorPos = ImGui::GetCursorScreenPos();
            auto itemSpacing = ImGui::GetStyle().ItemSpacing;
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));

            auto frameHeight = ImGui::GetFrameHeight();
            ImGui::BeginGroup();

            ImVec2 effectiveSize = size;
            if (size.x < 0.0f)
                effectiveSize.x = ImGui::GetContentRegionAvail().x;
            else
                effectiveSize.x = size.x;
            ImGui::Dummy(ImVec2(effectiveSize.x, 0.0f));

            ImGui::Dummy(ImVec2(frameHeight * 0.5f, 0.0f));
            ImGui::SameLine(0.0f, 0.0f);
            ImGui::BeginGroup();
            ImGui::Dummy(ImVec2(frameHeight * 0.5f, 0.0f));
            ImGui::SameLine(0.0f, 0.0f);

            s_treeOpen = ImGui::TreeNodeEx(name, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_NoTreePushOnOpen);

            auto labelMin = ImGui::GetItemRectMin();
            auto labelMax = ImGui::GetItemRectMax();
            ImGui::SameLine(0.0f, 0.0f);
            ImGui::Dummy(ImVec2(0.0, frameHeight + itemSpacing.y));
            ImGui::BeginGroup();

            //ImGui::GetWindowDrawList()->AddRect(labelMin, labelMax, IM_COL32(255, 0, 255, 255));

            ImGui::PopStyleVar(2);

            ImGui::GetCurrentWindow()->ContentRegionRect.Max.x -= frameHeight * 0.5f;
            ImGui::GetCurrentWindow()->WorkRect.Max.x -= frameHeight * 0.5f;
            ImGui::GetCurrentWindow()->InnerRect.Max.x -= frameHeight * 0.5f;
            ImGui::GetCurrentWindow()->Size.x -= frameHeight;

            auto itemWidth = ImGui::CalcItemWidth();
            ImGui::PushItemWidth(ImMax(0.0f, itemWidth - frameHeight));

            s_GroupPanelLabelStack.push_back(ImRect(labelMin, labelMax));

            return s_treeOpen;
        }

        void EndGroupPanel()
        {
            ImGui::PopItemWidth();

            auto itemSpacing = ImGui::GetStyle().ItemSpacing;

            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));

            auto frameHeight = ImGui::GetFrameHeight();

            ImGui::EndGroup();

            //ImGui::GetWindowDrawList()->AddRectFilled(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), IM_COL32(0, 255, 0, 64), 4.0f);

            /*if (s_treeOpen)
            {
                ImGui::TreePop();
            }*/

            ImGui::EndGroup();

            ImGui::SameLine(0.0f, 0.0f);
            ImGui::Dummy(ImVec2(frameHeight * 0.5f, 0.0f));
            ImGui::Dummy(ImVec2(0.0, frameHeight - frameHeight * 0.5f - itemSpacing.y));

            ImGui::EndGroup();

            vec2 itemMin = ImGui::GetItemRectMin();
            vec2 itemMax = ImGui::GetItemRectMax();
            //ImGui::GetWindowDrawList()->AddRectFilled(itemMin, itemMax, IM_COL32(255, 0, 0, 64), 4.0f);

            auto labelRect = s_GroupPanelLabelStack.back();
            s_GroupPanelLabelStack.pop_back();

            vec2 halfFrame = vec2(frameHeight * 0.25f, frameHeight) * 0.5f;
            ImRect frameRect = ImRect(itemMin + halfFrame, itemMax - vec2(halfFrame.x, 0.0f));
            labelRect.Min.x -= itemSpacing.x;
            labelRect.Max.x += itemSpacing.x;

            f32 extraPadding = 0.0f;
            if (s_treeOpen)
            {
                extraPadding = 5.0f;
                frameRect.Max.y += itemSpacing.y; // TEST
                //labelRect.Max.y += itemSpacing.y; // TEST
            }

            for (int i = 0; i < 4; ++i)
            {

                switch (i)
                {
                    // left half-plane
                case 0: ImGui::PushClipRect(ImVec2(-FLT_MAX, -FLT_MAX), ImVec2(labelRect.Min.x, FLT_MAX), true); break;
                    // right half-plane
                case 1: ImGui::PushClipRect(ImVec2(labelRect.Max.x, -FLT_MAX), ImVec2(FLT_MAX, FLT_MAX), true); break;
                    // top
                case 2: ImGui::PushClipRect(ImVec2(labelRect.Min.x, -FLT_MAX), ImVec2(labelRect.Max.x, labelRect.Min.y), true); break;
                    // bottom
                case 3: ImGui::PushClipRect(ImVec2(labelRect.Min.x, labelRect.Max.y), ImVec2(labelRect.Max.x, FLT_MAX), true); break;
                }

                if (s_treeOpen)
                {
                    ImGui::GetWindowDrawList()->AddRect(
                        frameRect.Min, frameRect.Max,
                        ImColor(ImGui::GetStyleColorVec4(ImGuiCol_Border)),
                        halfFrame.x);
                }

                ImGui::PopClipRect();
            }

            ImGui::PopStyleVar(2);

            ImGui::GetCurrentWindow()->ContentRegionRect.Max.x += frameHeight * 0.5f;
            ImGui::GetCurrentWindow()->WorkRect.Max.x += frameHeight * 0.5f;
            ImGui::GetCurrentWindow()->InnerRect.Max.x += frameHeight * 0.5f;

            ImGui::GetCurrentWindow()->Size.x += frameHeight;

            ImGui::Dummy(ImVec2(0.0f, extraPadding));

            ImGui::EndGroup();
        }

        bool DrawColoredRectAndDragF32(const char* id, f32& value, ImVec4 color, f32 fractionOfWidth, f32 speed)
        {
            // Get a pointer to the current window's draw list
            ImDrawList* drawList = ImGui::GetWindowDrawList();

            f32 rectHeight = ImGui::GetFrameHeight();  // height of the DragFloat
            f32 rectWidth = 2;  // adjust as needed

            // Draw a colored rectangle
            vec2 startPos = ImGui::GetCursorScreenPos();
            vec2 endPos = vec2(ImGui::GetCursorScreenPos()) + vec2(rectWidth, rectHeight);

            drawList->AddRectFilled(startPos, endPos, ImColor(color));
            ImGui::SetCursorScreenPos(vec2(endPos.x, startPos.y));

            // Adjust the width for the space if it's not the last component
            f32 adjustedWidth = fractionOfWidth - rectWidth;

            // Draw a DragFloat
            ImGui::PushItemWidth(adjustedWidth);
            bool isDirty = ImGui::DragFloat(id, &value, speed);
            ImGui::PopItemWidth();

            return isDirty;
        }

        void DrawColoredRectAndReadOnlyF32(const char* id, const f32& value, ImVec4 color, f32 fractionOfWidth)
        {
            // Get a pointer to the current window's draw list
            ImDrawList* drawList = ImGui::GetWindowDrawList();

            f32 rectHeight = ImGui::GetFrameHeight();  // height of the Text field
            f32 rectWidth = 2;  // adjust as needed

            // Draw a colored rectangle
            vec2 startPos = ImGui::GetCursorScreenPos();
            vec2 endPos = vec2(ImGui::GetCursorScreenPos()) + vec2(rectWidth, rectHeight);

            drawList->AddRectFilled(startPos, endPos, ImColor(color));
            ImGui::SetCursorScreenPos(vec2(endPos.x, startPos.y));

            // Adjust the width for the space if it's not the last component
            f32 adjustedWidth = fractionOfWidth - rectWidth;

            // Draw a InputFloat field with read only flag
            ImGui::PushItemWidth(adjustedWidth);
            ImGui::InputFloat(id, const_cast<f32*>(&value), 0.0f, 0.0f, "%.3f", ImGuiInputTextFlags_ReadOnly);
            ImGui::PopItemWidth();
        }

        bool DrawColoredRectAndDragI32(const char* id, i32& value, ImVec4 color, f32 fractionOfWidth)
        {
            // Get a pointer to the current window's draw list
            ImDrawList* drawList = ImGui::GetWindowDrawList();

            f32 rectHeight = ImGui::GetFrameHeight();
            f32 rectWidth = 2;

            // Draw a colored rectangle
            vec2 startPos = ImGui::GetCursorScreenPos();
            vec2 endPos = vec2(ImGui::GetCursorScreenPos()) + vec2(rectWidth, rectHeight);

            drawList->AddRectFilled(startPos, endPos, ImColor(color));
            ImGui::SetCursorScreenPos(vec2(endPos.x, startPos.y));

            f32 adjustedWidth = fractionOfWidth - rectWidth;

            ImGui::PushItemWidth(adjustedWidth);
            bool isDirty = ImGui::DragInt(id, &value);
            ImGui::PopItemWidth();

            return isDirty;
        }

        void DrawColoredRectAndReadOnlyI32(const char* id, const i32& value, ImVec4 color, f32 fractionOfWidth)
        {
            // Get a pointer to the current window's draw list
            ImDrawList* drawList = ImGui::GetWindowDrawList();

            f32 rectHeight = ImGui::GetFrameHeight();  // height of the Text field
            f32 rectWidth = 2;  // adjust as needed

            // Draw a colored rectangle
            vec2 startPos = ImGui::GetCursorScreenPos();
            vec2 endPos = vec2(ImGui::GetCursorScreenPos()) + vec2(rectWidth, rectHeight);

            drawList->AddRectFilled(startPos, endPos, ImColor(color));
            ImGui::SetCursorScreenPos(vec2(endPos.x, startPos.y));

            // Adjust the width for the space if it's not the last component
            f32 adjustedWidth = fractionOfWidth - rectWidth;

            // Draw a InputInt field with read only flag
            ImGui::PushItemWidth(adjustedWidth);
            ImGui::InputInt(id, const_cast<i32*>(&value), 0, 0, ImGuiInputTextFlags_ReadOnly);
            ImGui::PopItemWidth();
        }

        bool DrawColoredRectAndDragU32(const char* id, u32& value, ImVec4 color, f32 fractionOfWidth)
        {
            // Get a pointer to the current window's draw list
            ImDrawList* drawList = ImGui::GetWindowDrawList();

            f32 rectHeight = ImGui::GetFrameHeight();
            f32 rectWidth = 2;

            // Draw a colored rectangle
            vec2 startPos = ImGui::GetCursorScreenPos();
            vec2 endPos = vec2(ImGui::GetCursorScreenPos()) + vec2(rectWidth, rectHeight);

            drawList->AddRectFilled(startPos, endPos, ImColor(color));
            ImGui::SetCursorScreenPos(vec2(endPos.x, startPos.y));

            f32 adjustedWidth = fractionOfWidth - rectWidth;

            ImGui::PushItemWidth(adjustedWidth);
            bool isDirty = ImGui::DragScalar(id, ImGuiDataType_U32, &value);
            ImGui::PopItemWidth();

            return isDirty;
        }

        void DrawColoredRectAndReadOnlyU32(const char* id, const u32& value, ImVec4 color, f32 fractionOfWidth)
        {
            // Get a pointer to the current window's draw list
            ImDrawList* drawList = ImGui::GetWindowDrawList();

            f32 rectHeight = ImGui::GetFrameHeight();  // height of the Text field
            f32 rectWidth = 2;  // adjust as needed

            // Draw a colored rectangle
            vec2 startPos = ImGui::GetCursorScreenPos();
            vec2 endPos = vec2(ImGui::GetCursorScreenPos()) + vec2(rectWidth, rectHeight);

            drawList->AddRectFilled(startPos, endPos, ImColor(color));
            ImGui::SetCursorScreenPos(vec2(endPos.x, startPos.y));

            // Adjust the width for the space if it's not the last component
            f32 adjustedWidth = fractionOfWidth - rectWidth;

            // Draw a InputInt field with read only flag
            ImGui::PushItemWidth(adjustedWidth);
            ImGui::InputScalar(id, ImGuiDataType_U32, const_cast<u32*>(&value), nullptr, nullptr, nullptr, ImGuiInputTextFlags_ReadOnly);
            ImGui::PopItemWidth();
        }

        bool Inspect(const char* name, f32& value, f32 speed)
        {
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%s", name);

            ImGui::PushID(name);  // Ensure unique ID for multiple floats

            bool isDirty = false;

            ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);
            isDirty |= ImGui::DragFloat("##value", &value, speed);
            ImGui::PopItemWidth();

            ImGui::PopID();

            return isDirty;
        }

        bool Inspect(const char* name, const f32& value, f32 speed)
        {
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%s", name);

            ImGui::PushID(name);  // Ensure unique ID for multiple floats

            ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);
            ImGui::InputFloat("##value", const_cast<f32*>(&value), 0.0f, 0.0f, "%.3f", ImGuiInputTextFlags_ReadOnly);
            ImGui::PopItemWidth();

            ImGui::PopID();

            return false;
        }

        bool Inspect(const char* name, vec2& value, f32 speed)
        {
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%s", name);

            ImGui::PushID(name);  // Ensure unique ID for multiple vec2s

            f32 fractionOfWidth = ImGui::GetContentRegionAvail().x;
            bool isDirty = false;

            isDirty |= DrawColoredRectAndDragF32("##X", value.x, ImVec4(1.0f, 0.0f, 0.0f, 1.0f), fractionOfWidth, speed);  // Red for X
            isDirty |= DrawColoredRectAndDragF32("##Y", value.y, ImVec4(0.0f, 1.0f, 0.0f, 1.0f), fractionOfWidth, speed);  // Green for Y

            ImGui::PopID();

            return isDirty;
        }

        bool Inspect(const char* name, const vec2& value, f32 speed)
        {
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%s", name);

            ImGui::PushID(name);  // Ensure unique ID for multiple vec2s

            f32 fractionOfWidth = ImGui::GetContentRegionAvail().x;

            DrawColoredRectAndReadOnlyF32("##X", value.x, ImVec4(1.0f, 0.0f, 0.0f, 1.0f), fractionOfWidth);  // Red for X
            DrawColoredRectAndReadOnlyF32("##Y", value.y, ImVec4(0.0f, 1.0f, 0.0f, 1.0f), fractionOfWidth);  // Green for Y

            ImGui::PopID();

            return false;
        }

        bool Inspect(const char* name, vec3& value, f32 speed)
        {
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%s", name);

            ImGui::PushID(name);  // Ensure unique ID for multiple vec3s

            f32 fractionOfWidth = ImGui::GetContentRegionAvail().x;
            bool isDirty = false;

            // Draw colored rectangles and DragFloats for the X, Y, and Z components
            isDirty |= DrawColoredRectAndDragF32("##X", value.x, ImVec4(1.0f, 0.0f, 0.0f, 1.0f), fractionOfWidth, speed);  // Red for X
            isDirty |= DrawColoredRectAndDragF32("##Y", value.y, ImVec4(0.0f, 1.0f, 0.0f, 1.0f), fractionOfWidth, speed);  // Green for Y
            isDirty |= DrawColoredRectAndDragF32("##Z", value.z, ImVec4(0.0f, 0.0f, 1.0f, 1.0f), fractionOfWidth, speed);  // Blue

            ImGui::PopID();

            return isDirty;
        }

        bool Inspect(const char* name, const vec3& value, f32 speed)
        {
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%s", name);

            ImGui::PushID(name);  // Ensure unique ID for multiple vec3s

            f32 fractionOfWidth = ImGui::GetContentRegionAvail().x;

            // Draw colored rectangles and DragFloats for the X, Y, and Z components
            DrawColoredRectAndReadOnlyF32("##X", value.x, ImVec4(1.0f, 0.0f, 0.0f, 1.0f), fractionOfWidth);  // Red for X
            DrawColoredRectAndReadOnlyF32("##Y", value.y, ImVec4(0.0f, 1.0f, 0.0f, 1.0f), fractionOfWidth);  // Green for Y
            DrawColoredRectAndReadOnlyF32("##Z", value.z, ImVec4(0.0f, 0.0f, 1.0f, 1.0f), fractionOfWidth);  // Blue for Z

            ImGui::PopID();

            return false;
        }

        bool Inspect(const char* name, vec4& value, f32 speed)
        {
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%s", name);

            ImGui::PushID(name);  // Ensure unique ID for multiple vec4s

            f32 fractionOfWidth = ImGui::GetContentRegionAvail().x;
            bool isDirty = false;

            // Draw colored rectangles and DragFloats for the X, Y, Z, and W components
            isDirty |= DrawColoredRectAndDragF32("##X", value.x, ImVec4(1.0f, 0.0f, 0.0f, 1.0f), fractionOfWidth, speed);  // Red for X
            isDirty |= DrawColoredRectAndDragF32("##Y", value.y, ImVec4(0.0f, 1.0f, 0.0f, 1.0f), fractionOfWidth, speed);  // Green for Y
            isDirty |= DrawColoredRectAndDragF32("##Z", value.z, ImVec4(0.0f, 0.0f, 1.0f, 1.0f), fractionOfWidth, speed);  // Blue for Z
            isDirty |= DrawColoredRectAndDragF32("##W", value.w, ImVec4(1.0f, 1.0f, 1.0f, 1.0f), fractionOfWidth, speed);  // White for W

            ImGui::PopID();

            return isDirty;
        }

        bool Inspect(const char* name, const vec4& value, f32 speed)
        {
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%s", name);

            ImGui::PushID(name);  // Ensure unique ID for multiple vec4s

            f32 fractionOfWidth = ImGui::GetContentRegionAvail().x;

            // Draw colored rectangles and DragFloats for the X, Y, Z, and W components
            DrawColoredRectAndReadOnlyF32("##X", value.x, ImVec4(1.0f, 0.0f, 0.0f, 1.0f), fractionOfWidth);  // Red for X
            DrawColoredRectAndReadOnlyF32("##Y", value.y, ImVec4(0.0f, 1.0f, 0.0f, 1.0f), fractionOfWidth);  // Green for Y
            DrawColoredRectAndReadOnlyF32("##Z", value.z, ImVec4(0.0f, 0.0f, 1.0f, 1.0f), fractionOfWidth);  // Blue for Z
            DrawColoredRectAndReadOnlyF32("##W", value.w, ImVec4(1.0f, 1.0f, 1.0f, 1.0f), fractionOfWidth);  // White for W

            ImGui::PopID();

            return false;
        }

        bool Inspect(const char* name, glm::quat& value, f32 speed)
        {
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%s", name);

            ImGui::PushID(name);  // Ensure unique ID for multiple quaternions

            glm::vec3 euler = glm::degrees(glm::eulerAngles(value));  // convert to Euler angles in degrees
            glm::vec3 originalEuler = euler;  // keep a copy of the original values

            f32 fractionOfWidth = ImGui::GetContentRegionAvail().x;
            bool isDirty = false;

            // Draw colored rectangles and DragFloats for the Euler angles
            isDirty |= DrawColoredRectAndDragF32("##X", euler.x, ImVec4(1.0f, 0.0f, 0.0f, 1.0f), fractionOfWidth, speed);  // Red for X
            isDirty |= DrawColoredRectAndDragF32("##Y", euler.y, ImVec4(0.0f, 1.0f, 0.0f, 1.0f), fractionOfWidth, speed);  // Green for Y
            isDirty |= DrawColoredRectAndDragF32("##Z", euler.z, ImVec4(0.0f, 0.0f, 1.0f, 1.0f), fractionOfWidth, speed);  // Blue for Z

            // If the Euler angles have changed, convert them back to a quaternion
            if (euler != originalEuler)
            {
                value = glm::quat(glm::radians(euler));  // convert from degrees to radians
            }

            ImGui::PopID();

            return isDirty;
        }

        bool Inspect(const char* name, const glm::quat& value, f32 speed)
        {
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%s", name);

            ImGui::PushID(name);  // Ensure unique ID for multiple quaternions

            glm::vec3 euler = glm::degrees(glm::eulerAngles(value));  // convert to Euler angles in degrees
            glm::vec3 originalEuler = euler;  // keep a copy of the original values

            f32 fractionOfWidth = ImGui::GetContentRegionAvail().x;

            // Draw colored rectangles and DragFloats for the Euler angles
            DrawColoredRectAndReadOnlyF32("##X", euler.x, ImVec4(1.0f, 0.0f, 0.0f, 1.0f), fractionOfWidth);  // Red for X
            DrawColoredRectAndReadOnlyF32("##Y", euler.y, ImVec4(0.0f, 1.0f, 0.0f, 1.0f), fractionOfWidth);  // Green for Y
            DrawColoredRectAndReadOnlyF32("##Z", euler.z, ImVec4(0.0f, 0.0f, 1.0f, 1.0f), fractionOfWidth);  // Blue for Z

            ImGui::PopID();

            return false;
        }

        bool Inspect(const char* name, i32& value, f32 speed)
        {
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%s", name);

            ImGui::PushID(name);
            bool isDirty = false;

            ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);
            isDirty |= ImGui::DragInt("##value", &value);
            ImGui::PopItemWidth();

            ImGui::PopID();

            return isDirty;
        }

        bool Inspect(const char* name, const i32& value, f32 speed)
        {
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%s", name);

            ImGui::PushID(name);

            ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);
            ImGui::InputInt("##value", const_cast<i32*>(&value), 0, 0, ImGuiInputTextFlags_ReadOnly);
            ImGui::PopItemWidth();

            ImGui::PopID();

            return false;
        }

        bool Inspect(const char* name, ivec2& value, f32 speed)
        {
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%s", name);

            ImGui::PushID(name);
            bool isDirty = false;

            f32 fractionOfWidth = ImGui::GetContentRegionAvail().x;
            isDirty |= DrawColoredRectAndDragI32("##X", value.x, ImVec4(1.0f, 0.0f, 0.0f, 1.0f), fractionOfWidth);
            isDirty |= DrawColoredRectAndDragI32("##Y", value.y, ImVec4(0.0f, 1.0f, 0.0f, 1.0f), fractionOfWidth);

            ImGui::PopID();

            return isDirty;
        }

        bool Inspect(const char* name, const ivec2& value, f32 speed)
        {
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%s", name);

            ImGui::PushID(name);

            f32 fractionOfWidth = ImGui::GetContentRegionAvail().x;
            DrawColoredRectAndReadOnlyI32("##X", value.x, ImVec4(1.0f, 0.0f, 0.0f, 1.0f), fractionOfWidth);
            DrawColoredRectAndReadOnlyI32("##Y", value.y, ImVec4(0.0f, 1.0f, 0.0f, 1.0f), fractionOfWidth);

            ImGui::PopID();

            return false;
        }

        bool Inspect(const char* name, ivec3& value, f32 speed)
        {
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%s", name);

            ImGui::PushID(name);
            bool isDirty = false;

            f32 fractionOfWidth = ImGui::GetContentRegionAvail().x;
            isDirty |= DrawColoredRectAndDragI32("##X", value.x, ImVec4(1.0f, 0.0f, 0.0f, 1.0f), fractionOfWidth);
            isDirty |= DrawColoredRectAndDragI32("##Y", value.y, ImVec4(0.0f, 1.0f, 0.0f, 1.0f), fractionOfWidth);
            isDirty |= DrawColoredRectAndDragI32("##Z", value.z, ImVec4(0.0f, 0.0f, 1.0f, 1.0f), fractionOfWidth);

            ImGui::PopID();

            return isDirty;
        }

        bool Inspect(const char* name, const ivec3& value, f32 speed)
        {
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%s", name);

            ImGui::PushID(name);

            f32 fractionOfWidth = ImGui::GetContentRegionAvail().x;
            DrawColoredRectAndReadOnlyI32("##X", value.x, ImVec4(1.0f, 0.0f, 0.0f, 1.0f), fractionOfWidth);
            DrawColoredRectAndReadOnlyI32("##Y", value.y, ImVec4(0.0f, 1.0f, 0.0f, 1.0f), fractionOfWidth);
            DrawColoredRectAndReadOnlyI32("##Z", value.z, ImVec4(0.0f, 0.0f, 1.0f, 1.0f), fractionOfWidth);

            ImGui::PopID();

            return false;
        }

        bool Inspect(const char* name, ivec4& value, f32 speed)
        {
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%s", name);

            ImGui::PushID(name);
            bool isDirty = false;

            f32 fractionOfWidth = ImGui::GetContentRegionAvail().x;
            isDirty |= DrawColoredRectAndDragI32("##X", value.x, ImVec4(1.0f, 0.0f, 0.0f, 1.0f), fractionOfWidth);
            isDirty |= DrawColoredRectAndDragI32("##Y", value.y, ImVec4(0.0f, 1.0f, 0.0f, 1.0f), fractionOfWidth);
            isDirty |= DrawColoredRectAndDragI32("##Z", value.z, ImVec4(0.0f, 0.0f, 1.0f, 1.0f), fractionOfWidth);
            isDirty |= DrawColoredRectAndDragI32("##W", value.w, ImVec4(1.0f, 1.0f, 1.0f, 1.0f), fractionOfWidth);

            ImGui::PopID();

            return isDirty;
        }

        bool Inspect(const char* name, const ivec4& value, f32 speed)
        {
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%s", name);

            ImGui::PushID(name);

            f32 fractionOfWidth = ImGui::GetContentRegionAvail().x;
            DrawColoredRectAndReadOnlyI32("##X", value.x, ImVec4(1.0f, 0.0f, 0.0f, 1.0f), fractionOfWidth);
            DrawColoredRectAndReadOnlyI32("##Y", value.y, ImVec4(0.0f, 1.0f, 0.0f, 1.0f), fractionOfWidth);
            DrawColoredRectAndReadOnlyI32("##Z", value.z, ImVec4(0.0f, 0.0f, 1.0f, 1.0f), fractionOfWidth);
            DrawColoredRectAndReadOnlyI32("##W", value.w, ImVec4(1.0f, 1.0f, 1.0f, 1.0f), fractionOfWidth);

            ImGui::PopID();

            return false;
        }

        bool Inspect(const char* name, u32& value, f32 speed)
        {
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%s", name);

            ImGui::PushID(name);
            bool isDirty = false;

            ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);
            isDirty |= ImGui::DragScalar("##value", ImGuiDataType_U32, &value);
            ImGui::PopItemWidth();

            ImGui::PopID();

            return isDirty;
        }

        bool Inspect(const char* name, const u32& value, f32 speed)
        {
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%s", name);

            ImGui::PushID(name);

            ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);
            ImGui::InputScalar("##value", ImGuiDataType_U32, const_cast<u32*>(&value), nullptr, nullptr, nullptr, ImGuiInputTextFlags_ReadOnly);
            ImGui::PopItemWidth();

            ImGui::PopID();

            return false;
        }

        bool Inspect(const char* name, uvec2& value, f32 speed)
        {
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%s", name);

            ImGui::PushID(name);
            bool isDirty = false;

            f32 fractionOfWidth = ImGui::GetContentRegionAvail().x;
            isDirty |= DrawColoredRectAndDragU32("##X", value.x, ImVec4(1.0f, 0.0f, 0.0f, 1.0f), fractionOfWidth);
            isDirty |= DrawColoredRectAndDragU32("##Y", value.y, ImVec4(0.0f, 1.0f, 0.0f, 1.0f), fractionOfWidth);

            ImGui::PopID();

            return isDirty;
        }

        bool Inspect(const char* name, const uvec2& value, f32 speed)
        {
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%s", name);

            ImGui::PushID(name);

            f32 fractionOfWidth = ImGui::GetContentRegionAvail().x;
            DrawColoredRectAndReadOnlyU32("##X", value.x, ImVec4(1.0f, 0.0f, 0.0f, 1.0f), fractionOfWidth);
            DrawColoredRectAndReadOnlyU32("##Y", value.y, ImVec4(0.0f, 1.0f, 0.0f, 1.0f), fractionOfWidth);

            ImGui::PopID();

            return false;
        }

        bool Inspect(const char* name, uvec3& value, f32 speed)
        {
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%s", name);

            ImGui::PushID(name);
            bool isDirty = false;

            f32 fractionOfWidth = ImGui::GetContentRegionAvail().x;
            isDirty |= DrawColoredRectAndDragU32("##X", value.x, ImVec4(1.0f, 0.0f, 0.0f, 1.0f), fractionOfWidth);
            isDirty |= DrawColoredRectAndDragU32("##Y", value.y, ImVec4(0.0f, 1.0f, 0.0f, 1.0f), fractionOfWidth);
            isDirty |= DrawColoredRectAndDragU32("##Z", value.z, ImVec4(0.0f, 0.0f, 1.0f, 1.0f), fractionOfWidth);

            ImGui::PopID();

            return isDirty;
        }

        bool Inspect(const char* name, const uvec3& value, f32 speed)
        {
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%s", name);

            ImGui::PushID(name);

            f32 fractionOfWidth = ImGui::GetContentRegionAvail().x;
            DrawColoredRectAndReadOnlyU32("##X", value.x, ImVec4(1.0f, 0.0f, 0.0f, 1.0f), fractionOfWidth);
            DrawColoredRectAndReadOnlyU32("##Y", value.y, ImVec4(0.0f, 1.0f, 0.0f, 1.0f), fractionOfWidth);
            DrawColoredRectAndReadOnlyU32("##Z", value.z, ImVec4(0.0f, 0.0f, 1.0f, 1.0f), fractionOfWidth);

            ImGui::PopID();

            return false;
        }

        bool Inspect(const char* name, uvec4& value, f32 speed)
        {
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%s", name);

            ImGui::PushID(name);
            bool isDirty = false;

            f32 fractionOfWidth = ImGui::GetContentRegionAvail().x;
            isDirty |= DrawColoredRectAndDragU32("##X", value.x, ImVec4(1.0f, 0.0f, 0.0f, 1.0f), fractionOfWidth);
            isDirty |= DrawColoredRectAndDragU32("##Y", value.y, ImVec4(0.0f, 1.0f, 0.0f, 1.0f), fractionOfWidth);
            isDirty |= DrawColoredRectAndDragU32("##Z", value.z, ImVec4(0.0f, 0.0f, 1.0f, 1.0f), fractionOfWidth);
            isDirty |= DrawColoredRectAndDragU32("##W", value.w, ImVec4(1.0f, 1.0f, 1.0f, 1.0f), fractionOfWidth);

            ImGui::PopID();

            return isDirty;
        }

        bool Inspect(const char* name, const uvec4& value, f32 speed)
        {
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%s", name);

            ImGui::PushID(name);

            f32 fractionOfWidth = ImGui::GetContentRegionAvail().x;
            DrawColoredRectAndReadOnlyU32("##X", value.x, ImVec4(1.0f, 0.0f, 0.0f, 1.0f), fractionOfWidth);
            DrawColoredRectAndReadOnlyU32("##Y", value.y, ImVec4(0.0f, 1.0f, 0.0f, 1.0f), fractionOfWidth);
            DrawColoredRectAndReadOnlyU32("##Z", value.z, ImVec4(0.0f, 0.0f, 1.0f, 1.0f), fractionOfWidth);
            DrawColoredRectAndReadOnlyU32("##W", value.w, ImVec4(1.0f, 1.0f, 1.0f, 1.0f), fractionOfWidth);

            ImGui::PopID();

            return false;
        }

        bool Inspect(const char* name, std::string& value, f32 speed)
        {
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%s", name);

            ImGui::PushID(name);
            bool isDirty = false;

            ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);
            isDirty |= ImGui::InputText("##value", &value);
            ImGui::PopItemWidth();

            ImGui::PopID();

            return isDirty;
        }

        bool Inspect(const char* name, const std::string& value, f32 speed)
        {
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%s", name);

            ImGui::PushID(name);

            ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);
            ImGui::InputText("##value", const_cast<char*>(value.c_str()), value.size(), ImGuiInputTextFlags_ReadOnly);
            ImGui::PopItemWidth();

            ImGui::PopID();

            return false;
        }

        void GroupHeader(const std::string& headerName)
        {
            f32 centerAlignPos = (ImGui::GetWindowWidth() - ImGui::CalcTextSize(headerName.c_str()).x) * 0.5f;
            ImGui::SetCursorPosX(centerAlignPos);

            ImGui::TextWrapped("%s", headerName.c_str());
        }

        void FloatSlider(const std::string& text, f32* valuePtr, f32 minVal, f32 maxVal, f32 step, f32 fastStep,
            bool arrowsEnabled, const char* format, ImGuiSliderFlags sliderFlags, f32 sliderWidth, const std::string& append)
        {
            f32 frameHeight = ImGui::GetFrameHeight();
            f32 currentFontSize = ImGui::GetDrawListSharedData()->FontSize;
            f32 arrowSize = frameHeight * 0.45f;
            f32 arrowSpacing = frameHeight - 2.0f * arrowSize;
            f32 spacing = ImGui::GetStyle().ItemInnerSpacing.x;

            ImGui::TextWrapped("%s", text.c_str());

            if (sliderWidth == ImGui::GetWindowWidth())
                sliderWidth -= frameHeight;

            ImGui::SetNextItemWidth(sliderWidth);
            ImGui::SliderFloat(("##" + text + append).c_str(), valuePtr, minVal, maxVal, format, sliderFlags);

            HoveredMouseWheelStep(valuePtr, step, fastStep);

            if (arrowsEnabled)
            {
                ImGui::SameLine(0.0f, 1.0f);

                ImGui::BeginGroup();

                ImGui::GetDrawListSharedData()->FontSize = arrowSize;
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, vec2{ ImGui::GetCurrentContext()->Style.ItemSpacing.x, arrowSpacing });

                ImGui::PushButtonRepeat(true);

                if (ImGui::ArrowButtonEx(("##right" + text + append).c_str(), ImGuiDir_Up, vec2(arrowSize, arrowSize)))
                {
                    if (ImGui::GetIO().KeyShift)
                    {
                        *(valuePtr) += fastStep;
                    }
                    else
                    {
                        *(valuePtr) += step;
                    }
                }

                HoveredMouseWheelStep(valuePtr, step, fastStep);

                if (ImGui::ArrowButtonEx(("##left" + text + append).c_str(), ImGuiDir_Down, vec2(arrowSize, arrowSize)))
                {
                    if (ImGui::GetIO().KeyShift)
                    {
                        *(valuePtr) -= fastStep;
                    }
                    else
                    {
                        *(valuePtr) -= step;
                    }
                }

                HoveredMouseWheelStep(valuePtr, step, fastStep);

                ImGui::GetDrawListSharedData()->FontSize = currentFontSize;
                ImGui::PopStyleVar();
                ImGui::PopButtonRepeat();
                ImGui::EndGroup();
            }

            *valuePtr = glm::clamp(*valuePtr, minVal, maxVal);
        }

        void ColorPicker(const std::string& name, Color* valuePtr, vec2 size, const std::string& append)
        {
            // Make name optional?
            //ImGui::TextWrapped("%s", (name).c_str());

            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_Button, *reinterpret_cast<ImVec4*>(valuePtr));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, *reinterpret_cast<ImVec4*>(valuePtr));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, *reinterpret_cast<ImVec4*>(valuePtr));

            f32 centerAlignPos = (ImGui::GetWindowWidth() - size.x) * 0.5f;
            ImGui::SetCursorPosX(centerAlignPos);

            if (ImGui::Button(("##" + name + append + "Button").c_str(), size))
                ImGui::OpenPopup((name + append + "popup").c_str());

            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            {
                ImGui::BeginTooltip();
                ImGui::Text("Click to edit!");
                ImGui::EndTooltip();
            }

            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar();

            if (ImGui::BeginPopup((name + append + "popup").c_str()))
            {
                ImGui::ColorPicker4(("##" + name + append + "Label").c_str(), reinterpret_cast<f32*>(valuePtr),
                    ImGuiColorEditFlags_PickerHueWheel | ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreview
                    | ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoSmallPreview);

                ImGui::EndPopup();
            }
        }
        void ColumnRadioButton(const std::string& valueName, i32* valuePtr, i32 brushType)
        {
            ImGui::TableNextColumn();

            // Need to add center alignment

            ImGui::RadioButton(valueName.c_str(), valuePtr, brushType);
        }
    }
}