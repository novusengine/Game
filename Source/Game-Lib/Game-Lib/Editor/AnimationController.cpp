#include "AnimationController.h"

#include "Game-Lib/Animation/AnimationDefines.h"
#include "Game-Lib/Animation/AnimationSystem.h"
#include "Game-Lib/Application/EnttRegistries.h"
#include "Game-Lib/ECS/Components/AnimationData.h"
#include "Game-Lib/ECS/Components/Model.h"
#include "Game-Lib/ECS/Singletons/CharacterSingleton.h"
#include "Game-Lib/ECS/Singletons/ClientDBCollection.h"
#include "Game-Lib/Util/AnimationUtil.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <FileFormat/Novus/ClientDB/Definitions.h>

#include <entt/entt.hpp>
#include <imgui/imgui.h>

#include <string>

using namespace ECS;

namespace Editor
{
    AnimationController::AnimationController()
        : BaseEditor(GetName(), true)
    {

    }

    void AnimationController::DrawImGui()
    {
        if (ImGui::Begin(GetName()))
        {
            EnttRegistries* registries = ServiceLocator::GetEnttRegistries();
            entt::registry& registry = *registries->gameRegistry;
            entt::registry::context& ctx = registry.ctx();

            auto& characterSingleton = ctx.get<Singletons::CharacterSingleton>();
            if (characterSingleton.moverEntity == entt::null)
            {
                ImGui::Text("No Active Mover to control animations on");
            }
            else
            {
                Animation::AnimationSystem* animationSystem = ServiceLocator::GetAnimationSystem();
                Singletons::ClientDBCollection& clientDBCollection = ctx.get<Singletons::ClientDBCollection>();
                auto* animationStorage = clientDBCollection.Get<ClientDB::Definitions::AnimationData>(Singletons::ClientDBHash::AnimationData);

                auto* animationData = registry.try_get<Components::AnimationData>(characterSingleton.moverEntity);
                auto* model = registry.try_get<Components::Model>(characterSingleton.moverEntity);
                if (animationData && model)
                {
                    static i32 selectedAnimation = static_cast<i32>(Animation::AnimationType::Stand);
                    static i32 selectedKeyBone = static_cast<i32>(Animation::AnimationBone::Main);
                    static Animation::AnimationSequenceID selectedSequenceID = Animation::InvalidSequenceID;
                    static ClientDB::Definitions::AnimationData::Flags originalAnimFlags = { 0 };
                    static bool sequenceIsFallback = false;
                    static bool skeletonHasBone = true;

                    ImGui::Text("Animations");
                    if (ImGui::ListBox("##AnimationsListBox", &selectedAnimation, Animation::AnimationNames, static_cast<i32>(Animation::AnimationType::Count)))
                    {
                        Animation::AnimationType animationType = static_cast<Animation::AnimationType>(selectedAnimation);
                        ClientDB::Definitions::AnimationData* animationDataRec = animationStorage->GetRow(selectedAnimation);
                        Animation::AnimationSequenceID sequenceID = Animation::InvalidSequenceID;
                        originalAnimFlags = { 0 };
                        sequenceIsFallback = false;

                        if (animationDataRec)
                        {
                            sequenceID = animationSystem->GetSequenceIDForAnimationID(model->modelID, animationType);
                            originalAnimFlags = animationDataRec->flags[0];

                            if (sequenceID == Animation::InvalidSequenceID && animationDataRec->fallback != 0)
                            {
                                Animation::AnimationType fallbackAnimationType = static_cast<Animation::AnimationType>(animationDataRec->fallback);
                                sequenceID = animationSystem->GetSequenceIDForAnimationID(model->modelID, fallbackAnimationType);
                                sequenceIsFallback = true;
                            }
                        }

                        selectedSequenceID = sequenceID;
                    }

                    if (ImGui::Combo("##AnimationComboKeyBones", &selectedKeyBone, Animation::AnimationBoneNames, static_cast<i32>(Animation::AnimationBone::Count), -1))
                    {
                        Animation::AnimationSkeleton& skeleton = animationSystem->GetStorage().skeletons[model->modelID];
                        i16 boneIndex = animationSystem->GetBoneIndexFromKeyBoneID(model->modelID, static_cast<Animation::AnimationBone>(selectedKeyBone));

                        if (boneIndex == Animation::InvalidBoneID)
                        {
                            skeletonHasBone = false;
                        }
                        else
                        {
                            const Animation::AnimationSkeletonBone& skeletonBone = skeleton.bones[boneIndex];

                            Animation::AnimationType animationType = static_cast<Animation::AnimationType>(selectedAnimation);
                            ::Animation::AnimationSequenceID sequenceID = animationSystem->GetSequenceIDForAnimationID(model->modelID, animationType);

                            if (sequenceID == Animation::InvalidSequenceID)
                            {
                                skeletonHasBone = false;
                            }
                            else
                            {
                                const Model::ComplexModel::AnimationData<vec3>& translationAnimData = skeletonBone.info.translation;
                                const Model::ComplexModel::AnimationData<quat>& rotationAnimData = skeletonBone.info.rotation;
                                const Model::ComplexModel::AnimationData<vec3>& scaleAnimData = skeletonBone.info.scale;

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

                    bool hasAnimation = selectedSequenceID != Animation::InvalidSequenceID;
                    if (hasAnimation && skeletonHasBone)
                    {
                        if (ImGui::Button("Play"))
                        {
                            Animation::AnimationType animationType = static_cast<Animation::AnimationType>(selectedAnimation);
                            Animation::AnimationBone keyBone = static_cast<Animation::AnimationBone>(selectedKeyBone);

                            Util::Animation::SetBoneSequence(registry, *model, *animationData, keyBone, animationType);
                        }
                    }
                    else
                    {
                        ImGui::Text("This animation is not supported by the model");
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