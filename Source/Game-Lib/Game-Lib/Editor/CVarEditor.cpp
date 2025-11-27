#include "CVarEditor.h"

#include <Base/CVarSystem/CVarSystemPrivate.h>
#include <Base/Util/DebugHandler.h>

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include <map>
#include <stack>
#include <string>

namespace Editor
{
    CVarEditor::CVarEditor()
        : BaseEditor(GetName(), BaseEditorFlags_DefaultVisible | BaseEditorFlags_HideInMenuBar)
    {

    }

    void CVarEditor::DrawImGuiSubMenuBar()
    {
        if (ImGui::BeginMenu(GetName()))
        {
            CVarSystemImpl* cvarSystem = CVarSystemImpl::Get();

            static std::string searchText = "";

            ImGui::Text("Filter");
            ImGui::InputText("##CVarFilter", &searchText);
            static bool bShowAdvanced = false;
            ImGui::Checkbox("Advanced", &bShowAdvanced);
            static bool bShowHidden = false;
            ImGui::Checkbox("Hidden", &bShowHidden);

            cachedEditParameters.clear();
            float maxTextWidth = 0;

            auto addToEditList = [&](auto parameter)
            {
                bool bHidden = ((u32)parameter->flags & (u32)CVarFlags::Hidden);
                bool bIsAdvanced = ((u32)parameter->flags & (u32)CVarFlags::Advanced);

                if (!bHidden || bShowHidden)
                {
                    if (!(!bShowAdvanced && bIsAdvanced) && parameter->name.find(searchText) != std::string::npos)
                    {
                        cachedEditParameters.push_back(parameter);
                        maxTextWidth = std::max(maxTextWidth, ImGui::CalcTextSize(parameter->name.c_str()).x);
                    };
                }
            };

            for (int i = 0; i < cvarSystem->GetCVarArray<i32>()->lastCVar; i++)
            {
                addToEditList(cvarSystem->GetCVarArray<i32>()->cvars[i].parameter);
            }

            for (int i = 0; i < cvarSystem->GetCVarArray<f64>()->lastCVar; i++)
            {
                addToEditList(cvarSystem->GetCVarArray<f64>()->cvars[i].parameter);
            }

            for (int i = 0; i < cvarSystem->GetCVarArray<std::string>()->lastCVar; i++)
            {
                addToEditList(cvarSystem->GetCVarArray<std::string>()->cvars[i].parameter);
            }

            for (int i = 0; i < cvarSystem->GetCVarArray<vec4>()->lastCVar; i++)
            {
                addToEditList(cvarSystem->GetCVarArray<vec4>()->cvars[i].parameter);
            }

            for (int i = 0; i < cvarSystem->GetCVarArray<ivec4>()->lastCVar; i++)
            {
                addToEditList(cvarSystem->GetCVarArray<ivec4>()->cvars[i].parameter);
            }

            for (int i = 0; i < cvarSystem->GetCVarArray<ShowFlag>()->lastCVar; i++)
            {
                addToEditList(cvarSystem->GetCVarArray<ShowFlag>()->cvars[i].parameter);
            }

            //alphabetical sort
            std::sort(cachedEditParameters.begin(), cachedEditParameters.end(), [](CVarParameter* A, CVarParameter* B)
            {
                if (A->category == B->category)
                {
                    return A->name < B->name;
                }

                return A->category > B->category;
            });
            
            std::map<std::string, std::vector<CVarParameter*>> categorizedParams;
            std::stack<u8> closeStack; // 0 = PopID, 1 = EndMenu;

            //insert all the edit parameters into the hashmap by category
            for (auto p : cachedEditParameters)
            {
                NC_ASSERT(closeStack.size() == 0, "Stack not empty!");

                u32 intCategory = static_cast<u32>(p->category);
                if (intCategory == 0)
                    continue;

                bool displayParameter = true;
                u32 depthLevel = 0;
                for (u32 i = 0; i < static_cast<u32>(CVarCategory::COUNT); i++)
                {
                    u32 mask = (1 << i);
                    if (intCategory & mask)
                    {
                        depthLevel++;

                        const char* categoryName = CVarCategoryToString[i];

                        std::string idName = "Depth" + std::to_string(depthLevel) + categoryName;
                        ImGui::PushID(idName.c_str());
                        closeStack.push(0);

                        if (ImGui::BeginMenu(categoryName))
                        {
                            closeStack.push(1);
                        }
                        else
                        {
                            displayParameter = false;
                            break;
                        }
                    }
                }
                
                if (displayParameter)
                {
                    EditParameter(p, maxTextWidth);
                }

                while (!closeStack.empty())
                {
                    u8 command = closeStack.top();

                    if (command == 0)
                    {
                        ImGui::PopID();
                    }
                    else if (command == 1)
                    {
                        ImGui::EndMenu();
                    }

                    closeStack.pop();
                }
            }
            ImGui::EndMenu();
        }
    }

    void CVarEditor::DrawImGui()
    {
    }

