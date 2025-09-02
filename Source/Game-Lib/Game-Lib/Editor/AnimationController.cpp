#include "AnimationController.h"

#include "Game-Lib/Application/EnttRegistries.h"
#include "Game-Lib/ECS/Components/AnimationData.h"
#include "Game-Lib/ECS/Components/Model.h"
#include "Game-Lib/ECS/Components/Unit.h"
#include "Game-Lib/ECS/Singletons/CharacterSingleton.h"
#include "Game-Lib/ECS/Singletons/Database/ClientDBSingleton.h"
#include "Game-Lib/Gameplay/Animation/Defines.h"
#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Rendering/Model/ModelLoader.h"
#include "Game-Lib/Util/AnimationUtil.h"
#include "Game-Lib/Util/UnitUtil.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Meta/Generated/Shared/ClientDB.h>

#include <entt/entt.hpp>
#include <imgui/imgui.h>

#include <string>

using namespace ECS;

namespace Editor
{
    AnimationController::AnimationController()
        : BaseEditor(GetName())
    {

    }

    void AnimationController::DrawImGui()
    {
        if (ImGui::Begin(GetName(), &IsVisible()))
        {
            EnttRegistries* registries = ServiceLocator::GetEnttRegistries();
            entt::registry& gameRegistry = *registries->gameRegistry;
            entt::registry& dbRegistry = *registries->dbRegistry;
            entt::registry::context& gameCtx = gameRegistry.ctx();
            entt::registry::context& dbCtx = dbRegistry.ctx();

            auto& characterSingleton = gameCtx.get<Singletons::CharacterSingleton>();
            if (characterSingleton.moverEntity == entt::null)
            {
                ImGui::Text("No Active Mover to control animations on");
            }
            else
            {
                Singletons::ClientDBSingleton& clientDBSingleton = dbCtx.get<Singletons::ClientDBSingleton>();
                auto* animationStorage = clientDBSingleton.Get(ClientDBHash::AnimationData);

                auto* animationData = gameRegistry.try_get<Components::AnimationData>(characterSingleton.moverEntity);
                auto* model = gameRegistry.try_get<Components::Model>(characterSingleton.moverEntity);
                auto* unit = gameRegistry.try_get<Components::Unit>(characterSingleton.moverEntity);
                ModelLoader* modelLoader = ServiceLocator::GetGameRenderer()->GetModelLoader();
                const auto* modelInfo = modelLoader->GetModelInfo(model->modelHash);

                if (animationData && model && unit && modelInfo)
                {
                    static i32 selectedAnimation = static_cast<i32>(::Animation::Defines::Type::Stand);
                    static i32 selectedKeyBone = static_cast<i32>(::Animation::Defines::Bone::Main);
                    static ::Animation::Defines::SequenceID selectedSequenceID = ::Animation::Defines::InvalidSequenceID;
                    static Database::Unit::AnimationDataFlags originalAnimFlags = { 0 };
                    static bool sequenceIsFallback = false;
                    static bool skeletonHasBone = true;

                    ImGui::Text("Animations");
                    if (ImGui::ListBox("##AnimationsListBox", &selectedAnimation, ::Animation::Defines::TypeNames, static_cast<i32>(::Animation::Defines::Type::Count)))
                    {
                        ::Animation::Defines::Type Type = static_cast<::Animation::Defines::Type>(selectedAnimation);
                        ::Animation::Defines::SequenceID sequenceID = ::Animation::Defines::InvalidSequenceID;
                        originalAnimFlags = { 0 };
                        sequenceIsFallback = false;

                        if (animationStorage->Has(selectedAnimation))
                        {
                            auto& animationDataRec = animationStorage->Get<Generated::AnimationDataRecord>(selectedAnimation);

                            sequenceID = Util::Animation::GetFirstSequenceForAnimation(modelInfo, Type);
                            originalAnimFlags = *reinterpret_cast<Database::Unit::AnimationDataFlags*>(&animationDataRec.flags);

                            if (sequenceID == ::Animation::Defines::InvalidSequenceID && animationDataRec.fallback != 0)
                            {
                                ::Animation::Defines::Type fallbackType = static_cast<::Animation::Defines::Type>(animationDataRec.fallback);
                                sequenceID = Util::Animation::GetFirstSequenceForAnimation(modelInfo, fallbackType);
                                sequenceIsFallback = true;
                            }
                        }

                        selectedSequenceID = sequenceID;
                    }

                    if (ImGui::Combo("##AnimationComboKeyBones", &selectedKeyBone, ::Animation::Defines::BoneNames, static_cast<i32>(::Animation::Defines::Bone::Count), -1))
                    {
                        i16 boneIndex = Util::Animation::GetBoneIndexFromKeyBoneID(modelInfo, static_cast<::Animation::Defines::Bone>(selectedKeyBone));

                        if (boneIndex == ::Animation::Defines::InvalidBoneID)
                        {
                            skeletonHasBone = false;
                        }
                        else
                        {
                            const Model::ComplexModel::Bone& skeletonBone = modelInfo->bones[boneIndex];

                            ::Animation::Defines::Type Type = static_cast<::Animation::Defines::Type>(selectedAnimation);
                            ::Animation::Defines::SequenceID sequenceID = Util::Animation::GetFirstSequenceForAnimation(modelInfo, Type);

                            if (sequenceID == ::Animation::Defines::InvalidSequenceID)
                            {
                                skeletonHasBone = false;
                            }
                            else
                            {
                                if (boneIndex == Util::Animation::GetBoneIndexFromKeyBoneID(modelInfo, ::Animation::Defines::Bone::Default))
                                {
                                    skeletonHasBone = true;
                                }
                                else
                                {
                                    const Model::ComplexModel::AnimationData<vec3>& translationAnimData = skeletonBone.translation;
                                    const Model::ComplexModel::AnimationData<quat>& rotationAnimData = skeletonBone.rotation;
                                    const Model::ComplexModel::AnimationData<vec3>& scaleAnimData = skeletonBone.scale;

                                    bool hasTranslation = translationAnimData.globalLoopIndex == -1 && translationAnimData.tracks.size() > sequenceID;
                                    bool hasRotation = rotationAnimData.globalLoopIndex == -1 && rotationAnimData.tracks.size() > sequenceID;
                                    bool hasScale = scaleAnimData.globalLoopIndex == -1 && scaleAnimData.tracks.size() > sequenceID;

                                    if (!hasTranslation && !hasRotation && !hasScale)
                                    {
                                        skeletonHasBone = false;
                                    }
                                    else
                                    {
                                        skeletonHasBone = true;
                                    }
                                }
                            }
                        }
                    }

                    bool hasAnimation = selectedSequenceID != ::Animation::Defines::InvalidSequenceID;
                    if (hasAnimation && skeletonHasBone)
                    {
                        if (ImGui::Button("Play"))
                        {
                            ::Animation::Defines::Type Type = static_cast<::Animation::Defines::Type>(selectedAnimation);
                            ::Animation::Defines::Bone keyBone = static_cast<::Animation::Defines::Bone>(selectedKeyBone);

                            bool popagateToChildren = false;
                            i16 boneIndex = ::Util::Animation::GetBoneIndexFromKeyBoneID(modelInfo, keyBone);
                            i16 defaultBoneIndex = ::Util::Animation::GetBoneIndexFromKeyBoneID(modelInfo, ::Animation::Defines::Bone::Default);

                            if (boneIndex == defaultBoneIndex)
                            {
                                unit->overrideAnimation = Type;
                            }
                            else
                            {
                                popagateToChildren = true;
                            }

                            Util::Animation::SetBoneSequence(modelInfo, *animationData, keyBone, Type, popagateToChildren);
                        }
                    }
                    else
                    {
                        ImGui::Text("This animation is not supported by the model");
                    }

                    if (unit->overrideAnimation != ::Animation::Defines::Type::Invalid)
                    {
                        if (ImGui::Button("Clear Override"))
                        {
                            unit->overrideAnimation = ::Animation::Defines::Type::Invalid;
                        }
                    }

                    if (ImGui::Button("Toggle Left Hand"))
                    {
                        bool isLeftHandClosed = Util::Unit::IsHandClosed(gameRegistry, characterSingleton.moverEntity, true);
                        if (isLeftHandClosed)
                        {
                            Util::Unit::OpenHand(gameRegistry, characterSingleton.moverEntity, true);
                        }
                        else
                        {
                            Util::Unit::CloseHand(gameRegistry, characterSingleton.moverEntity, true);
                        }
                    }

                    if (ImGui::Button("Toggle Right Hand"))
                    {
                        bool isRightHandClosed = Util::Unit::IsHandClosed(gameRegistry, characterSingleton.moverEntity, false);
                        if (isRightHandClosed)
                        {
                            Util::Unit::OpenHand(gameRegistry, characterSingleton.moverEntity, false);
                        }
                        else
                        {
                            Util::Unit::CloseHand(gameRegistry, characterSingleton.moverEntity, false);
                        }
                    }

                    if (ImGui::Button("Dump HandClosed Bones"))
                    {
                        NC_LOG_INFO("-- Dumping HandsClosed Bones for Model --");
                        u32 handClosedSequenceIndex = Util::Animation::GetFirstSequenceForAnimation(modelInfo, ::Animation::Defines::Type::HandsClosed);

                        if (handClosedSequenceIndex != ::Animation::Defines::InvalidSequenceID)
                        {
                            u32 numBones = static_cast<u32>(modelInfo->bones.size());
                            for (u32 i = 0; i < numBones; i++)
                            {
                                const Model::ComplexModel::Bone& bone = modelInfo->bones[i];

                                // Translation
                                {
                                    u32 numTranslationTracks = static_cast<u32>(bone.translation.tracks.size());
                                    if (numTranslationTracks > handClosedSequenceIndex)
                                    {
                                        u32 numValues = static_cast<u32>(bone.translation.tracks[handClosedSequenceIndex].values.size());
                                        if (numValues > 0)
                                        {
                                            if (bone.parentBoneID == -1)
                                            {
                                                NC_LOG_INFO("Model Bone {0}({1}) contains translation values for HandsClosed", i, bone.keyBoneID)
                                            }
                                            else
                                            {
                                                NC_LOG_INFO("Model Bone {0}({1}) -> {2}({3}) contains translation values for HandsClosed", i, bone.keyBoneID, bone.parentBoneID, modelInfo->bones[bone.parentBoneID].keyBoneID)
                                            }
                                        }
                                    }
                                }

                                // Rotation
                                {
                                    u32 numRotationTracks = static_cast<u32>(bone.rotation.tracks.size());
                                    if (numRotationTracks > handClosedSequenceIndex)
                                    {
                                        u32 numValues = static_cast<u32>(bone.rotation.tracks[handClosedSequenceIndex].values.size());
                                        if (numValues > 0)
                                        {
                                            if (bone.parentBoneID == -1)
                                            {
                                                NC_LOG_INFO("Model Bone {0}({1}) contains rotation values for HandsClosed", i, bone.keyBoneID)
                                            }
                                            else
                                            {
                                                NC_LOG_INFO("Model Bone {0}({1}) -> {2}({3}) contains rotation values for HandsClosed", i, bone.keyBoneID, bone.parentBoneID, modelInfo->bones[bone.parentBoneID].keyBoneID)
                                            }
                                        }
                                    }
                                }

                                // Scale
                                {
                                    u32 numScaleTracks = static_cast<u32>(bone.scale.tracks.size());
                                    if (numScaleTracks > handClosedSequenceIndex)
                                    {
                                        u32 numValues = static_cast<u32>(bone.scale.tracks[handClosedSequenceIndex].values.size());
                                        if (numValues > 0)
                                        {
                                            if (bone.parentBoneID == -1)
                                            {
                                                NC_LOG_INFO("Model Bone {0}({1}) contains scale values for HandsClosed", i, bone.keyBoneID)
                                            }
                                            else
                                            {
                                                NC_LOG_INFO("Model Bone {0}({1}) -> {2}({3}) scale translation values for HandsClosed", i, bone.keyBoneID, bone.parentBoneID, modelInfo->bones[bone.parentBoneID].keyBoneID)
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        NC_LOG_INFO("\n")
                    }
                }
                else
                {
                    ImGui::Text("No Animation Data on Active Mover");
                }
            }
        }
        ImGui::End();
    }
}