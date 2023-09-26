#include "CVarEditor.h"

#include <Base/CVarSystem/CVarSystemPrivate.h>

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include <map>
#include <string>

namespace Editor
{
	CVarEditor::CVarEditor()
        : BaseEditor(GetName(), false)
	{

	}

	void CVarEditor::DrawImGuiSubMenuBar()
	{
        if (ImGui::BeginMenu(GetName()))
        {
            CVarSystemImpl* cvarSystem = CVarSystemImpl::Get();

            static std::string searchText = "";

            ImGui::InputText("Filter", &searchText);
            static bool bShowAdvanced = false;
            ImGui::Checkbox("Advanced", &bShowAdvanced);

            cachedEditParameters.clear();

            auto addToEditList = [&](auto parameter)
            {
                bool bHidden = ((u32)parameter->flags & (u32)CVarFlags::Noedit);
                bool bIsAdvanced = ((u32)parameter->flags & (u32)CVarFlags::Advanced);

                if (!bHidden)
                {
                    if (!(!bShowAdvanced && bIsAdvanced) && parameter->name.find(searchText) != std::string::npos)
                    {
                        cachedEditParameters.push_back(parameter);
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

            if (cachedEditParameters.size() > 10)
            {
                std::map<std::string, std::vector<CVarParameter*>> categorizedParams;

                //insert all the edit parameters into the hashmap by category
                for (auto p : cachedEditParameters)
                {
                    int dotPos = -1;
                    //find where the first dot is to categorize
                    for (int i = 0; i < p->name.length(); i++)
                    {
                        if (p->name[i] == '.')
                        {
                            dotPos = i;
                            break;
                        }
                    }
                    std::string category = "";
                    if (dotPos != -1)
                    {
                        category = p->name.substr(0, dotPos);
                    }

                    auto it = categorizedParams.find(category);
                    if (it == categorizedParams.end())
                    {
                        categorizedParams[category] = std::vector<CVarParameter*>();
                        it = categorizedParams.find(category);
                    }
                    it->second.push_back(p);
                }

                for (auto [category, parameters] : categorizedParams)
                {
                    //alphabetical sort
                    std::sort(parameters.begin(), parameters.end(), [](CVarParameter* A, CVarParameter* B)
                    {
                        return A->name < B->name;
                    });

                    if (ImGui::BeginMenu(category.c_str()))
                    {
                        float maxTextWidth = 0;

                        for (auto p : parameters)
                        {
                            maxTextWidth = std::max(maxTextWidth, ImGui::CalcTextSize(p->name.c_str()).x);
                        }

                        for (auto p : parameters)
                        {
                            EditParameter(p, maxTextWidth);
                        }

                        ImGui::EndMenu();
                    }
                }
            }
            else
            {
                //alphabetical sort
                std::sort(cachedEditParameters.begin(), cachedEditParameters.end(), [](CVarParameter* A, CVarParameter* B)
                {
                    return A->name < B->name;
                });

                float maxTextWidth = 0;

                for (auto p : cachedEditParameters)
                {
                    maxTextWidth = std::max(maxTextWidth, ImGui::CalcTextSize(p->name.c_str()).x);
                }

                for (auto p : cachedEditParameters)
                {
                    EditParameter(p, maxTextWidth);
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
                        cvarSystem->GetCVarArray<i32>()->SetCurrent(bCheckbox ? 1 : 0, p->arrayIndex);

                    // intCVars[p->arrayIndex].current = bCheckbox ? 1 : 0;
                }
                else
                {
                    if (ImGui::InputInt("", cvarSystem->GetCVarArray<i32>()->GetCurrentPtr(p->arrayIndex)))
                        cvarSystem->MarkDirty();
                }
            }
            break;

        case CVarType::FLOAT:

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
                    if (ImGui::InputDouble("", cvarSystem->GetCVarArray<f64>()->GetCurrentPtr(p->arrayIndex)))
                        cvarSystem->MarkDirty();
                }
                else
                {
                    if (ImGui::InputDouble("", cvarSystem->GetCVarArray<f64>()->GetCurrentPtr(p->arrayIndex)))
                        cvarSystem->MarkDirty();
                }
            }
            break;

        case CVarType::FLOATVEC:
            Label(p->name.c_str(), textWidth);
            if (ImGui::InputFloat4("", &(*cvarSystem->GetCVarArray<vec4>()->GetCurrentPtr(p->arrayIndex))[0]))
                cvarSystem->MarkDirty();

            break;

        case CVarType::INTVEC:
            Label(p->name.c_str(), textWidth);
            if (ImGui::InputInt4("", &(*cvarSystem->GetCVarArray<ivec4>()->GetCurrentPtr(p->arrayIndex))[0]))
                cvarSystem->MarkDirty();

            break;

        case CVarType::STRING:

            if (readonlyFlag)
            {
                std::string displayFormat = p->name + "= %s";
                ImGui::Text(displayFormat.c_str(), cvarSystem->GetCVarArray<std::string>()->GetCurrent(p->arrayIndex).c_str());
            }
            else
            {
                Label(p->name.c_str(), textWidth);
                if (ImGui::InputText("", cvarSystem->GetCVarArray<std::string>()->GetCurrentPtr(p->arrayIndex)))
                    cvarSystem->MarkDirty();
            }
            break;

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