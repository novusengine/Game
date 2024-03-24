#include "Inspector.h"

#include <Game/Util/ServiceLocator.h>
#include <Game/Util/ImguiUtil.h>
#include <Game/Application/EnttRegistries.h>
#include <Game/Rendering/GameRenderer.h>
#include <Game/Rendering/Terrain/TerrainRenderer.h>
#include <Game/Rendering/Model/ModelLoader.h>
#include <Game/Rendering/Model/ModelRenderer.h>
#include <Game/Rendering/Debug/DebugRenderer.h>
#include <Game/Rendering/PixelQuery.h>
#include <Game/Rendering/Camera.h>
#include <Game/ECS/Singletons/MapDB.h>
#include <Game/ECS/Singletons/TextureSingleton.h>
#include <Game/ECS/Singletons/ActiveCamera.h>
#include <Game/ECS/Singletons/FreeflyingCameraSettings.h>
#include <Game/ECS/Singletons/RenderState.h>
#include <Game/ECS/Components/Camera.h>
#include <Game/ECS/Components/Model.h>
#include <Game/ECS/Components/Name.h>
#include <Game/ECS/Util/Transforms.h>
#include <Game/Editor/EditorHandler.h>
#include <Game/Editor/ActionStack.h>
#include <Game/Editor/Hierarchy.h>
#include <Game/Editor/Viewport.h>

#include <Renderer/RenderSettings.h>

#include <Base/CVarSystem/CVarSystem.h>
#include <Base/CVarSystem/CVarSystemPrivate.h>

#include <GLFW/glfw3.h>
#include <entt/entt.hpp>
#include <tracy/Tracy.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <imgui/imguizmo/ImGuizmo.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

namespace Editor
{
    AutoCVar_Int CVAR_InspectorEnabled("editor.inspector.Enable", "enable editor mode for the client", 1, CVarFlags::EditCheckbox);

    AutoCVar_ShowFlag CVAR_InspectorOBBShowFlag("editor.showflags.InspectorOBB", "draw OBB for selected object", ShowFlag::DISABLED);
    AutoCVar_ShowFlag CVAR_InspectorWorldAABBShowFlag("editor.showflags.InspectorWorldAABB", "draw world AABB for selected object", ShowFlag::DISABLED);

    struct MoveModelAction : public BaseAction
    {
        MoveModelAction(u32 instanceID, mat4x4 preEditValue)
            : BaseAction()
            , _instanceID(instanceID)
            , _preEditValue(preEditValue)
        {

        }

        virtual char* GetActionName() override
        {
            static char buffer[]{"Transform Model"};

            return buffer;
        }

        virtual void Undo() override
        {
            GameRenderer* gameRenderer = ServiceLocator::GetGameRenderer();
            ModelRenderer* modelRenderer = gameRenderer->GetModelRenderer();

            Renderer::GPUVector<mat4x4>& instances = modelRenderer->GetInstanceMatrices();

            instances.Get()[_instanceID] = _preEditValue;
            instances.SetDirtyElement(_instanceID);
        }

        virtual void Draw() override
        {
            GameRenderer* gameRenderer = ServiceLocator::GetGameRenderer();
            ModelLoader* modelLoader = gameRenderer->GetModelLoader();
            ModelRenderer* modelRenderer = gameRenderer->GetModelRenderer();

            u32 modelID;
            modelLoader->GetModelIDFromInstanceID(_instanceID, modelID);

            ModelLoader::DiscoveredModel& discoveredModel = modelLoader->GetDiscoveredModelFromModelID(modelID);

            ImGui::Indent();

            ImGui::Text("Instance: %i, Model: %i", _instanceID, modelID);
            ImGui::SameLine(0.0f, 10.0f);

            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(170, 170, 170, 255));
            ImGui::Text("%s", discoveredModel.name.c_str());
            ImGui::PopStyleColor();

            ImGui::Unindent();
        }

