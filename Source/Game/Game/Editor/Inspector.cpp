#include "Inspector.h"
#include "ActionStack.h"
#include "EditorHandler.h"
//#include "Viewport.h"
#include "../Util/ServiceLocator.h"
#include "../Application/EnttRegistries.h"
//#include "../Util/MapUtils.h"

#include "../Rendering/GameRenderer.h"
#include "../Rendering/Terrain/TerrainRenderer.h"
#include "../Rendering/Model/ModelLoader.h"
#include "../Rendering/Model/ModelRenderer.h"
#include "../Rendering/Debug/DebugRenderer.h"
#include "../Rendering/PixelQuery.h"
#include "../Rendering/Camera.h"

#include "../ECS/Singletons/MapDB.h"
#include "../ECS/Singletons/TextureSingleton.h"
#include "../ECS/Singletons/ActiveCamera.h"
#include "../ECS/Singletons/FreeFlyingCameraSettings.h"
#include "../ECS/Components/Camera.h"
#include "../ECS/Components/Transform.h"

#include <Base/CVarSystem/CVarSystem.h>
#include <GLFW/glfw3.h>
#include <entt/entt.hpp>
#include <tracy/Tracy.hpp>

#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <imgui/imguizmo/ImGuizmo.h>
#include <imgui/misc/cpp/imgui_stdlib.h>
#include <Renderer/RenderSettings.h>

namespace Editor
{
    AutoCVar_Int CVAR_InspectorEnabled("editor.inspector.Enable", "enable editor mode for the client", 1, CVarFlags::EditCheckbox);

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
            return "Transform Model";
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
            ImGui::Text(discoveredModel.name.c_str());
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
        KeybindGroup* keybindGroup = inputManager->CreateKeybindGroup("Editor", 15);
        keybindGroup->SetActive(true);

