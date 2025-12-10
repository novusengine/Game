#include "CameraInfo.h"

#include "Game-Lib/Application/EnttRegistries.h"
#include "Game-Lib/ECS/Components/Camera.h"
#include "Game-Lib/ECS/Singletons/ActiveCamera.h"
#include "Game-Lib/ECS/Singletons/Database/ClientDBSingleton.h"
#include "Game-Lib/ECS/Singletons/FreeflyingCameraSettings.h"
#include "Game-Lib/ECS/Util/Transforms.h"
#include "Game-Lib/ECS/Util/Database/CameraUtil.h"
#include "Game-Lib/Util/CameraSaveUtil.h"
#include "Game-Lib/Util/CameraSaveUtil.h"
#include "Game-Lib/Util/MapUtil.h"

#include <Base/CVarSystem/CVarSystemPrivate.h>
#include <Base/Math/Math.h>

#include <FileFormat/Shared.h>

#include <MetaGen/Shared/ClientDB/ClientDB.h>

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
        : BaseEditor(GetName())
    {

    }

    void CameraInfo::DrawImGui()
    {
        if (ImGui::Begin(GetName(), &IsVisible()))
        {
            EnttRegistries* registries = ServiceLocator::GetEnttRegistries();
            entt::registry& gameRegistry = *registries->gameRegistry;
            entt::registry& dbRegistry = *registries->dbRegistry;
            entt::registry::context& gameCtx = gameRegistry.ctx();
            entt::registry::context& dbCtx = dbRegistry.ctx();

            auto& activeCamera = gameCtx.get<ActiveCamera>();
            auto& settings = gameCtx.get<FreeflyingCameraSettings>();

            if (activeCamera.entity != entt::null)
            {
                auto& cameraTransform = gameRegistry.get<ECS::Components::Transform>(activeCamera.entity);
                auto& camera = gameRegistry.get<ECS::Components::Camera>(activeCamera.entity);

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

                    auto& clientDBSingleton = dbCtx.get<ClientDBSingleton>();

                    auto* cameraSaveStorage = clientDBSingleton.Get(ClientDBHash::CameraSave);

                    ImGui::Text("Camera Save List (Names)");

                    if (ImGui::BeginListBox("##camerasavelistbox"))
                    {
                        u32 numCameraSaves = cameraSaveStorage->GetNumRows();

                        cameraSaveStorage->Each([&cameraSaveStorage](u32 id, const MetaGen::Shared::ClientDB::CameraSaveRecord& cameraSave) -> bool
                        {
                            const std::string& cameraSaveName = cameraSaveStorage->GetString(cameraSave.name);
                            u32 cameraSaveNameHash = StringUtils::fnv1a_32(cameraSaveName.c_str(), cameraSaveName.length());

                            const bool isSelected = cameraSaveNameHash == currentSelectedSaveNameHash;
                            if (ImGui::Selectable(cameraSaveName.c_str(), isSelected))
                            {
                                currentSelectedSaveNameHash = cameraSaveNameHash;
                            }

                            if (isSelected)
                                ImGui::SetItemDefaultFocus();

                            return true;
                        });

                        ImGui::EndListBox();
                    }


                    bool hasSelectedCamera = ECSUtil::Camera::HasCameraSave(currentSelectedSaveNameHash);
                    if (hasSelectedCamera)
                    {
                        u32 id = ECSUtil::Camera::GetCameraSaveID(currentSelectedSaveNameHash);
                        const auto& cameraSave = cameraSaveStorage->Get<MetaGen::Shared::ClientDB::CameraSaveRecord>(id);

                        const std::string& cameraSaveName = cameraSaveStorage->GetString(cameraSave.name);
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
                            u32 id = ECSUtil::Camera::GetCameraSaveID(currentSelectedSaveNameHash);

                            const auto& cameraSave = cameraSaveStorage->Get<MetaGen::Shared::ClientDB::CameraSaveRecord>(id);
                            const std::string& cameraSaveName = cameraSaveStorage->GetString(cameraSave.name);
                            const std::string& cameraSaveCode = cameraSaveStorage->GetString(cameraSave.code);

                            if (ECSUtil::Camera::LoadSaveLocationFromBase64(cameraSaveCode))
                            {
                                NC_LOG_INFO("[CameraInfo] Loaded Camera Save {0}", cameraSaveName);
                            }
                            else
                            {
                                NC_LOG_ERROR("[CameraInfo] Failed to load Camera Save {0}, the code is likely outdated or corrupt", cameraSaveName);
                            }
                        }
                    }

                    ImGui::SameLine();

                    if (ImGui::Button("Delete Location"))
                    {
                        if (hasSelectedCamera)
                        {
                            ECSUtil::Camera::RemoveCameraSave(currentSelectedSaveNameHash);
                        }
                    }

                    ImGui::Separator();
                    static std::string currentSaveName = "";

                    ImGui::Text("New Save Name");
                    ImGui::InputText("##savename", &currentSaveName);

                    if (ImGui::Button("Add Location"))
                    {
                        if (ECSUtil::Camera::AddCameraSave(currentSaveName))
                        {
                            currentSaveName.clear();
                        }
                    }
                }
            }
            else
            {
                ImGui::Text("No active camera");
            }
        }
        ImGui::End();
    }
}