#include "NetworkedInfo.h"

#include "Game-Lib/Application/EnttRegistries.h"
#include "Game-Lib/ECS/Components/AABB.h"
#include "Game-Lib/ECS/Components/Camera.h"
#include "Game-Lib/ECS/Components/MovementInfo.h"
#include "Game-Lib/ECS/Components/Unit.h"
#include "Game-Lib/ECS/Components/UnitPowersComponent.h"
#include "Game-Lib/ECS/Components/UnitResistancesComponent.h"
#include "Game-Lib/ECS/Components/UnitStatsComponent.h"
#include "Game-Lib/ECS/Singletons/ActiveCamera.h"
#include "Game-Lib/ECS/Singletons/Database/CameraSaveSingleton.h"
#include "Game-Lib/ECS/Singletons/CharacterSingleton.h"
#include "Game-Lib/ECS/Singletons/FreeflyingCameraSettings.h"
#include "Game-Lib/ECS/Singletons/NetworkState.h"
#include "Game-Lib/ECS/Util/MessageBuilderUtil.h"
#include "Game-Lib/ECS/Util/Transforms.h"
#include "Game-Lib/ECS/Util/Network/NetworkUtil.h"
#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Rendering/Debug/DebugRenderer.h"
#include "Game-Lib/Util/CameraSaveUtil.h"
#include "Game-Lib/Util/MapUtil.h"
#include "Game-Lib/Util/UnitUtil.h"

#include <Base/CVarSystem/CVarSystemPrivate.h>
#include <Base/Math/Math.h>

#include <FileFormat/Shared.h>

#include <Meta/Generated/Shared/NetworkPacket.h>
#include <Meta/Generated/Shared/UnitEnum.h>

#include <Network/Client.h>

#include <entt/entt.hpp>
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include <string>

AutoCVar_String CVAR_NetworkConnectIP(CVarCategory::Network, "connectIP", "Sets the connection IP", "127.0.0.1");
AutoCVar_String CVAR_NetworkAccountName(CVarCategory::Network, "accountName", "Sets the account name", "dev");
AutoCVar_Int CVAR_NetworkDrawTargetABB(CVarCategory::Network, "drawTargetAABB", "Debug Draws the target's AABB", 0, CVarFlags::EditCheckbox);
AutoCVar_Int CVAR_NetworkOfflineMode(CVarCategory::Network, "offlineMode", "Skips login screen", 1, CVarFlags::EditCheckbox);

using namespace ClientDB;
using namespace ECS::Singletons;

namespace Editor
{
    NetworkedInfo::NetworkedInfo()
        : BaseEditor(GetName())
    {

    }