        keybindGroup->AddKeyboardCallback("Mouse Left", GLFW_MOUSE_BUTTON_LEFT, KeybindAction::Press, KeybindModifier::None | KeybindModifier::Shift, std::bind(&Inspector::OnMouseClickLeft, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    }

    void Inspector::DrawImGui()
    {
        ZoneScoped;

        //entt::registry* gameRegistry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        //NDBCSingleton& ndbcSingleton = gameRegistry->ctx<NDBCSingleton>();

        GameRenderer* gameRenderer = ServiceLocator::GetGameRenderer();
        DebugRenderer* debugRenderer = gameRenderer->GetDebugRenderer();

        if (ImGui::Begin(GetName(), &_isVisible))
        {
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
                    if (pixelData.type == QueryObjectType::Terrain)
                    {
                        if (hasNewSelection)
                        {
                            const u32 packedChunkCellID = pixelData.value;
                            u32 cellID = packedChunkCellID & 0xffff;
                            u32 chunkID = packedChunkCellID >> 16;

                            SelectTerrain(chunkID, cellID, false);
                        }
                    }
                    else if (pixelData.type == QueryObjectType::ModelOpaque || pixelData.type == QueryObjectType::ModelTransparent)
                    {
                        if (hasNewSelection)
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

            if (_selectedObjectType == QueryObjectType::Terrain)
            {
                TerrainSelectionDrawImGui();
                debugRenderer->DrawAABB3D(_selectedTerrainData.boundingBox.center, _selectedTerrainData.boundingBox.extents, 0xFF0000FF);
            }
            else if (_selectedObjectType == QueryObjectType::ModelOpaque || _selectedObjectType == QueryObjectType::ModelTransparent)
            {
                ModelSelectionDrawImGui();
                debugRenderer->DrawAABB3D(_selectedModelData.boundingBox.center, _selectedModelData.boundingBox.extents, 0xFF0000FF);
            }

            if (pixelData.type == QueryObjectType::None)
            {
                ImGui::TextWrapped("Welcome to the editor window. In the editor window you can see information about what you are currently viewing. To start viewing, click on a map tile, map object or complex model.");
            }
            else
            {
                ImGui::Spacing();
                ImGui::Spacing();
                ImGui::TextWrapped("You can clear your selection by using 'Shift + Mouse Left'");
            }
        }
        ImGui::End();
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
        _selectedObjectType = QueryObjectType::None;
    }

    void Inspector::DirtySelection()
    {
        /*GameRenderer* gameRenderer = ServiceLocator::GetGameRenderer();
        ModelRenderer* modelRenderer = gameRenderer->GetModelRenderer();

        f32 radius;
        if (_selectedObjectType == QueryObjectType::ModelOpaque || _selectedObjectType == QueryObjectType::ModelTransparent)
        {
            // TODO: Bounding box of models
            //modelRenderer->CalculateBoundingBox(_selectedComplexModelData.instanceID, _selectedComplexModelData.boundingBox.center, _selectedComplexModelData.boundingBox.extents, radius);
        }*/
    }

    void Inspector::SelectTerrain(u32 chunkID, u32 cellID, bool selectChunk)
    {
        /*entt::registry* mainRegistry = ServiceLocator::GetMainRegistry();
        MapDB& mapDB = mainRegistry->ctx<MapDB>();
        NDBCSingleton& ndbcSingleton = mainRegistry->ctx<NDBCSingleton>();

        NDBC::File* areaTableFile = ndbcSingleton.GetNDBCFile("AreaTable"_h);

        NChunk* chunk = mapSingleton.GetCurrentMap().GetChunkById(chunkID);

        const u32 chunkX = chunkID % Terrain::NMAP_CHUNKS_PER_MAP_SIDE;
        const u32 chunkY = chunkID / Terrain::NMAP_CHUNKS_PER_MAP_SIDE;

        vec2 chunkOrigin;
        chunkOrigin.x = Terrain::NMAP_HALF_SIZE - (chunkX * Terrain::NMAP_CHUNK_SIZE);
        chunkOrigin.y = Terrain::NMAP_HALF_SIZE - (chunkY * Terrain::NMAP_CHUNK_SIZE);

        vec3 min;
        vec3 max;

        // The reason for the flip in X and Y here is because in 2D X is Left and Right, Y is Forward and Backward.
        // In our 3D coordinate space X is Forward and Backwards, Y is Left and Right.

        if (selectChunk)
        {
            // X and Y extents for the chunk
            min.x = chunkOrigin.y;
            min.y = chunkOrigin.x;

            max.x = chunkOrigin.y - (Terrain::NMAP_CHUNK_SIZE);
            max.y = chunkOrigin.x - (Terrain::NMAP_CHUNK_SIZE);

            // Z extents by minmaxing every cell
            min.z = std::numeric_limits<f32>::infinity();
            max.z = std::numeric_limits<f32>::lowest();

            for (u32 i = 0; i < Terrain::NMAP_CELLS_PER_CHUNK; i++)
            {
                const NCell& cell = chunk->nCells[i];
                const auto heightMinMax = std::minmax_element(cell.heightData, cell.heightData + Terrain::NMAP_CELL_TOTAL_GRID_SIZE);

                if (*heightMinMax.first < min.z)
                    min.z = *heightMinMax.first;

                if (*heightMinMax.second > max.z)
                    max.z = *heightMinMax.second;
            }
        }
        else
        {
            const NCell& cell = chunk->nCells[cellID];
            const u16 cellX = cellID % Terrain::NMAP_CELLS_PER_CHUNK_SIDE;
            const u16 cellY = cellID / Terrain::NMAP_CELLS_PER_CHUNK_SIDE;

            const auto heightMinMax = std::minmax_element(cell.heightData, cell.heightData + Terrain::NMAP_CELL_TOTAL_GRID_SIZE);

            min.x = chunkOrigin.y - (cellY * Terrain::NMAP_CELL_SIZE);
            min.y = chunkOrigin.x - (cellX * Terrain::NMAP_CELL_SIZE);
            min.z = *heightMinMax.first + 0.1f;

            max.x = chunkOrigin.y - ((cellY + 1) * Terrain::NMAP_CELL_SIZE);
            max.y = chunkOrigin.x - ((cellX + 1) * Terrain::NMAP_CELL_SIZE);
            max.z = *heightMinMax.second + 0.1f;
        }

        vec3 aabbMin = glm::max(min, max);
        vec3 aabbMax = glm::min(min, max);
        _selectedTerrainData.boundingBox.center = (aabbMin + aabbMax) * 0.5f;
        _selectedTerrainData.boundingBox.extents = aabbMax - _selectedTerrainData.boundingBox.center;

        _selectedTerrainData.chunkWorldPos.x = Terrain::NMAP_HALF_SIZE - (chunkY * Terrain::NMAP_CHUNK_SIZE);
        _selectedTerrainData.chunkWorldPos.y = Terrain::NMAP_HALF_SIZE - (chunkX * Terrain::NMAP_CHUNK_SIZE);
        _selectedTerrainData.chunkId = chunkID;
        _selectedTerrainData.cellId = cellID;

        _selectedTerrainData.selectedChunk = selectChunk;
        _selectedTerrainData.chunk = chunk;
        _selectedTerrainData.cell = &_selectedTerrainData.chunk->nCells[_selectedTerrainData.cellId];


        _selectedTerrainData.zone = _selectedTerrainData.cell ? areaTableFile->GetRowById<NDBC::AreaTable>(_selectedTerrainData.cell->areaID) : nullptr;
        _selectedTerrainData.area = nullptr;

        if (_selectedTerrainData.zone && _selectedTerrainData.zone->parentId)
        {
            _selectedTerrainData.area = _selectedTerrainData.zone;
            _selectedTerrainData.zone = areaTableFile->GetRowById<NDBC::AreaTable>(_selectedTerrainData.area->parentId);
        }

        _selectedObjectType = QueryObjectType::Terrain;*/
    }

    void Inspector::SelectModel(u32 instanceID)
    {
        GameRenderer* gameRenderer = ServiceLocator::GetGameRenderer();
        ModelLoader* modelLoader = gameRenderer->GetModelLoader();
        ModelRenderer* modelRenderer = gameRenderer->GetModelRenderer();

        _selectedModelData.entityID = entt::entity(0);
        modelLoader->GetModelIDFromInstanceID(instanceID, _selectedModelData.modelID);
        _selectedModelData.instanceID = instanceID;

        ModelLoader::DiscoveredModel& discoveredModel = modelLoader->GetDiscoveredModelFromModelID(_selectedModelData.modelID);

        // TODO: Bounding box of models
        //f32 radius;
        //modelRenderer->CalculateBoundingBox(instanceID, _selectedComplexModelData.boundingBox.center, _selectedComplexModelData.boundingBox.extents, radius);

        _selectedModelData.isOpaque = true; // TODO: Support selecting transparent drawcalls
        _selectedModelData.numRenderBatches = discoveredModel.modelHeader.numRenderBatches; // TODO: Support selecting transparent drawcalls loadedComplexModel.numTransparentDrawCalls;
        _selectedModelData.selectedRenderBatch = 1;

        _selectedObjectType = _selectedModelData.isOpaque ? QueryObjectType::ModelOpaque : QueryObjectType::ModelTransparent;
    }

    void Inspector::TerrainSelectionDrawImGui()
    {
        /*entt::registry* registry = ServiceLocator::GetMainRegistry();
        MapSingleton& mapSingleton = registry->ctx<MapSingleton>();
        TextureSingleton& textureSingleton = registry->ctx<TextureSingleton>();

        NDBCSingleton& ndbcSingleton = registry->ctx<NDBCSingleton>();
        NDBC::File* areaTableFile = ndbcSingleton.GetNDBCFile("AreaTable"_h);

        NChunk* chunk = _selectedTerrainData.chunk;
        NCell* cell = _selectedTerrainData.cell;

        if (!chunk || !cell)
            return;

        const NDBC::AreaTable* zone = _selectedTerrainData.zone;
        const NDBC::AreaTable* area = _selectedTerrainData.area;

        if (zone && zone->parentId)
        {
            area = zone;
            zone = areaTableFile->GetRowById<NDBC::AreaTable>(area->parentId);
        }

        ImGui::Text("Selected Chunk (%u)", _selectedTerrainData.chunkId);
        ImGui::BulletText("Zone: %s", zone ? areaTableFile->GetStringTable()->GetString(zone->name).c_str() : "No Zone Name");
        ImGui::BulletText("Map Object Placements: %u", chunk->mapObjectPlacements.size());
        ImGui::BulletText("Complex Model Placements: %u", chunk->complexModelPlacements.size());

        ImGui::Spacing();
        ImGui::Spacing();

        bool hasLiquid = false;// chunk->liquidHeaders.size() > 0 ? chunk->liquidHeaders[_selectedTerrainData.cellId].packedData != 0 : false;
        if (!_selectedTerrainData.selectedChunk)
        {
            ImGui::Text("Selected Cell (%u)", _selectedTerrainData.cellId);
            ImGui::BulletText("Area: %s", area ? areaTableFile->GetStringTable()->GetString(area->name).c_str() : "No Area Name");
            ImGui::BulletText("Area Id: %u, Has Holes: %u, Has Liquid: %u", cell->areaID, cell->hole > 0, hasLiquid);

            bool showTextures = ImGui::CollapsingHeader("Textures");
            if (showTextures)
            {
                ImGui::Spacing();
                ImGui::Spacing();

                if (cell)
                {
                    for (u32 i = 0; i < 4; i++)
                    {
                        NCell::LayerData& layerData = cell->layers[i];
                        if (layerData.textureID != Terrain::TEXTURE_ID_INVALID)
                        {
                            Renderer::TextureID textureID(textureSingleton.textureHashToTextureID[layerData.textureID]);
                            DrawTextureInspector(i, textureID);
                        }
                        else
                        {
                            ImGui::BulletText("Texture %u: Unused", i);
                        }
                    }
                }
            }
        }

        ImGui::Separator();
        bool showRenderOptions = ImGui::CollapsingHeader("Render Options");
        if (showRenderOptions)
        {
            ImGui::Checkbox("Draw Wireframe", &_selectedTerrainData.drawWireframe);
        }*/
    }

    void Inspector::ModelSelectionDrawImGui()
    {
        //Renderer::Renderer* renderer = ServiceLocator::GetRenderer();
        GameRenderer* gameRenderer = ServiceLocator::GetGameRenderer();
        ModelLoader* modelLoader = gameRenderer->GetModelLoader();
        ModelRenderer* modelRenderer = gameRenderer->GetModelRenderer();

        u32 modelID;
        if (!modelLoader->GetModelIDFromInstanceID(_selectedModelData.instanceID, modelID))
            return;

        ModelLoader::DiscoveredModel& discoveredModel = modelLoader->GetDiscoveredModelFromModelID(modelID);

        ImGui::Text("Complex Model");
        ImGui::Text("Instance Id (%u)", _selectedModelData.instanceID);
        ImGui::Text("ModelID : %u, Name: %s", modelID, discoveredModel.name.c_str());

        Renderer::GPUVector<mat4x4>& instanceMatrices = modelRenderer->GetInstanceMatrices();

        bool isDirty = false;

        mat4x4& instanceMatrix = instanceMatrices.Get()[_selectedModelData.instanceID];

        bool finishedAction = false;
        mat4x4 preEditMatrix;
        if (DrawTransformInspector(instanceMatrix, finishedAction, preEditMatrix))
        {
            instanceMatrices.SetDirtyElement(_selectedModelData.instanceID);
            isDirty = true;
        }
        if (finishedAction)
        {
            new MoveModelAction(_selectedModelData.instanceID, preEditMatrix);
        }

        vec3 pos = instanceMatrix[3];

        vec3 right = glm::normalize(instanceMatrix * vec4(ECS::Components::Transform::WORLD_RIGHT, 0.0f));
        vec3 up = glm::normalize(instanceMatrix * vec4(ECS::Components::Transform::WORLD_UP, 0.0f));
        vec3 forward = glm::normalize(instanceMatrix * vec4(ECS::Components::Transform::WORLD_FORWARD, 0.0f));

        DebugRenderer* debugRenderer = gameRenderer->GetDebugRenderer();
        debugRenderer->DrawLine3D(pos, pos + (right * 100.0f), 0xff0000ff);
        debugRenderer->DrawLine3D(pos, pos + (up * 100.0f), 0xff00ff00);
        debugRenderer->DrawLine3D(pos, pos + (forward * 100.0f), 0xffff0000);

        if (isDirty)
        {
            // TODO: Model bounding boxes
            //f32 radius;
            //cModelRenderer->CalculateBoundingBox(_selectedComplexModelData.instanceID, _selectedComplexModelData.boundingBox.center, _selectedComplexModelData.boundingBox.extents, radius);
        }

        // TODO: Animations
        /*bool hasAnimationEntries = _selectedComplexModelData.animationEntries.size() > 0;
        if (loadedComplexModel.isAnimated && hasAnimationEntries)
        {
            AnimationSystem* animationSystem = ServiceLocator::GetAnimationSystem();

            static const char* selectedAnimationName = nullptr;
            static const char* previewAnimationName = nullptr;

            u32& selectedAnimationEntry = _selectedComplexModelData.selectedAnimationEntry;
            previewAnimationName = _selectedComplexModelData.animationEntries[selectedAnimationEntry].name.c_str();

            ImGui::Separator();
            ImGui::Separator();
            ImGui::Text("Animation (ID: %u)", _selectedComplexModelData.animationEntries[selectedAnimationEntry].id);

            if (ImGui::BeginCombo("##", previewAnimationName)) // The second parameter is the label previewed before opening the combo.
            {
                for (u32 i = 0; i < _selectedComplexModelData.animationEntries.size(); i++)
                {
                    const CModelAnimationEntry& animationEntry = _selectedComplexModelData.animationEntries[i];
                    bool isSelected = selectedAnimationEntry == i;

                    if (ImGui::Selectable(animationEntry.name.c_str(), &isSelected))
                    {
                        selectedAnimationEntry = i;
                        selectedAnimationName = animationEntry.name.c_str();
                        previewAnimationName = animationEntry.name.c_str();
                    }

                    if (isSelected)
                        ImGui::SetItemDefaultFocus();
                }

                ImGui::EndCombo();
            }

            ImGui::SameLine();

            static bool value = false;;
            if (ImGui::Checkbox("Loop", &value)) {}

            if (ImGui::Button("Play"))
            {
                const CModelAnimationEntry& animationEntry = _selectedComplexModelData.animationEntries[selectedAnimationEntry];
                animationSystem->PlayActiveAnimation(_selectedComplexModelData.instanceID, animationEntry.id, animationEntry.variationID, value);
            }

            ImGui::SameLine();
            if (ImGui::Button("Stop"))
            {
                u32 animationID = _selectedComplexModelData.animationEntries[selectedAnimationEntry].id;
                animationSystem->StopActiveAnimation(_selectedComplexModelData.instanceID);
            }

            ImGui::SameLine();
            if (ImGui::Button("Stop All"))
            {
                animationSystem->ResetAllAnimations(_selectedComplexModelData.instanceID);
                animationSystem->PlayAlwaysPlayingAnimations(_selectedComplexModelData.instanceID);
            }
        }*/

        // TODO: Textures
        // Texture Units Inspector
        /*ImGui::Separator();
        ImGui::Separator();
        bool showTextureUnits = ImGui::CollapsingHeader("Texture Units");
        if (showTextureUnits)
        {
            Renderer::GPUVector<CModelRenderer::TextureUnit>& gpuTextureUnits = cModelRenderer->GetTextureUnits();
            Renderer::TextureArrayID textureArrayID = cModelRenderer->GetTextureArrayID();

            ImGui::Indent();
            SafeVectorScopedWriteLock textureUnitsLock(gpuTextureUnits);
            auto& textureUnits = textureUnitsLock.Get();

            u32 textureIndex = 0;
            for (const CModelRenderer::DrawCallData& drawCallData : loadedComplexModel.opaqueDrawCallDataTemplates)
            {
                for (u32 i = 0; i < drawCallData.numTextureUnits; i++)
                {
                    u32 textureUnitIndex = drawCallData.textureUnitOffset + i;
                    CModelRenderer::TextureUnit& textureUnit = textureUnits[textureUnitIndex];

                    ImGui::BulletText(std::to_string(textureUnitIndex).c_str());

                    ImGui::Indent();

                    //bool isProjectedTexture = (static_cast<u8>(complexTextureUnit.flags) & static_cast<u8>(CModel::ComplexTextureUnitFlag::PROJECTED_TEXTURE)) != 0;
                    //u16 materialFlag = *reinterpret_cast<u16*>(&complexMaterial.flags) << 1;
                    //u16 blendingMode = complexMaterial.blendingMode << 11;

                    bool isDirty = false;

                    bool isProjectedTexture = textureUnit.data & 0x1;
                    isDirty |= ImGui::Checkbox("Is Projected Texture", &isProjectedTexture);

                    u32 materialFlags = (textureUnit.data >> 1) & 0x3FF;

                    bool isUnlit = (materialFlags & 0x1);
                    isDirty |= ImGui::Checkbox("Is Unlit", &isUnlit);

                    i32 blendingMode = (textureUnit.data >> 11) & 0x7;
                    isDirty |= ImGui::InputInt("Blending Mode", &blendingMode);

                    i32 vertexShaderId = textureUnit.materialType & 0xFF;
                    isDirty |= ImGui::InputInt("Vertex Shader ID", &vertexShaderId);

                    i32 pixelShaderId = textureUnit.materialType >> 8;
                    isDirty |= ImGui::InputInt("Pixel Shader ID", &pixelShaderId);

                    if (isDirty)
                    {
                        gpuTextureUnits.SetDirtyElement(textureUnitIndex);
                    }

                    u32 numTextures = (vertexShaderId >= 2) ? 2 : 1;
                    for (u32 j = 0; j < numTextures; j++)
                    {
                        u32 textureArrayIndex = textureUnit.textureIds[j];
                        Renderer::TextureID textureID = renderer->GetTextureID(textureArrayID, textureArrayIndex);

                        if (textureID != Renderer::TextureID::Invalid())
                        {
                            DrawTextureInspector(textureIndex++, textureID);
                        }
                        else
                        {
                            ImGui::BulletText("Texture %u: Unused", j);
                        }
                    }
                    ImGui::Unindent();
                }


            }
            ImGui::Unindent();
        }*/

        if (_selectedModelData.numRenderBatches)
        {
            ImGui::Separator();
            bool showRenderOptions = ImGui::CollapsingHeader("Render Options");
            if (showRenderOptions)
            {
                ImGui::Text("Render Batch (%i/%u)", _selectedModelData.selectedRenderBatch, _selectedModelData.numRenderBatches);
                if (ImGui::InputInt("##", &_selectedModelData.selectedRenderBatch, 1, 1))
                {
                    i32 minValue = 1;
                    i32 maxValue = static_cast<i32>(_selectedModelData.numRenderBatches);

                    _selectedModelData.selectedRenderBatch = glm::clamp(_selectedModelData.selectedRenderBatch, minValue, maxValue);
                }

                ImGui::Checkbox("Draw Wireframe", &_selectedModelData.drawWireframe);
                ImGui::Checkbox("Wireframe Entire Object", &_selectedModelData.wireframeEntireObject);
            }
        }
    }

    bool Inspector::DrawTransformInspector(mat4x4& instanceMatrix, bool& finishedAction, mat4x4& outPreEditMatrix)
    {
        static ImGuizmo::OPERATION operation = ImGuizmo::OPERATION::TRANSLATE;
        static ImGuizmo::MODE mode = ImGuizmo::MODE::WORLD;

        if (ImGui::RadioButton("Translate", operation == ImGuizmo::TRANSLATE))
            operation = ImGuizmo::TRANSLATE;
        ImGui::SameLine();
        if (ImGui::RadioButton("Rotate", operation == ImGuizmo::ROTATE))
            operation = ImGuizmo::ROTATE;
        ImGui::SameLine();
        if (ImGui::RadioButton("Scale", operation == ImGuizmo::SCALE))
            operation = ImGuizmo::SCALE;

        if (operation != ImGuizmo::SCALE)
        {
            if (ImGui::RadioButton("Local", mode == ImGuizmo::LOCAL))
                mode = ImGuizmo::LOCAL;
            ImGui::SameLine();
            if (ImGui::RadioButton("World", mode == ImGuizmo::WORLD))
                mode = ImGuizmo::WORLD;
        }

        float* instanceMatrixPtr = glm::value_ptr(instanceMatrix);

        vec3 pos;
        vec3 rot;
        vec3 scale;

        bool isDirty = false;
        ImGuizmo::DecomposeMatrixToComponents(instanceMatrixPtr, glm::value_ptr(pos), glm::value_ptr(rot), glm::value_ptr(scale));

        f32 offset = ImGui::CalcTextSize("Scale").x + 10.0f;

        static mat4x4 preEditMatrix;

        ImGui::Text("Pos");
        ImGui::SameLine(offset);
        isDirty |= ImGui::DragFloat3("##pos", glm::value_ptr(pos), 0.1f);

        // If it was activated this frame, save the preEditValue
        if (ImGui::IsItemActivated())
        {
            preEditMatrix = instanceMatrix;
        }
        finishedAction |= ImGui::IsItemDeactivatedAfterEdit();

        ImGui::Text("Rot");
        ImGui::SameLine(offset);
        isDirty |= ImGui::DragFloat3("##rot", glm::value_ptr(rot), 0.1f);

        // If it was activated this frame, save the preEditValue
        if (ImGui::IsItemActivated())
        {
            preEditMatrix = instanceMatrix;
        }
        finishedAction |= ImGui::IsItemDeactivatedAfterEdit();

        ImGui::Text("Scale");
        ImGui::SameLine(offset);
        isDirty |= ImGui::DragFloat3("##scale", glm::value_ptr(scale), 0.01f);

        // If it was activated this frame, save the preEditValue
        if (ImGui::IsItemActivated())
        {
            preEditMatrix = instanceMatrix;
        }
        finishedAction |= ImGui::IsItemDeactivatedAfterEdit();

        ImGuizmo::RecomposeMatrixFromComponents(glm::value_ptr(pos), glm::value_ptr(rot), glm::value_ptr(scale), instanceMatrixPtr);

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        entt::registry::context& ctx = registry->ctx();

        ECS::Singletons::ActiveCamera& activeCamera = ctx.at<ECS::Singletons::ActiveCamera>();
        ECS::Singletons::FreeflyingCameraSettings& settings = ctx.at<ECS::Singletons::FreeflyingCameraSettings>();

        ECS::Components::Camera& camera = registry->get<ECS::Components::Camera>(activeCamera.entity);
        if (ImGuizmo::Manipulate(glm::value_ptr(camera.worldToView), glm::value_ptr(camera.viewToClip), operation, operation == ImGuizmo::SCALE ? ImGuizmo::LOCAL : mode, instanceMatrixPtr, nullptr))
        {
            isDirty = true;
        }
        static bool wasUsing = false;
        bool isUsing = ImGuizmo::IsUsing();

        // If we just started using the gizmo
        if (isUsing && !wasUsing)
        {
            preEditMatrix = instanceMatrix;
        }
        // If we just stopped using the gizmo
        if (!isUsing && wasUsing)
        {
            finishedAction = true;
        }
        wasUsing = isUsing;

        if (finishedAction)
        {
            outPreEditMatrix = preEditMatrix;
        }

        return isDirty;
    }

    void Inspector::DrawTextureInspector(u32 textureIndex, Renderer::TextureID textureID)
    {
        /*if (textureIndex >= _textureStatus.size())
        {
            _textureStatus.resize(textureIndex + 1);
        }

        Renderer::Renderer* renderer = ServiceLocator::GetRenderer();

        const std::string& textureName = renderer->GetDebugName(textureID);
        std::filesystem::path texturePath(textureName);

        ImGui::PushID(textureIndex);
        if (ImGui::Button(_textureStatus[textureIndex].isOpen ? "Hide" : "Show"))
        {
            _textureStatus[textureIndex].isOpen = !_textureStatus[textureIndex].isOpen;
        }
        ImGui::SameLine();
        ImGui::Bullet();
        ImGui::SameLine();
        ImGui::TextWrapped("Texture: %s", texturePath.filename().string().c_str());

        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        {
            ImGui::SetTooltip(textureName.c_str());
        }

        if (_textureStatus[textureIndex].isOpen)
        {
            void* imageHandle = ServiceLocator::GetRenderer()->GetImguiImageHandle(textureID);

            ImGui::Checkbox("r", &_textureStatus[textureIndex].r);
            ImGui::SameLine();
            ImGui::Checkbox("g", &_textureStatus[textureIndex].g);
            ImGui::SameLine();
            ImGui::Checkbox("b", &_textureStatus[textureIndex].b);
            ImGui::SameLine();
            ImGui::Checkbox("a", &_textureStatus[textureIndex].a);

            ImGui::Image(imageHandle, ImVec2(256, 256), ImVec2(0, 0), ImVec2(1, 1), ImVec4(_textureStatus[textureIndex].r, _textureStatus[textureIndex].g, _textureStatus[textureIndex].b, _textureStatus[textureIndex].a));

        }
        ImGui::PopID();*/
    }

    bool Inspector::OnMouseClickLeft(i32 key, KeybindAction action, KeybindModifier modifier)
    {
        if (!CVAR_InspectorEnabled.Get())
            return false;

        //Camera* camera = ServiceLocator::GetCamera();
        //if (!camera->IsActive())
        //    return false;

        //if (camera->IsMouseCaptured())
        //    return false;

        //if (ImGui::GetCurrentContext()->HoveredWindow)
        //    return false;

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
            if (_activeToken != 0)
            {
                pixelQuery->FreeToken(_activeToken);
                _activeToken = 0;
            }

            return false;
        }

        vec2 mousePos = inputManager->GetMousePosition();

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
}