#include "NetworkedInfo.h"

#include "Game-Lib/Application/EnttRegistries.h"
#include "Game-Lib/ECS/Components/Camera.h"
#include "Game-Lib/ECS/Components/MovementInfo.h"
#include "Game-Lib/ECS/Components/NetworkedEntity.h"
#include "Game-Lib/ECS/Components/UnitStatsComponent.h"
#include "Game-Lib/ECS/Singletons/ActiveCamera.h"
#include "Game-Lib/ECS/Singletons/CameraSaveDB.h"
#include "Game-Lib/ECS/Singletons/CharacterSingleton.h"
#include "Game-Lib/ECS/Singletons/ClientDBCollection.h"
#include "Game-Lib/ECS/Singletons/FreeflyingCameraSettings.h"
#include "Game-Lib/ECS/Singletons/NetworkState.h"
#include "Game-Lib/ECS/Util/MessageBuilderUtil.h"
#include "Game-Lib/ECS/Util/Transforms.h"
#include "Game-Lib/Util/CameraSaveUtil.h"
#include "Game-Lib/Util/CameraSaveUtil.h"
#include "Game-Lib/Util/MapUtil.h"

#include <Base/CVarSystem/CVarSystemPrivate.h>
#include <Base/Math/Math.h>

#include <FileFormat/Shared.h>

#include <Gameplay/Network/Opcode.h>

#include <Network/Client.h>

#include <entt/entt.hpp>
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include <string>

AutoCVar_String CVAR_NetworkConnectIP(CVarCategory::Network, "connectIP", "Sets the connection IP", "127.0.0.1");
AutoCVar_String CVAR_NetworkCharacterName(CVarCategory::Network, "characterName", "Sets the character name", "dev");

using namespace ClientDB;
using namespace ECS::Singletons;

namespace Editor
{
    NetworkedInfo::NetworkedInfo()
        : BaseEditor(GetName(), true)
    {

    }