    private:
        u32 _instanceID;
        mat4x4 _preEditValue;
    };

    Inspector::Inspector()
        : BaseEditor(GetName(), true)
    {
        InputManager* inputManager = ServiceLocator::GetInputManager();
        KeybindGroup* keybindGroup = inputManager->GetKeybindGroupByHash("Imgui"_h);

        keybindGroup->AddKeyboardCallback("Mouse Left", GLFW_MOUSE_BUTTON_LEFT, KeybindAction::Press, KeybindModifier::ModNone | KeybindModifier::Shift, std::bind(&Inspector::OnMouseClickLeft, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    }

    void Inspector::SetViewport(Viewport* viewport)
    {
        _viewport = viewport;
    }

    void Inspector::SetHierarchy(Hierarchy* hierarchy)
    {
        _hierarchy = hierarchy;
    }

    void Inspector::Update(f32 deltaTime)
    {
        GameRenderer* gameRenderer = ServiceLocator::GetGameRenderer();
        PixelQuery* pixelQuery = gameRenderer->GetPixelQuery();

        bool hasQueryToken = _queriedToken != 0;
        bool hasActiveToken = _activeToken != 0;
        bool hasNewSelection = false;

        if (hasQueryToken)
        {
            PixelQuery::PixelData pixelData;
            if (pixelQuery->GetQueryResult(_queriedToken, pixelData))
            {
                // Here we free the currently active token (If set)
                if (hasActiveToken)
                {
                    pixelQuery->FreeToken(_activeToken);
                    _activeToken = 0;
                }

                if (pixelData.type == QueryObjectType::None)
                {
                    pixelQuery->FreeToken(_queriedToken);
                    _queriedToken = 0;
                }
                else
                {
                    _activeToken = _queriedToken;
                    _queriedToken = 0;

                    hasActiveToken = true;
                    hasNewSelection = true;
                }
            }
        }

        PixelQuery::PixelData pixelData;
        if (hasActiveToken)
        {
            if (pixelQuery->GetQueryResult(_activeToken, pixelData))
            {
                if (hasNewSelection)
                {
                    if (pixelData.type == QueryObjectType::Terrain)
                    {
                        // Terrain is not an entity so we can't select it for now
                    }
                    else if (pixelData.type == QueryObjectType::ModelOpaque || pixelData.type == QueryObjectType::ModelTransparent)
                    {

                        GameRenderer* gameRenderer = ServiceLocator::GetGameRenderer();
                        ModelRenderer* modelRenderer = gameRenderer->GetModelRenderer();

                        bool isOpaque = pixelData.type == QueryObjectType::ModelOpaque;

                        u32 instanceID = modelRenderer->GetInstanceIDFromDrawCallID(pixelData.value, isOpaque);

                        SelectModel(instanceID);
                    }
                }
            }
        }
    }

    void Inspector::DrawImGui()
    {
        ZoneScoped;

        GameRenderer* gameRenderer = ServiceLocator::GetGameRenderer();
        DebugRenderer* debugRenderer = gameRenderer->GetDebugRenderer();

        if (ImGui::Begin(GetName(), &IsVisible()))
        {
            if (_selectedEntity == entt::null)
            {
                ImGui::TextWrapped("Welcome to the Inspector window. In the inspector window you can see information about the selected entity. To start, click on an entity in the world or select one in the Hierarchy window.");
            }
            else
            {
                InspectEntity(_selectedEntity);
            }
        }
        ImGui::End();

        DrawGizmoControls();
    }

    void Inspector::SelectEntity(entt::entity entity)
    {
        _selectedEntity = entity;
    }

    void Inspector::ClearSelection()
    {
        if (_activeToken != 0)
        {
            GameRenderer* gameRenderer = ServiceLocator::GetGameRenderer();
            PixelQuery* pixelQuery = gameRenderer->GetPixelQuery();

            pixelQuery->FreeToken(_activeToken);
            _activeToken = 0;
        }

        _hierarchy->SelectEntity(entt::null);
        _selectedEntity = entt::null;
    }

    void Inspector::DirtySelection()
    {

    }

    bool Inspector::OnMouseClickLeft(i32 key, KeybindAction action, KeybindModifier modifier)
    {
        if (!CVAR_InspectorEnabled.Get())
            return false;

        ImGuiContext* context = ImGui::GetCurrentContext();
        if (context)
        {
            bool imguiItemHovered = ImGui::IsAnyItemHovered();
            if (!imguiItemHovered && context->HoveredWindow)
            {
                imguiItemHovered |= strcmp(context->HoveredWindow->Name, _viewport->GetName()) != 0;
            }

            if (imguiItemHovered || ImGuizmo::IsOver())
                return false;
        }

        ZoneScoped;

        InputManager* inputManager = ServiceLocator::GetInputManager();
        GameRenderer* gameRenderer = ServiceLocator::GetGameRenderer();
        PixelQuery* pixelQuery = gameRenderer->GetPixelQuery();

        if (_queriedToken != 0)
        {
            pixelQuery->FreeToken(_queriedToken);
            _queriedToken = 0;
        }

        // Shift Click clears selection
        if ((modifier & KeybindModifier::Shift) != KeybindModifier::Invalid)
        {
            ClearSelection();
            return false;
        }

        vec2 mousePos;
        if (!_viewport->GetMousePosition(mousePos))
        {
            return false;
        }

        // Check if we need to free the previous _queriedToken
        {
            if (_queriedToken != 0)
            {
                pixelQuery->FreeToken(_queriedToken);
                _queriedToken = 0;
            }
        }

        _queriedToken = pixelQuery->PerformQuery(uvec2(mousePos));
        return true;
    }

    void Inspector::SelectModel(u32 instanceID)
    {
        GameRenderer* gameRenderer = ServiceLocator::GetGameRenderer();
        ModelLoader* modelLoader = gameRenderer->GetModelLoader();
        ModelRenderer* modelRenderer = gameRenderer->GetModelRenderer();

        entt::entity selectedEntity;
        if (modelLoader->GetEntityIDFromInstanceID(instanceID, selectedEntity))
        {
            _selectedEntity = selectedEntity;
            _hierarchy->SelectEntity(_selectedEntity);
        }
    }

    void Inspector::InspectEntity(entt::entity entity)
    {
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;

        ECS::Components::Name* name = registry->try_get<ECS::Components::Name>(entity);
        if (!name)
        {
            ImGui::Text("Selected entity (%d) has no name component", static_cast<i32>(entity));
        }

        Util::Imgui::Inspect(*name);

        InspectEntityTransforms(entity);

        ECS::Components::Model* model = registry->try_get<ECS::Components::Model>(entity);
        if (model)
        {
            Util::Imgui::Inspect(*model);
        }

        // Debug drawing
        DebugRenderer* debugRenderer = ServiceLocator::GetGameRenderer()->GetDebugRenderer();
        if (CVAR_InspectorOBBShowFlag.Get() == ShowFlag::ENABLED)
        {
            ECS::Components::AABB* aabb = registry->try_get<ECS::Components::AABB>(entity);
            ECS::Components::Transform* transform = registry->try_get<ECS::Components::Transform>(entity);
            if (transform && aabb)
            {
                // Apply the transform's position and scale to the AABB's center and extents
                glm::vec3 transformedCenter = transform->GetLocalPosition() + transform->GetLocalRotation() * (aabb->centerPos * transform->GetLocalScale());
                glm::vec3 transformedExtents = aabb->extents * transform->GetLocalScale();

                debugRenderer->DrawOBB3D(transformedCenter, transformedExtents, transform->GetLocalRotation(), Color::Red);
            }
        }
        if (CVAR_InspectorWorldAABBShowFlag.Get() == ShowFlag::ENABLED)
        {
            ECS::Components::WorldAABB* worldAABB = registry->try_get<ECS::Components::WorldAABB>(entity);
            if (worldAABB)
            {
                vec3 center = (worldAABB->min + worldAABB->max) / 2.0f;
                vec3 extents = (worldAABB->max - worldAABB->min) / 2.0f;
                debugRenderer->DrawAABB3D(center, extents, Color::Green);
            }
        }
    }

    void Inspector::InspectEntityTransforms(entt::entity entity)
    {
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;

        ECS::Components::Transform* transform = registry->try_get<ECS::Components::Transform>(entity);

        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
        
        if (transform)
        {
            DrawGizmo(registry, entity, *transform);

            if (Util::Imgui::BeginGroupPanel("Transform"))
            {
                bool needRefresh = false;
                
                //editing the transform component members directly to only go through the transform system for propagation once
                vec3 pos = transform->position;
                if (ImGui::DragFloat3("position", &pos.x))
                {
                    transform->position = pos;
                    needRefresh = true;
                }

                vec3 eulerAngles = glm::degrees(glm::eulerAngles(transform->rotation));
                if (ImGui::DragFloat3("rotation", &eulerAngles.x))
                {
                    transform->rotation = glm::quat(glm::radians(eulerAngles));
                    needRefresh = true;
                }

                vec3 scale = transform->scale;
                if (ImGui::DragFloat3("scale", &scale.x))
                {
                    transform->scale = scale;
                    needRefresh = true;
                }

                //if we have changed the transform components we need to update matrix and children
                if (needRefresh)
                {
                    //setting local position will refresh all
                    ECS::TransformSystem::Get(*registry).RefreshTransform(entity, *transform);
                }
            }
            Util::Imgui::EndGroupPanel();

            ECS::Components::SceneNode* node = registry->try_get<ECS::Components::SceneNode>(entity);
            if (node)
            {
                if (Util::Imgui::BeginGroupPanel("SceneNode"))
                {
                    if (node->parent)
                    {
                        ECS::Components::Name* name = registry->try_get<ECS::Components::Name>(node->parent->ownerEntity);
                        if (name)
                        {
                            ImGui::Text("Parent: ");
                            ImGui::SameLine();
                            if (ImGui::Button(name->name.c_str()))
                            {
                                SelectEntity(node->parent->ownerEntity);
                            }
                        }
                    }
                    else
                    {
                        ImGui::Text("Parent: %s", "none");
                    }

                    //draw children on a scroll bar
                    if (node->firstChild)
                    {
                        struct ChildElement
                        {
                            ECS::Components::Name* name;
                            entt::entity et;
                        };
                       
                        //copy all the children of this scenenode into array for display
                        std::vector<ChildElement> nameComps;
                        ECS::TransformSystem::Get(*registry).IterateChildren(entity, [&](ECS::Components::SceneNode* child)
                        {
                            if (child && child->ownerEntity != entt::null)
                            {
                                ECS::Components::Name* name = registry->try_get<ECS::Components::Name>(child->ownerEntity);
                                if (name)
                                {
                                    nameComps.push_back({name,child->ownerEntity});
                                }
                            }
                        });

                        ImGui::Text(" %zu children", nameComps.size());

                        if (ImGui::BeginChild("childcomps",{0.0f,200.0f}))
                        {
                            for (const auto& c : nameComps)
                            {   
                                //evil hackery converting an entityID into a void* to use on the ID system
                                ImGui::PushID((void*)c.et);
                                if (ImGui::Button(c.name->name.c_str()))
                                {
                                    SelectEntity(c.et);
                                }
                                ImGui::PopID();
                            }
                        }
                        ImGui::EndChild();
                    }
                }

                Util::Imgui::EndGroupPanel();
            }
        }
        ImGui::PopStyleColor();
    }

    bool Inspector::DrawGizmo(entt::registry* registry, entt::entity entity, ECS::Components::Transform& transform)
    {
        entt::registry::context& ctx = registry->ctx();

        ECS::Singletons::ActiveCamera& activeCamera = ctx.get<ECS::Singletons::ActiveCamera>();
        
        if (activeCamera.entity == entt::null)
            return false;

        ECS::Components::Camera& camera = registry->get<ECS::Components::Camera>(static_cast<entt::entity>(0));

        mat4x4& viewMatrix = camera.worldToView;
        mat4x4& projMatrix = camera.viewToClip;

        //we build a matrix to send to imguizmo by using local scale instead of world scale. This fixes issues with parented scales.
        mat4a tm = Math::AffineMatrix::TransformMatrix(transform.GetWorldPosition(), transform.GetWorldRotation(), transform.GetLocalScale());
        mat4x4 transformMatrix = tm;
        //affine matrices converted to mat4 need their last element set manually.
        transformMatrix[3][3] = 1.0f;

        float* instanceMatrixPtr = glm::value_ptr(transformMatrix);

        ImGuizmo::OPERATION operation = static_cast<ImGuizmo::OPERATION>(_operation);
        ImGuizmo::MODE mode = static_cast<ImGuizmo::MODE>(_mode);

        bool isDirty = ImGuizmo::Manipulate(glm::value_ptr(viewMatrix), glm::value_ptr(projMatrix), operation, operation == ImGuizmo::SCALE ? ImGuizmo::LOCAL : mode, instanceMatrixPtr, nullptr);

        if (isDirty)
        {
            vec3 eulerAngles;
            vec3 position = (transform.GetWorldPosition());
            vec3 scale = (transform.GetLocalScale());
            ImGuizmo::DecomposeMatrixToComponents(instanceMatrixPtr, glm::value_ptr(position), glm::value_ptr(eulerAngles), glm::value_ptr(scale));

            //only translation will work on child objects. need to fix later
            if (operation & ImGuizmo::TRANSLATE)
            {
                ECS::TransformSystem::Get(*registry).SetWorldPosition(entity, position);
            }
            else if (operation & ImGuizmo::ROTATE)
            {
                ECS::TransformSystem::Get(*registry).SetWorldRotation(entity, glm::quat(glm::radians(eulerAngles)));
            }
            else if (operation & ImGuizmo::SCALE)
            {
                ECS::TransformSystem::Get(*registry).SetLocalScale(entity, scale);
            }
            else
            {
                ECS::TransformSystem::Get(*registry).SetLocalTransform(entity, position, glm::quat(glm::radians(eulerAngles)), scale);
            }
        }

        return isDirty;
    }

    void Inspector::DrawGizmoControls()
    {
        if (ImGui::Begin(_viewport->GetName()))
        {
            if (ImGui::BeginChild("BottomBar"))
            {
                f32 xOffset = ImGui::GetContentRegionAvail().x;

                f32 frameHeight = ImGui::GetFrameHeight();
                f32 spacing = ImGui::GetStyle().ItemInnerSpacing.x;

                f32 showWidth = ImGui::CalcTextSize("Show").x + 10.0f;

                // Remove size of all our buttons
                xOffset -= showWidth;
                xOffset -= frameHeight + spacing + ImGui::CalcTextSize("Translate").x;
                xOffset -= frameHeight + spacing + ImGui::CalcTextSize("Rotate").x;
                xOffset -= frameHeight + spacing + ImGui::CalcTextSize("Scale").x;
                xOffset -= frameHeight + spacing + ImGui::CalcTextSize("Local").x;
                xOffset -= frameHeight + spacing + ImGui::CalcTextSize("World").x;
                xOffset -= 40.0f; // Some extra padding

                ImGui::SetCursorPos(vec2(xOffset, 0.0f));

                ImGui::PushItemWidth(showWidth);
                if (ImGui::BeginCombo("##Show", "Show", ImGuiComboFlags_NoArrowButton)) // The second argument is the label previewed before opening the combo.
                {
                    CVarSystemImpl* cvarSystem = CVarSystemImpl::Get();
                    for (i32 i = 0; i < cvarSystem->GetCVarArray<ShowFlag>()->lastCVar; i++)
                    {
                        CVarStorage<ShowFlag>& cvar = cvarSystem->GetCVarArray<ShowFlag>()->cvars[i];
                        CVarParameter* parameter = cvar.parameter;

                        bool isChecked = cvar.current == ShowFlag::ENABLED;
                        if (ImGui::Checkbox(parameter->name.c_str(), &isChecked))
                        {
                            cvar.current = isChecked ? ShowFlag::ENABLED : ShowFlag::DISABLED;
                            cvarSystem->MarkDirty();
                        }
                    }

                    ImGui::EndCombo();
                }
                ImGui::PopItemWidth();

                ImGui::SameLine();
                if (ImGui::RadioButton("Translate", _operation == ImGuizmo::TRANSLATE))
                    _operation = ImGuizmo::TRANSLATE;
                ImGui::SameLine();
                if (ImGui::RadioButton("Rotate", _operation == ImGuizmo::ROTATE))
                    _operation = ImGuizmo::ROTATE;
                ImGui::SameLine();
                if (ImGui::RadioButton("Scale", _operation == ImGuizmo::SCALE))
                    _operation = ImGuizmo::SCALE;
                ImGui::SameLine();
                if (ImGui::RadioButton("Local", _mode == ImGuizmo::LOCAL))
                    _mode = ImGuizmo::LOCAL;
                ImGui::SameLine();
                if (ImGui::RadioButton("World", _mode == ImGuizmo::WORLD))
                    _mode = ImGuizmo::WORLD;
            }
            ImGui::EndChild();
        }
        ImGui::End();
    }
}