    void NetworkedInfo::DrawImGui()
    {
        if (ImGui::Begin(GetName(), &IsVisible()))
        {
            EnttRegistries* registries = ServiceLocator::GetEnttRegistries();
            entt::registry& registry = *registries->gameRegistry;
            entt::registry::context& ctx = registry.ctx();

            auto& networkState = ctx.get<NetworkState>();
            auto& characterSingleton = ctx.get<CharacterSingleton>();

            bool isConnected = networkState.client->IsConnected();

            ImGui::Text("Connection Status : %s", isConnected ? "Connected" : "Disconnected");

            if (isConnected)
            {
                ImGui::Text("Ping: %dms", networkState.pingInfo.ping);
                ImGui::Text("Server Update Diff: %dms", networkState.pingInfo.serverUpdateDiff);
                ImGui::NewLine();

                ImGui::Separator();

                bool hasMover = characterSingleton.moverEntity != entt::null;
                if (hasMover)
                {
                    auto& characterTransform = registry.get<ECS::Components::Transform>(characterSingleton.moverEntity);
                    auto& movementInfo = registry.get<ECS::Components::MovementInfo>(characterSingleton.moverEntity);

                    glm::vec3 worldPos = characterTransform.GetWorldPosition();
                    const vec3 characterForward = characterTransform.GetLocalForward();
                    const vec3 characterRight = characterTransform.GetLocalRight();
                    const vec3 characterUp = characterTransform.GetLocalUp();

                    f32 yaw = glm::degrees(movementInfo.yaw);
                    ImGui::Text("Pos + O: (%.2f, %.2f, %.2f, %.2f (%.2f))", worldPos.x, worldPos.y, worldPos.z, movementInfo.yaw, yaw);
                    ImGui::Text("Speed: %.2f", movementInfo.speed);

                    ImGui::Separator();

                    ImGui::Text("Pitch, Yaw, Roll: (%.2f, %.2f, %.2f)", glm::degrees(movementInfo.pitch), yaw, 0.0f);
                    ImGui::Text("Forward: (%.2f, %.2f, %.2f)", characterForward.x, characterForward.y, characterForward.z);
                    ImGui::Text("Right: (%.2f, %.2f, %.2f)", characterRight.x, characterRight.y, characterRight.z);
                    ImGui::Text("Up: (%.2f, %.2f, %.2f)", characterUp.x, characterUp.y, characterUp.z);

                    ImGui::NewLine();
                    ImGui::Separator();

                    auto& moverUnit = registry.get<ECS::Components::Unit>(characterSingleton.moverEntity);

                    if (ImGui::CollapsingHeader("Basic Info"))
                    {
                        auto& unitPowersComponent = registry.get<ECS::Components::UnitPowersComponent>(characterSingleton.moverEntity);
                        auto& healthPower = ::Util::Unit::GetPower(unitPowersComponent, Generated::PowerTypeEnum::Health);

                        ImGui::Text("Name : %s", moverUnit.name.c_str());
                        ImGui::Text("Health (Base, Current, Max) : (%.2f, %.2f, %.2f)", healthPower.base, healthPower.current, healthPower.max);

                        ImGui::Separator();
                    }

                    if (moverUnit.targetEntity != entt::null && registry.valid(moverUnit.targetEntity))
                    {
                        if (ImGui::CollapsingHeader("Target Info"))
                        {
                            auto& targetUnit = registry.get<ECS::Components::Unit>(moverUnit.targetEntity);
                            auto& unitPowersComponent = registry.get<ECS::Components::UnitPowersComponent>(moverUnit.targetEntity);
                            auto& healthPower = ::Util::Unit::GetPower(unitPowersComponent, Generated::PowerTypeEnum::Health);

                            ImGui::Text("Name : %s", targetUnit.name.c_str());
                            ImGui::Text("Health (Base, Current, Max) : (%.2f, %.2f, %.2f)", healthPower.base, healthPower.current, healthPower.max);

                            ImGui::Separator();
                        }

                        if (CVAR_NetworkDrawTargetABB.Get())
                        {
                            auto* aabb = registry.try_get<ECS::Components::AABB>(moverUnit.targetEntity);
                            if (aabb)
                            {
                                auto& transform = registry.get<ECS::Components::Transform>(moverUnit.targetEntity);

                                DebugRenderer* debugRenderer = ServiceLocator::GetGameRenderer()->GetDebugRenderer();
                                vec3 worldCenter = transform.GetWorldPosition() + transform.GetWorldRotation() * (transform.GetLocalScale() * aabb->centerPos);
                                vec3 worldExtents = transform.GetLocalScale() * aabb->extents;
                                quat worldRotation = transform.GetWorldRotation();

                                debugRenderer->DrawOBB3D(worldCenter, worldExtents, worldRotation, Color::Cyan);
                            }
                        }
                    }
                }
                else
                {
                    ImGui::Text("No active character");
                }

                if (ImGui::Button("Disconnect"))
                {
                    networkState.client->Stop();
                }
            }
            else
            {
                ImGui::Separator();

                bool hasMover = characterSingleton.moverEntity != entt::null;
                if (hasMover)
                {
                    auto& characterTransform = registry.get<ECS::Components::Transform>(characterSingleton.moverEntity);
                    auto& movementInfo = registry.get<ECS::Components::MovementInfo>(characterSingleton.moverEntity);

                    glm::vec3 worldPos = characterTransform.GetWorldPosition();
                    const vec3 characterForward = characterTransform.GetLocalForward();
                    const vec3 characterRight = characterTransform.GetLocalRight();
                    const vec3 characterUp = characterTransform.GetLocalUp();

                    f32 yaw = glm::degrees(movementInfo.yaw);
                    ImGui::Text("Pos + O: (%.2f, %.2f, %.2f, %.2f (%.2f))", worldPos.x, worldPos.y, worldPos.z, movementInfo.yaw, yaw);
                    ImGui::Text("Speed: %.2f", movementInfo.speed);

                    ImGui::Separator();

                    ImGui::Text("Pitch, Yaw, Roll: (%.2f, %.2f, %.2f)", glm::degrees(movementInfo.pitch), yaw, 0.0f);
                    ImGui::Text("Forward: (%.2f, %.2f, %.2f)", characterForward.x, characterForward.y, characterForward.z);
                    ImGui::Text("Right: (%.2f, %.2f, %.2f)", characterRight.x, characterRight.y, characterRight.z);
                    ImGui::Text("Up: (%.2f, %.2f, %.2f)", characterUp.x, characterUp.y, characterUp.z);

                    ImGui::NewLine();
                    ImGui::Separator();
                }

                const char* accountName = CVAR_NetworkAccountName.Get();
                size_t accountNameLength = strlen(accountName);

                ImGui::Text("Not connected to server");
                ImGui::Text("Account Name: %s", accountName);

                if (ImGui::Button("Connect"))
                {
                    if (networkState.client && accountNameLength > 0)
                    {
                        if (networkState.client->Connect(CVAR_NetworkConnectIP.Get(), 4000))
                        {
                            ECS::Util::Network::SendPacket(networkState, Generated::ConnectPacket{
                                .accountName = accountName
                            });
                        }
                    }
                }
            }
        }
        ImGui::End();
    }
}