    void Label(const char* label, float textWidth)
    {
        constexpr float Slack = 50;
        constexpr float EditorWidth = 200;

        //ImGuiWindow* window = ImGui::GetCurrentWindow();
        //const ImVec2 lineStart = ImGui::GetCursorScreenPos();
        //const ImGuiStyle& style = ImGui::GetStyle();
        float fullWidth = textWidth + Slack;

        //ImVec2 textSize = ImGui::CalcTextSize(label);

        ImVec2 startPos = ImGui::GetCursorScreenPos();

        ImGui::Text("%s", label);

        ImVec2 finalPos = { startPos.x + fullWidth, startPos.y };

        ImGui::SameLine();
        ImGui::SetCursorScreenPos(finalPos);

        ImGui::SetNextItemWidth(EditorWidth);
    }

    void CVarEditor::EditParameter(CVarParameter* p, float textWidth)
    {
        CVarSystemImpl* cvarSystem = CVarSystemImpl::Get();

        const bool readonlyFlag = ((u32)p->flags & (u32)CVarFlags::EditReadOnly);
        const bool checkboxFlag = ((u32)p->flags & (u32)CVarFlags::EditCheckbox);
        const bool dragFlag = ((u32)p->flags & (u32)CVarFlags::EditFloatDrag);

        // push the ID of the current parameter so that we can have empty labels with no conflicts
        ImGui::PushID(p->name.c_str());

        switch (p->type)
        {
        case CVarType::INT:
        {
            if (readonlyFlag)
            {
                std::string displayFormat = p->name + "= %i";
                ImGui::Text(displayFormat.c_str(), cvarSystem->GetCVarArray<i32>()->GetCurrent(p->arrayIndex));
            }
            else
            {
                Label(p->name.c_str(), textWidth);
                if (checkboxFlag)
                {
                    bool bCheckbox = cvarSystem->GetCVarArray<i32>()->GetCurrent(p->arrayIndex) != 0;
                    if (ImGui::Checkbox("", &bCheckbox))
                    {
                        cvarSystem->GetCVarArray<i32>()->SetCurrent(bCheckbox ? 1 : 0, p->arrayIndex);
                    }
                }
                else
                {
                    i32 val = cvarSystem->GetCVarArray<i32>()->GetCurrent(p->arrayIndex);
                    if (ImGui::InputInt("", &val))
                    {
                        cvarSystem->GetCVarArray<i32>()->SetCurrent(val, p->arrayIndex);
                    }
                }
            }
            break;
        }
        case CVarType::FLOAT:
        {
            if (readonlyFlag)
            {
                std::string displayFormat = p->name + "= %f";
                ImGui::Text(displayFormat.c_str(), cvarSystem->GetCVarArray<f64>()->GetCurrent(p->arrayIndex));
            }
            else
            {
                Label(p->name.c_str(), textWidth);
                if (dragFlag)
                {
                    // TODO: Should this be a different kind of input for dragging?
                    f64 val = cvarSystem->GetCVarArray<f64>()->GetCurrent(p->arrayIndex);
                    if (ImGui::InputDouble("", &val))
                    {
                        cvarSystem->GetCVarArray<f64>()->SetCurrent(val, p->arrayIndex);
                    }
                }
                else
                {
                    f64 val = cvarSystem->GetCVarArray<f64>()->GetCurrent(p->arrayIndex);
                    if (ImGui::InputDouble("", &val))
                    {
                        cvarSystem->GetCVarArray<f64>()->SetCurrent(val, p->arrayIndex);
                    }
                }
            }
            break;
        }
        case CVarType::FLOATVEC:
        {
            Label(p->name.c_str(), textWidth);
            vec4 val = cvarSystem->GetCVarArray<vec4>()->GetCurrent(p->arrayIndex);
            if (ImGui::InputFloat4("", &(val)[0]))
            {
                cvarSystem->GetCVarArray<vec4>()->SetCurrent(val, p->arrayIndex);
            }

            break;
        }
        case CVarType::INTVEC:
        {
            Label(p->name.c_str(), textWidth);
            ivec4 val = cvarSystem->GetCVarArray<ivec4>()->GetCurrent(p->arrayIndex);
            if (ImGui::InputInt4("", &(val)[0]))
            {
                cvarSystem->GetCVarArray<ivec4>()->SetCurrent(val, p->arrayIndex);
            }

            break;
        }
        case CVarType::STRING:
        {
            if (readonlyFlag)
            {
                std::string displayFormat = p->name + "= %s";
                ImGui::Text(displayFormat.c_str(), cvarSystem->GetCVarArray<std::string>()->GetCurrent(p->arrayIndex).c_str());
            }
            else
            {
                Label(p->name.c_str(), textWidth);
                std::string val = cvarSystem->GetCVarArray<std::string>()->GetCurrent(p->arrayIndex);
                if (ImGui::InputText("", &val))
                {
                    cvarSystem->GetCVarArray<std::string>()->SetCurrent(val, p->arrayIndex);
                }
            }
            break;
        }
        case CVarType::SHOWFLAG:
        {
            Label(p->name.c_str(), textWidth);
            bool enabled = cvarSystem->GetCVarArray<ShowFlag>()->GetCurrent(p->arrayIndex) == ShowFlag::ENABLED;

            if (ImGui::Checkbox("", &enabled))
            {
                cvarSystem->GetCVarArray<ShowFlag>()->SetCurrent(enabled ? ShowFlag::ENABLED : ShowFlag::DISABLED, p->arrayIndex);
            }
            break;
        }
        default:
            break;
        }

        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("%s", p->description.c_str());
        }

        ImGui::PopID();
    }
}