#include "CameraInfo.h"

#include "Game/Application/EnttRegistries.h"
#include "Game/ECS/Components/Camera.h"
#include "Game/ECS/Singletons/ActiveCamera.h"
#include "Game/ECS/Singletons/CameraSaveDB.h"
#include "Game/ECS/Singletons/ClientDBCollection.h"
#include "Game/ECS/Singletons/FreeflyingCameraSettings.h"
#include "Game/ECS/Util/Transforms.h"
#include "Game/Util/CameraSaveUtil.h"
#include "Game/Util/CameraSaveUtil.h"
#include "Game/Util/MapUtil.h"

#include <Base/CVarSystem/CVarSystemPrivate.h>
#include <Base/Math/Math.h>

#include <FileFormat/Shared.h>

#include <entt/entt.hpp>
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include <string>

using namespace ClientDB;
using namespace ECS::Singletons;

namespace Editor
{
    CameraInfo::CameraInfo()
        : BaseEditor(GetName(), true)
	{

	}

	void CameraInfo::DrawImGui()
	{
        EnttRegistries* registries = ServiceLocator::GetEnttRegistries();
        entt::registry& registry = *registries->gameRegistry;
        entt::registry::context& ctx = registry.ctx();

        ActiveCamera& activeCamera = ctx.get<ActiveCamera>();
        FreeflyingCameraSettings& settings = ctx.get<FreeflyingCameraSettings>();

        ECS::Components::Transform& cameraTransform = registry.get<ECS::Components::Transform>(activeCamera.entity);
        ECS::Components::Camera& camera = registry.get<ECS::Components::Camera>(activeCamera.entity);

        // Print position
        if (ImGui::Begin(GetName()))
        {
            quat rotQuat = quat(vec3(glm::radians(camera.pitch), glm::radians(camera.yaw), glm::radians(camera.roll)));

            glm::vec3 worldPos = cameraTransform.GetWorldPosition();

            ImGui::Text("Pos: (%.2f, %.2f, %.2f)", worldPos.x, worldPos.y, worldPos.z);
            ImGui::Text("Speed: %.2f", settings.cameraSpeed);

            ImGui::Separator();

            if (ImGui::CollapsingHeader("Basic Info"))
            {
                ImGui::Text("Pitch: %.2f", camera.pitch);
                ImGui::Text("Yaw: %.2f", camera.yaw);
                ImGui::Text("Roll: %.2f", camera.roll);

                vec3 eulerAngles = glm::eulerAngles(rotQuat);
                ImGui::Text("Euler: (%.2f, %.2f, %.2f)", eulerAngles.x, eulerAngles.y, eulerAngles.z);

                ImGui::Separator();

                const vec3 camForward = cameraTransform.GetLocalForward();
                const vec3 camRight = cameraTransform.GetLocalRight();
                const vec3 camUp = cameraTransform.GetLocalUp();
                ImGui::Text("Forward: (%.2f, %.2f, %.2f)", camForward.x, camForward.y, camForward.z);
                ImGui::Text("Right: (%.2f, %.2f, %.2f)", camRight.x, camRight.y, camRight.z);
                ImGui::Text("Up: (%.2f, %.2f, %.2f)", camUp.x, camUp.y, camUp.z);
            }

            ImGui::Separator();

            if (ImGui::CollapsingHeader("Extended Info"))
            {
                vec2 chunkGlobalPos = Util::Map::WorldPositionToChunkGlobalPos(worldPos);

                vec2 chunkPos = Util::Map::GetChunkIndicesFromAdtPosition(chunkGlobalPos);
                vec2 chunkRemainder = chunkPos - glm::floor(chunkPos);

                vec2 cellLocalPos = (chunkRemainder * Terrain::CHUNK_SIZE);
                vec2 cellPos = cellLocalPos / Terrain::CELL_SIZE;
                vec2 cellRemainder = cellPos - glm::floor(cellPos);

                vec2 patchLocalPos = (cellRemainder * Terrain::CELL_SIZE);
                vec2 patchPos = patchLocalPos / Terrain::PATCH_SIZE;
                vec2 patchRemainder = patchPos - glm::floor(patchPos);

                u32 currentChunkID = Util::Map::GetChunkIdFromChunkPos(chunkPos);
                u32 currentCellID = Util::Map::GetCellIdFromCellPos(cellPos);
                u32 currentPatchID = Util::Map::GetCellIdFromCellPos(patchPos);

                ImGui::Text("Chunk : (%u)", currentChunkID);
                ImGui::Text("Chunk : (%f, %f)", chunkPos.x, chunkPos.y);
                ImGui::Text("Chunk Remainder : (%f, %f)", chunkRemainder.x, chunkRemainder.y);

                ImGui::Spacing();
                ImGui::Text("Cell : (%u)", currentCellID);
                ImGui::Text("ID : (%f, %f)", cellPos.x, cellPos.y);
                ImGui::Text("Pos : (%f, %f)", cellLocalPos.x, cellLocalPos.y);
                ImGui::Text("Remainder : (%f, %f)", cellRemainder.x, cellRemainder.y);

                ImGui::Spacing();
                ImGui::Text("Patch : (%u)", currentPatchID);
                ImGui::Text("ID : (%f, %f)", patchPos.x, patchPos.y);
                ImGui::Text("Pos : (%f, %f)", patchLocalPos.x, patchLocalPos.y);
                ImGui::Text("Remainder : (%f, %f)", patchRemainder.x, patchRemainder.y);
            }

            ImGui::Separator();

            if (ImGui::CollapsingHeader("Saves"))
            {
                static u32 currentSelectedSaveNameHash = 0;

                auto& cameraSaveDB = ctx.get<CameraSaveDB>();
                auto& clientDBCollection = ctx.get<ClientDBCollection>();

                auto cameraSaves = clientDBCollection.Get<Definitions::CameraSave>(ClientDBHash::CameraSave);

                ImGui::Text("Camera Save List (Names)");

                if (ImGui::ListBoxHeader("##camerasavelistbox"))
                {
                    u32 numCameraSaves = cameraSaves.Count();

                    for (const Definitions::CameraSave& cameraSave : cameraSaves)
                    {
                        if (!cameraSaves.IsValid(cameraSave))
                            continue;

                        if (!cameraSaves.HasString(cameraSave.name))
                            continue;

                        const std::string& cameraSaveName = cameraSaves.GetString(cameraSave.name);
                        u32 cameraSaveNameHash = StringUtils::fnv1a_32(cameraSaveName.c_str(), cameraSaveName.length());

                        const bool isSelected = cameraSaveNameHash == currentSelectedSaveNameHash;
                        if (ImGui::Selectable(cameraSaveName.c_str(), isSelected))
                        {
                            currentSelectedSaveNameHash = cameraSaveNameHash;
                        }

                        if (isSelected)
                            ImGui::SetItemDefaultFocus();
                    }
                
                    ImGui::ListBoxFooter();
                }
                
                bool hasSelectedCamera = cameraSaveDB.cameraSaveNameHashToID.contains(currentSelectedSaveNameHash);
                if (hasSelectedCamera)
                {
                    u32 id = cameraSaveDB.cameraSaveNameHashToID[currentSelectedSaveNameHash];
                    const auto& cameraSave = cameraSaves.GetByID(id);
                    const std::string& cameraSaveName = cameraSaves.GetString(cameraSave.name);
                
                    ImGui::Text("Selected Save : %s", cameraSaveName.c_str());
                }
                else
                {
                    ImGui::Text("No Selected Save");
                }
                
                if (ImGui::Button("Goto Location"))
                {
                    if (hasSelectedCamera)
                    {
                        u32 id = cameraSaveDB.cameraSaveNameHashToID[currentSelectedSaveNameHash];
                
                        const auto& cameraSave = cameraSaves.GetByID(id);
                
                        const std::string& cameraSaveName = cameraSaves.GetString(cameraSave.name);
                        const std::string& cameraSaveCode = cameraSaves.GetString(cameraSave.code);
                
                        if (Util::CameraSave::LoadSaveLocationFromBase64(cameraSaveCode))
                        {
                            DebugHandler::Print("[CameraInfo] Loaded Camera Save {0}", cameraSaveName);
                        }
                        else
                        {
                            DebugHandler::PrintError("[CameraInfo] Failed to load Camera Save {0}, the code is likely outdated or corrupt", cameraSaveName);
                        }
                    }
                }
                
                ImGui::SameLine();
                
                if (ImGui::Button("Delete Location"))
                {
                    if (hasSelectedCamera)
                    {
                        u32 id = cameraSaveDB.cameraSaveNameHashToID[currentSelectedSaveNameHash];
                        cameraSaveDB.cameraSaveNameHashToID.erase(currentSelectedSaveNameHash);
                
                        cameraSaves.Remove(id);
                    }
                }
                
                ImGui::Separator();
                static std::string currentSaveName = "";
                
                ImGui::Text("New Save Name");
                ImGui::InputText("##savename", &currentSaveName);
                
                if (ImGui::Button("Add Location"))
                {
                    if (currentSaveName.length() > 0)
                    {
                        u32 saveNameHash = StringUtils::fnv1a_32(currentSaveName.c_str(), currentSaveName.length());
                        if (!cameraSaveDB.cameraSaveNameHashToID.contains(saveNameHash))
                        {
                            std::string saveCode;
                            if (Util::CameraSave::GenerateSaveLocation(currentSaveName, saveCode))
                            {
                                Definitions::CameraSave cameraSave;
                                cameraSave.name = cameraSaves.AddString(currentSaveName);
                                cameraSave.code = cameraSaves.AddString(saveCode);
                                cameraSaves.Add(cameraSave);
                
                                cameraSaveDB.cameraSaveNameHashToID[saveNameHash] = cameraSave.GetID();
                
                                currentSaveName.clear();
                            }
                        }
                    }
                }
            }
        }
        ImGui::End();
	}
}