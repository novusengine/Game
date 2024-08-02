#include "NetworkedInfo.h"

#include "Game/Application/EnttRegistries.h"
#include "Game/ECS/Components/Camera.h"
#include "Game/ECS/Components/MovementInfo.h"
#include "Game/ECS/Components/NetworkedEntity.h"
#include "Game/ECS/Components/UnitStatsComponent.h"
#include "Game/ECS/Singletons/ActiveCamera.h"
#include "Game/ECS/Singletons/CameraSaveDB.h"
#include "Game/ECS/Singletons/CharacterSingleton.h"
#include "Game/ECS/Singletons/ClientDBCollection.h"
#include "Game/ECS/Singletons/FreeflyingCameraSettings.h"
#include "Game/ECS/Singletons/NetworkState.h"
#include "Game/ECS/Util/MessageBuilderUtil.h"
#include "Game/ECS/Util/Transforms.h"
#include "Game/Util/CameraSaveUtil.h"
#include "Game/Util/CameraSaveUtil.h"
#include "Game/Util/MapUtil.h"

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
            ImGui::NewLine();
            ImGui::Separator();

            if (characterSingleton.moverEntity != entt::null)
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

                if (networkState.client->IsConnected())
                {
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

                    if (ImGui::Button("Disconnect"))
                    {
                        networkState.client->Close();
                    }
                }
                else
                {
                    const char* characterName = CVAR_NetworkCharacterName.Get();
                    size_t characterNameLength = strlen(characterName);

                    ImGui::Text("Not connected to server");
                    ImGui::Text("Character Name: %s", characterName);

                    if (ImGui::Button("Connect"))
                    {
                        if (networkState.client && characterNameLength > 0)
                        {
                            Network::Socket::Result initResult = networkState.client->Init(Network::Socket::Mode::TCP);
                            if (initResult == Network::Socket::Result::SUCCESS)
                            {
                                // Connect to IP/Port
                                const char* ipAddress = CVAR_NetworkConnectIP.Get();
                                u16 port = 4000;

                                Network::Socket::Result connectResult = networkState.client->Connect(ipAddress, port);

                                if (connectResult != Network::Socket::Result::SUCCESS &&
                                    connectResult != Network::Socket::Result::ERROR_WOULD_BLOCK)
                                {
                                    NC_LOG_ERROR("Network : Failed to connect to ({0}, {1})", ipAddress, port);
                                }
                                else
                                {
                                    networkState.client->GetSocket()->SetBlockingState(false);

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
            }
            else
            {
                ImGui::Text("No active character");

                if (ImGui::Button("Disconnect"))
                {
                    networkState.client->Close();
                }
            }
        }
        ImGui::End();
    }
}