    void NetworkedInfo::DrawImGui()
    {
        if (ImGui::Begin(GetName()))
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
                ImGui::Text("Ping: %dms", networkState.ping);
                ImGui::Text("Server Update Diff: %dms", networkState.serverUpdateDiff);
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

                    f32 yaw = glm::degrees(movementInfo.yaw - glm::pi<f32>());
                    ImGui::Text("Pos + O: (%.2f, %.2f, %.2f, %.2f)", worldPos.x, worldPos.y, worldPos.z, yaw);
                    ImGui::Text("Speed: %.2f", movementInfo.speed);

                    ImGui::Separator();

                    ImGui::Text("Pitch, Yaw, Roll: (%.2f, %.2f, %.2f)", glm::degrees(movementInfo.pitch), yaw, 0.0f);
                    ImGui::Text("Forward: (%.2f, %.2f, %.2f)", characterForward.x, characterForward.y, characterForward.z);
                    ImGui::Text("Right: (%.2f, %.2f, %.2f)", characterRight.x, characterRight.y, characterRight.z);
                    ImGui::Text("Up: (%.2f, %.2f, %.2f)", characterUp.x, characterUp.y, characterUp.z);

                    ImGui::NewLine();
                    ImGui::Separator();

                    auto& networkedEntity = registry.get<ECS::Components::NetworkedEntity>(characterSingleton.moverEntity);

                    if (ImGui::CollapsingHeader("Basic Info"))
                    {
                        auto& unitStatsComponent = registry.get<ECS::Components::UnitStatsComponent>(characterSingleton.moverEntity);
                        ImGui::Text("Health (Base, Current, Max) : (%.2f, %.2f, %.2f)", unitStatsComponent.baseHealth, unitStatsComponent.currentHealth, unitStatsComponent.maxHealth);

                        for (u32 i = 0; i < (u32)ECS::Components::PowerType::Count; i++)
                        {
                            ECS::Components::PowerType type = (ECS::Components::PowerType)i;

                            switch (type)
                            {
                                case ECS::Components::PowerType::Mana:
                                {
                                    ImGui::Text("Mana (Base, Current, Max) : (%.2f, %.2f, %.2f)", unitStatsComponent.basePower[i], unitStatsComponent.currentPower[i], unitStatsComponent.maxPower[i]);
                                    break;
                                }
                                case ECS::Components::PowerType::Rage:
                                {
                                    ImGui::Text("Rage (Base, Current, Max) : (%.2f, %.2f, %.2f)", unitStatsComponent.basePower[i], unitStatsComponent.currentPower[i], unitStatsComponent.maxPower[i]);
                                    break;
                                }
                                case ECS::Components::PowerType::Focus:
                                {
                                    ImGui::Text("Focus (Base, Current, Max) : (%.2f, %.2f, %.2f)", unitStatsComponent.basePower[i], unitStatsComponent.currentPower[i], unitStatsComponent.maxPower[i]);
                                    break;
                                }
                                case ECS::Components::PowerType::Energy:
                                {
                                    ImGui::Text("Energy (Base, Current, Max) : (%.2f, %.2f, %.2f)", unitStatsComponent.basePower[i], unitStatsComponent.currentPower[i], unitStatsComponent.maxPower[i]);
                                    break;
                                }
                                case ECS::Components::PowerType::Happiness:
                                {
                                    ImGui::Text("Happiness (Base, Current, Max) : (%.2f, %.2f, %.2f)", unitStatsComponent.basePower[i], unitStatsComponent.currentPower[i], unitStatsComponent.maxPower[i]);
                                    break;
                                }
                            }
                        }

                        ImGui::Separator();
                    }

                    if (networkedEntity.targetEntity != entt::null && registry.valid(networkedEntity.targetEntity))
                    {
                        if (ImGui::CollapsingHeader("Target Info"))
                        {
                            auto& unitStatsComponent = registry.get<ECS::Components::UnitStatsComponent>(networkedEntity.targetEntity);
                            ImGui::Text("Health (Base, Current, Max) : (%.2f, %.2f, %.2f)", unitStatsComponent.baseHealth, unitStatsComponent.currentHealth, unitStatsComponent.maxHealth);

                            for (u32 i = 0; i < (u32)ECS::Components::PowerType::Count; i++)
                            {
                                ECS::Components::PowerType type = (ECS::Components::PowerType)i;

                                switch (type)
                                {
                                    case ECS::Components::PowerType::Mana:
                                    {
                                        ImGui::Text("Mana (Base, Current, Max) : (%.2f, %.2f, %.2f)", unitStatsComponent.basePower[i], unitStatsComponent.currentPower[i], unitStatsComponent.maxPower[i]);
                                        break;
                                    }
                                    case ECS::Components::PowerType::Rage:
                                    {
                                        ImGui::Text("Rage (Base, Current, Max) : (%.2f, %.2f, %.2f)", unitStatsComponent.basePower[i], unitStatsComponent.currentPower[i], unitStatsComponent.maxPower[i]);
                                        break;
                                    }
                                    case ECS::Components::PowerType::Focus:
                                    {
                                        ImGui::Text("Focus (Base, Current, Max) : (%.2f, %.2f, %.2f)", unitStatsComponent.basePower[i], unitStatsComponent.currentPower[i], unitStatsComponent.maxPower[i]);
                                        break;
                                    }
                                    case ECS::Components::PowerType::Energy:
                                    {
                                        ImGui::Text("Energy (Base, Current, Max) : (%.2f, %.2f, %.2f)", unitStatsComponent.basePower[i], unitStatsComponent.currentPower[i], unitStatsComponent.maxPower[i]);
                                        break;
                                    }
                                    case ECS::Components::PowerType::Happiness:
                                    {
                                        ImGui::Text("Happiness (Base, Current, Max) : (%.2f, %.2f, %.2f)", unitStatsComponent.basePower[i], unitStatsComponent.currentPower[i], unitStatsComponent.maxPower[i]);
                                        break;
                                    }
                                }
                            }

                            ImGui::Separator();
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

                    f32 yaw = glm::degrees(movementInfo.yaw - glm::pi<f32>());
                    ImGui::Text("Pos + O: (%.2f, %.2f, %.2f, %.2f)", worldPos.x, worldPos.y, worldPos.z, yaw);
                    ImGui::Text("Speed: %.2f", movementInfo.speed);

                    ImGui::Separator();

                    ImGui::Text("Pitch, Yaw, Roll: (%.2f, %.2f, %.2f)", glm::degrees(movementInfo.pitch), yaw, 0.0f);
                    ImGui::Text("Forward: (%.2f, %.2f, %.2f)", characterForward.x, characterForward.y, characterForward.z);
                    ImGui::Text("Right: (%.2f, %.2f, %.2f)", characterRight.x, characterRight.y, characterRight.z);
                    ImGui::Text("Up: (%.2f, %.2f, %.2f)", characterUp.x, characterUp.y, characterUp.z);

                    ImGui::NewLine();
                    ImGui::Separator();
                }

                const char* characterName = CVAR_NetworkCharacterName.Get();
                size_t characterNameLength = strlen(characterName);

                ImGui::Text("Not connected to server");
                ImGui::Text("Character Name: %s", characterName);

                if (ImGui::Button("Connect"))
                {
                    if (networkState.client && characterNameLength > 0)
                    {
                        if (networkState.client->Connect(CVAR_NetworkConnectIP.Get(), 4000))
                        {
                            std::shared_ptr<Bytebuffer> buffer = Bytebuffer::Borrow<128>();
                            if (ECS::Util::MessageBuilder::Authentication::BuildConnectMessage(buffer, characterName))
                            {
                                networkState.client->Send(buffer);
                            }
                        }
                    }
                }
            }
        }
        ImGui::End();
    }
}