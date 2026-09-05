#include "ControllerPosePlayer.h"

#include "Game-Lib/Rendering/Animation/Asset/AnimationAssetValidator.h"
#include "Game-Lib/Rendering/Animation/Pose/AnimationPose.h"
#include "Game-Lib/Rendering/Animation/Pose/AnimationIK.h"

#include <Base/Util/StringUtils.h>

#include <algorithm>
#include <cstring>
#include <limits>

namespace Animation
{
    ControllerPlaybackHandle ControllerPosePlayer::Create(AnimationLoading::SkeletonHandle skeleton, AnimationLoading::AnimationSetHandle animationSet, const AnimationLoading::SkeletonAssetView& skeletonView, const AnimationLoading::AnimationSetAssetView& setView, std::shared_ptr<const CompiledAnimationGraph> graph, u64 stableSeed)
    {
        if (!skeleton || !animationSet || !graph || setView.root.declaredPoseDomainType != FileFormat::Animation::PoseDomainType::Skeleton || setView.root.declaredPoseDomainAssetID != _assets->GetAssetID(skeleton))
            return {};
        const PoseSourceHandle poseSource = _poseSources->CreateRestPose(skeleton, skeletonView);
        if (!poseSource)
            return {};
        std::vector<ClipRecord> clips;
        std::vector<AnimationClipDomainRecord> domains;
        for (const FileFormat::Animation::AnimationClipVariant& variant : setView.variants)
        {
            if (std::any_of(clips.begin(), clips.end(), [&variant](const ClipRecord& clip) { return clip.assetID == variant.animationClipAssetID; }))
                continue;
            const AnimationLoading::AnimationClipHandle handle = _assets->LoadAnimationClip(variant.animationClipAssetID);
            AssetLoading::Diagnostic diagnostic;
            const AnimationLoading::AnimationClipAssetView* view = _assets->Get(handle, &diagnostic);
            if (!view || AnimationLoading::AnimationAssetValidator::ValidateDomain(*view, FileFormat::Animation::PoseDomainType::Skeleton, setView.root.declaredPoseDomainAssetID))
            {
                _assets->Release(handle);
                for (const ClipRecord& clip : clips)
                    _assets->Release(clip.handle);
                _poseSources->Destroy(poseSource);
                return {};
            }
            clips.push_back({variant.animationClipAssetID, handle});
            domains.push_back({variant.animationClipAssetID, view->root.sourcePoseDomainType, view->root.sourcePoseDomainAssetID, view->root.durationMicroseconds, view->synchronizationMarkers, view->events, view->eventPayloadBytes, (view->root.flags & FileFormat::Animation::AnimationClipFlags_Looping) != 0});
        }
        std::vector<MaskRecord> masks;
        for (const CompiledGraphLayer& layer : graph->layers)
        {
            if (layer.maskAssetID == FileFormat::INVALID_ASSET_ID || std::any_of(masks.begin(), masks.end(), [&layer](const MaskRecord& mask) { return mask.assetID == layer.maskAssetID; }))
                continue;
            const AnimationLoading::BoneMaskHandle handle = _assets->LoadBoneMask(layer.maskAssetID);
            const AnimationLoading::BoneMaskAssetView* view = _assets->Get(handle);
            MaskRecord mask = {layer.maskAssetID, handle};
            if (!view || !ResolveBoneMask(*view, FileFormat::Animation::PoseDomainType::Skeleton, setView.root.declaredPoseDomainAssetID, static_cast<u32>(skeletonView.joints.size()), mask.weights))
            {
                _assets->Release(handle);
                for (const MaskRecord& loaded : masks)
                    _assets->Release(loaded.handle);
                for (const ClipRecord& clip : clips)
                    _assets->Release(clip.handle);
                _poseSources->Destroy(poseSource);
                return {};
            }
            masks.push_back(std::move(mask));
        }
        for (const CompiledGraphActionSlot& actionSlot : graph->actionSlots)
        {
            if (actionSlot.maskAssetID == FileFormat::INVALID_ASSET_ID || std::any_of(masks.begin(), masks.end(), [&actionSlot](const MaskRecord& mask) { return mask.assetID == actionSlot.maskAssetID; }))
                continue;
            const AnimationLoading::BoneMaskHandle handle = _assets->LoadBoneMask(actionSlot.maskAssetID);
            const AnimationLoading::BoneMaskAssetView* view = _assets->Get(handle);
            MaskRecord mask = {actionSlot.maskAssetID, handle};
            if (!view || !ResolveBoneMask(*view, FileFormat::Animation::PoseDomainType::Skeleton, setView.root.declaredPoseDomainAssetID, static_cast<u32>(skeletonView.joints.size()), mask.weights))
            {
                _assets->Release(handle);
                for (const MaskRecord& loaded : masks)
                    _assets->Release(loaded.handle);
                for (const ClipRecord& clip : clips)
                    _assets->Release(clip.handle);
                _poseSources->Destroy(poseSource);
                return {};
            }
            masks.push_back(std::move(mask));
        }
        std::vector<IKRigRecord> ikRigs;
        for (const CompiledGraphIKOperation& operation : graph->ikOperations)
        {
            if (std::any_of(ikRigs.begin(), ikRigs.end(), [&operation](const IKRigRecord& rig) { return rig.assetID == operation.rigAssetID; }))
                continue;
            const AnimationLoading::IKRigHandle handle = _assets->LoadIKRig(operation.rigAssetID);
            const AnimationLoading::IKRigAssetView* view = _assets->Get(handle);
            bool valid = view && view->root.skeletonAssetID == _assets->GetAssetID(skeleton);
            for (const FileFormat::Animation::IKSolver& solver : view ? view->solvers : std::span<const FileFormat::Animation::IKSolver>())
            {
                if (solver.chainIndex >= view->chains.size() || solver.goalSpace != FileFormat::Animation::IKGoalSpace::Model)
                {
                    valid = false;
                    break;
                }
                const FileFormat::Animation::IKChain& chain = view->chains[solver.chainIndex];
                if (chain.jointIndexOffset > view->chainJointIndices.size() || chain.numJointIndices > view->chainJointIndices.size() - chain.jointIndexOffset)
                {
                    valid = false;
                    break;
                }
                for (u32 jointIndex : view->chainJointIndices.subspan(chain.jointIndexOffset, chain.numJointIndices))
                    valid &= jointIndex < skeletonView.joints.size();
            }
            if (!valid)
            {
                _assets->Release(handle);
                for (const IKRigRecord& loaded : ikRigs)
                    _assets->Release(loaded.handle);
                for (const MaskRecord& loaded : masks)
                    _assets->Release(loaded.handle);
                for (const ClipRecord& clip : clips)
                    _assets->Release(clip.handle);
                _poseSources->Destroy(poseSource);
                return {};
            }
            ikRigs.push_back({operation.rigAssetID, handle});
        }
        vec3 conservativeBoundsExpansion = {};
        for (const IKRigRecord& rig : ikRigs)
        {
            const AnimationLoading::IKRigAssetView* view = _assets->Get(rig.handle);
            conservativeBoundsExpansion = glm::max(conservativeBoundsExpansion, view->root.conservativeExtentExpansion);
        }
        if (!_poseSources->SetConservativeBoundsExpansion(poseSource, conservativeBoundsExpansion))
        {
            for (const IKRigRecord& rig : ikRigs)
                _assets->Release(rig.handle);
            for (const MaskRecord& loaded : masks)
                _assets->Release(loaded.handle);
            for (const ClipRecord& clip : clips)
                _assets->Release(clip.handle);
            _poseSources->Destroy(poseSource);
            return {};
        }
        const AnimatorHandle animator = _controllers.Create({_assets->GetAssetID(skeleton), FileFormat::INVALID_ASSET_ID, graph, stableSeed});
        if (!animator || !_controllers.BindAnimationData(animator, setView, domains))
        {
            if (animator)
                _controllers.Destroy(animator);
            for (const MaskRecord& mask : masks)
                _assets->Release(mask.handle);
            for (const IKRigRecord& rig : ikRigs)
                _assets->Release(rig.handle);
            for (const ClipRecord& clip : clips)
                _assets->Release(clip.handle);
            _poseSources->Destroy(poseSource);
            return {};
        }
        u32 slotIndex = 0;
        if (_freeSlots.empty())
        {
            slotIndex = static_cast<u32>(_slots.size());
            _slots.emplace_back();
        }
        else
        {
            slotIndex = _freeSlots.back();
            _freeSlots.pop_back();
        }
        Slot& slot = _slots[slotIndex];
        slot.skeleton = skeleton;
        slot.animationSet = animationSet;
        slot.animator = animator;
        slot.poseSource = poseSource;
        slot.clips = std::move(clips);
        slot.masks = std::move(masks);
        slot.ikRigs = std::move(ikRigs);
        slot.clipDomains = std::move(domains);
        slot.localPose.reserve(skeletonView.joints.size());
        for (const FileFormat::Animation::SkeletonJoint& joint : skeletonView.joints)
            slot.localPose.push_back(joint.restTransform);
        slot.samplePose.resize(skeletonView.joints.size());
        slot.lastRecipeHash = 0;
        slot.live = true;
        _poseToController[poseSource.GetRawValue()] = slotIndex;
        return ControllerPlaybackHandle::Create(slotIndex, slot.generation);
    }

    bool ControllerPosePlayer::Destroy(ControllerPlaybackHandle handle)
    {
        Slot* slot = GetSlot(handle);
        if (!slot)
            return false;
        _poseToController.erase(slot->poseSource.GetRawValue());
        _controllers.Destroy(slot->animator);
        _poseSources->Destroy(slot->poseSource);
        for (const ClipRecord& clip : slot->clips)
            _assets->Release(clip.handle);
        for (const MaskRecord& mask : slot->masks)
            _assets->Release(mask.handle);
        for (const IKRigRecord& rig : slot->ikRigs)
            _assets->Release(rig.handle);
        _assets->Release(slot->animationSet);
        _assets->Release(slot->skeleton);
        const u32 slotIndex = handle.GetSlot();
        *slot = {};
        slot->generation = handle.GetGeneration() + 1u;
        if (slot->generation == 0)
            slot->generation = 1;
        _freeSlots.push_back(slotIndex);
        return true;
    }

    void ControllerPosePlayer::Advance(f64 deltaSeconds)
    {
        _controllers.Advance(deltaSeconds);
        for (Slot& slot : _slots)
        {
            if (!slot.live)
                continue;
            const AnimationLoading::AnimationSetAssetView* set = _assets->Get(slot.animationSet);
            if (!set)
                continue;
            FileFormat::AssetID clipAssetID = FileFormat::INVALID_ASSET_ID;
            u64 timeMicroseconds = 0;
            bool supportsTightBounds = false;
            if (_controllers.GetBoundsMetadata(slot.animator, *set, slot.clipDomains, clipAssetID, timeMicroseconds, supportsTightBounds))
                _poseSources->SetBoundsMetadata(slot.poseSource, clipAssetID, timeMicroseconds, supportsTightBounds);
        }
        ++_stats.advances;
    }

    bool ControllerPosePlayer::RequestPose(PoseSourceHandle poseSource)
    {
        const auto found = _poseToController.find(poseSource.GetRawValue());
        if (found == _poseToController.end() || found->second >= _slots.size() || !_slots[found->second].live)
            return false;
        Slot& slot = _slots[found->second];
        ++_stats.poseRequests;
        const AnimationLoading::SkeletonAssetView* skeleton = _assets->Get(slot.skeleton);
        const AnimationLoading::AnimationSetAssetView* animationSet = _assets->Get(slot.animationSet);
        if (!skeleton || !animationSet)
        {
            ++_stats.failures;
            return false;
        }
        const PoseRecipe recipe = _controllers.RequestPose(slot.animator, *animationSet, slot.clipDomains, slot.localPose);
        std::vector<f32> debugJointWeights(recipe.layerCount != 0 ? skeleton->joints.size() : 0, 0.0f);
        std::array<PoseIKDebugOperation, 4> debugIKOperations = {};
        u32 debugIKOperationCount = 0;
        u64 recipeHash = recipe.sampleCount;
        for (u32 sampleIndex = 0; sampleIndex < recipe.sampleCount; ++sampleIndex)
        {
            const PoseRecipeSample& sample = recipe.samples[sampleIndex];
            recipeHash = XXHash64::hash(&sample.clipAssetID, sizeof(sample.clipAssetID), recipeHash);
            recipeHash = XXHash64::hash(&sample.timeMicroseconds, sizeof(sample.timeMicroseconds), recipeHash);
            recipeHash = XXHash64::hash(&sample.weight, sizeof(sample.weight), recipeHash);
            recipeHash = XXHash64::hash(&sample.stableVariationID, sizeof(sample.stableVariationID), recipeHash);
        }
        recipeHash = XXHash64::hash(&recipe.layerCount, sizeof(recipe.layerCount), recipeHash);
        for (u32 layerIndex = 0; layerIndex < recipe.layerCount; ++layerIndex)
        {
            const PoseRecipeLayer& layer = recipe.layers[layerIndex];
            recipeHash = XXHash64::hash(&layer.sample, sizeof(layer.sample), recipeHash);
            recipeHash = XXHash64::hash(&layer.maskAssetID, sizeof(layer.maskAssetID), recipeHash);
            recipeHash = XXHash64::hash(&layer.weight, sizeof(layer.weight), recipeHash);
            recipeHash = XXHash64::hash(&layer.type, sizeof(layer.type), recipeHash);
            recipeHash = XXHash64::hash(&layer.executionOrder, sizeof(layer.executionOrder), recipeHash);
        }
        recipeHash = XXHash64::hash(&recipe.transitionWeight, sizeof(recipe.transitionWeight), recipeHash);
        recipeHash = XXHash64::hash(&recipe.usesSnapshot, sizeof(recipe.usesSnapshot), recipeHash);
        recipeHash = XXHash64::hash(&recipe.ikOperationCount, sizeof(recipe.ikOperationCount), recipeHash);
        slot.ikWorkspace.current = false;
        for (u32 operationIndex = 0; operationIndex < recipe.ikOperationCount; ++operationIndex)
        {
            const PoseRecipeIKOperation& operation = recipe.ikOperations[operationIndex];
            recipeHash = XXHash64::hash(&operation.rigAssetID, sizeof(operation.rigAssetID), recipeHash);
            recipeHash = XXHash64::hash(&operation.solverSemanticID, sizeof(operation.solverSemanticID), recipeHash);
            recipeHash = XXHash64::hash(&operation.goalModel, sizeof(operation.goalModel), recipeHash);
            recipeHash = XXHash64::hash(&operation.goalAttachmentSemanticID, sizeof(operation.goalAttachmentSemanticID), recipeHash);
            recipeHash = XXHash64::hash(&operation.effectorAttachmentSemanticID, sizeof(operation.effectorAttachmentSemanticID), recipeHash);
            recipeHash = XXHash64::hash(&operation.effectorAlignmentAxis, sizeof(operation.effectorAlignmentAxis), recipeHash);
            recipeHash = XXHash64::hash(&operation.weight, sizeof(operation.weight), recipeHash);
            recipeHash = XXHash64::hash(&operation.executionOrder, sizeof(operation.executionOrder), recipeHash);
        }
        if (recipeHash == slot.lastRecipeHash)
            return true;

        std::vector<FileFormat::Animation::LocalTransform> composed = slot.localPose;
        bool hasComposedPose = recipe.usesSnapshot;
        f32 accumulatedWeight = recipe.usesSnapshot ? std::max(0.0f, 1.0f - recipe.transitionWeight) : 0.0f;
        for (u32 sampleIndex = 0; sampleIndex < recipe.sampleCount; ++sampleIndex)
        {
            const PoseRecipeSample& sample = recipe.samples[sampleIndex];
            const AnimationLoading::AnimationClipAssetView* clip = FindClip(slot, sample.clipAssetID);
            if (!clip || SampleClipLocalPose(*clip, *skeleton, static_cast<f64>(sample.timeMicroseconds) / 1'000'000.0, (clip->root.flags & FileFormat::Animation::AnimationClipFlags_Looping) != 0, slot.samplePose) != ClipSampleError::None)
            {
                ++_stats.failures;
                return false;
            }
            if (!hasComposedPose)
            {
                composed = slot.samplePose;
                accumulatedWeight = sample.weight;
                hasComposedPose = true;
            }
            else
            {
                const f32 denominator = accumulatedWeight + sample.weight;
                const f32 blendWeight = denominator > 0.0f ? sample.weight / denominator : 0.0f;
                if (!BlendLocalPoses(composed, slot.samplePose, blendWeight, composed))
                    return false;
                accumulatedWeight = denominator;
                ++_stats.blendedSamples;
            }
        }
        if (!hasComposedPose)
            return true;
        for (u32 layerIndex = 0; layerIndex < recipe.layerCount; ++layerIndex)
        {
            const PoseRecipeLayer& layer = recipe.layers[layerIndex];
            const AnimationLoading::AnimationClipAssetView* clip = FindClip(slot, layer.sample.clipAssetID);
            const MaskRecord* mask = layer.maskAssetID == FileFormat::INVALID_ASSET_ID ? nullptr : FindMask(slot, layer.maskAssetID);
            if (!clip || (layer.maskAssetID != FileFormat::INVALID_ASSET_ID && !mask) || SampleClipLocalPose(*clip, *skeleton, static_cast<f64>(layer.sample.timeMicroseconds) / 1000000.0, (clip->root.flags & FileFormat::Animation::AnimationClipFlags_Looping) != 0, slot.samplePose) != ClipSampleError::None)
            {
                ++_stats.failures;
                return false;
            }
            const std::span<const f32> weights = mask ? std::span<const f32>(mask->weights) : std::span<const f32>();
            const bool valid = layer.type == AnimationLayerType::Additive ? ((clip->root.flags & FileFormat::Animation::AnimationClipFlags_Additive) != 0 && ApplyAdditiveLocalPose(composed, slot.samplePose, layer.weight, weights, composed)) : BlendOverrideLocalPoses(composed, slot.samplePose, layer.weight, weights, composed);
            if (!valid)
            {
                ++_stats.failures;
                return false;
            }
            for (u32 jointIndex = 0; jointIndex < debugJointWeights.size(); ++jointIndex)
            {
                const f32 maskWeight = mask ? mask->weights[jointIndex] : 1.0f;
                debugJointWeights[jointIndex] = std::max(debugJointWeights[jointIndex], std::clamp(layer.weight * maskWeight, 0.0f, 1.0f));
            }
            ++_stats.layerOperations;
        }
        for (u32 operationIndex = 0; operationIndex < recipe.ikOperationCount; ++operationIndex)
        {
            const PoseRecipeIKOperation& operation = recipe.ikOperations[operationIndex];
            const IKRigRecord* rigRecord = FindIKRig(slot, operation.rigAssetID);
            const AnimationLoading::IKRigAssetView* rig = rigRecord ? _assets->Get(rigRecord->handle) : nullptr;
            if (!rig)
            {
                ++_stats.failures;
                return false;
            }
            const auto solver = std::find_if(rig->solvers.begin(), rig->solvers.end(), [&operation](const FileFormat::Animation::IKSolver& value) { return value.semanticID == operation.solverSemanticID; });
            if (solver == rig->solvers.end() || solver->chainIndex >= rig->chains.size())
            {
                ++_stats.failures;
                return false;
            }
            const FileFormat::Animation::IKChain& chain = rig->chains[solver->chainIndex];
            const std::span<const u32> chainJoints = rig->chainJointIndices.subspan(chain.jointIndexOffset, chain.numJointIndices);
            vec3 goalModel = operation.goalModel;
            vec3 effectorLocalPoint = {};
            vec3 goalModelAxis = {};
            if (operation.goalAttachmentSemanticID != 0)
            {
                const auto attachment = std::find_if(skeleton->attachments.begin(), skeleton->attachments.end(), [&operation](const FileFormat::Animation::SkeletonAttachment& value) { return value.semanticID == operation.goalAttachmentSemanticID; });
                if (attachment == skeleton->attachments.end() || attachment->jointIndex >= skeleton->joints.size())
                {
                    ++_stats.failures;
                    return false;
                }
                if (slot.ikWorkspace.modelTransforms.size() != skeleton->joints.size())
                    slot.ikWorkspace.modelTransforms.resize(skeleton->joints.size());
                if (!slot.ikWorkspace.current && !UpdateModelTransforms(skeleton->joints, composed, 0, slot.ikWorkspace.modelTransforms))
                    return false;
                slot.ikWorkspace.current = true;
                const mat4x4 socketModel = mat4x4(slot.ikWorkspace.modelTransforms[attachment->jointIndex]) * mat4x4(ComposeLocalTransform(attachment->localTransform));
                if (glm::length2(operation.effectorAlignmentAxis) > 1.0e-8f && glm::length2(goalModel) > 1.0e-8f)
                    goalModelAxis = glm::normalize(vec3(socketModel * vec4(glm::normalize(goalModel), 0.0f)));
                goalModel = vec3(socketModel * vec4(goalModel, 1.0f));
            }
            if (operation.effectorAttachmentSemanticID != 0)
            {
                const auto attachment = std::find_if(skeleton->attachments.begin(), skeleton->attachments.end(), [&operation](const FileFormat::Animation::SkeletonAttachment& value) { return value.semanticID == operation.effectorAttachmentSemanticID; });
                if (attachment == skeleton->attachments.end() || attachment->jointIndex != chain.effectorJointIndex)
                {
                    ++_stats.failures;
                    return false;
                }
                effectorLocalPoint = vec3(ComposeLocalTransform(attachment->localTransform)[3]);
            }
            const f32 goalDistance = glm::length(goalModel);
            bool rigGoalClamped = false;
            if (rig->root.maximumGoalDistance > 0.0f && goalDistance > rig->root.maximumGoalDistance)
            {
                goalModel *= rig->root.maximumGoalDistance / goalDistance;
                rigGoalClamped = true;
            }
            IKSolveResult solve;
            vec3 polePosition = {};
            if (slot.ikWorkspace.modelTransforms.size() != skeleton->joints.size())
                slot.ikWorkspace.modelTransforms.resize(skeleton->joints.size());
            if (!slot.ikWorkspace.current && !UpdateModelTransforms(skeleton->joints, composed, 0, slot.ikWorkspace.modelTransforms))
                return false;
            slot.ikWorkspace.current = true;
            if (solver->type == FileFormat::Animation::IKSolverType::TwoBone && chainJoints.size() == 3)
            {
                const vec3 rootPosition = vec3(slot.ikWorkspace.modelTransforms[chainJoints[0]][3]);
                polePosition = rootPosition + chain.preferredBendDirection;
                const u32 maximumIterations = std::max(1u, static_cast<u32>(solver->maximumIterations));
                f32 bestError = std::numeric_limits<f32>::infinity();
                IKSolveResult bestSolve;
                for (u32 iteration = 0; iteration < maximumIterations; ++iteration)
                {
                    solve = SolveTwoBoneIK(skeleton->joints, chainJoints[0], chainJoints[1], chain.effectorJointIndex, goalModel, polePosition, chain.maximumReach * solver->maximumReachScale, operation.weight, composed, &slot.ikWorkspace, effectorLocalPoint, operation.effectorAlignmentAxis, goalModelAxis);
                    if (solve && solve.finalError < bestError)
                    {
                        bestError = solve.finalError;
                        bestSolve = solve;
                        slot.ikBestPose.assign(composed.begin(), composed.end());
                    }
                    if (!solve || solve.finalError <= solver->convergenceTolerance)
                        break;
                }
                if (bestError < solve.finalError)
                {
                    std::copy(slot.ikBestPose.begin(), slot.ikBestPose.end(), composed.begin());
                    slot.ikWorkspace.current = false;
                    solve = bestSolve;
                }
            }
            else if (solver->type == FileFormat::Animation::IKSolverType::IterativeChain && chainJoints.size() >= 2)
            {
                polePosition = vec3(slot.ikWorkspace.modelTransforms[chainJoints.front()][3]) + chain.preferredBendDirection;
                solve = SolveIterativeChainIK(skeleton->joints, chainJoints, chain.effectorJointIndex, goalModel, chain.maximumReach * solver->maximumReachScale, solver->maximumIterations, solver->convergenceTolerance, operation.weight, composed, &slot.ikWorkspace, effectorLocalPoint, operation.effectorAlignmentAxis, goalModelAxis);
            }
            else if ((solver->type == FileFormat::Animation::IKSolverType::Aim || solver->type == FileFormat::Animation::IKSolverType::LookAt) && !chainJoints.empty())
            {
                polePosition = vec3(slot.ikWorkspace.modelTransforms[chainJoints.front()][3]) + chain.preferredBendDirection;
                solve = SolveAimIK(skeleton->joints, chainJoints.front(), chain.preferredBendDirection, goalModel, operation.weight, composed, &slot.ikWorkspace);
            }
            if (!solve)
            {
                ++_stats.failures;
                return false;
            }
            bool jointLimitClamped = false;
            const std::span<const FileFormat::Animation::IKJointLimit> jointLimits = chain.numJointLimits == 0 ? std::span<const FileFormat::Animation::IKJointLimit>() : rig->jointLimits.subspan(chain.jointLimitOffset, chain.numJointLimits);
            if (!ApplyIKJointLimits(chainJoints, jointLimits, composed, jointLimitClamped))
            {
                ++_stats.failures;
                return false;
            }
            if (jointLimitClamped)
                slot.ikWorkspace.current = false;
            ++_stats.ikOperations;
            if (solve.status == IKSolveStatus::Clamped || rigGoalClamped || jointLimitClamped)
                ++_stats.ikClampedGoals;
            PoseIKDebugOperation& debugOperation = debugIKOperations[debugIKOperationCount++];
            debugOperation.jointCount = static_cast<u32>(std::min(chainJoints.size(), debugOperation.jointIndices.size()));
            std::copy_n(chainJoints.begin(), debugOperation.jointCount, debugOperation.jointIndices.begin());
            debugOperation.goalModel = goalModel;
            debugOperation.poleModel = polePosition;
            debugOperation.maximumReach = chain.maximumReach * solver->maximumReachScale;
            debugOperation.finalError = solve.finalError;
            debugOperation.status = solve.status;
            debugOperation.clamped = solve.status == IKSolveStatus::Clamped || rigGoalClamped || jointLimitClamped;
            debugOperation.jointLimitClamped = jointLimitClamped;
        }
        if (recipe.usesSnapshot)
            ++_stats.snapshotBlends;
        slot.localPose = std::move(composed);
        slot.lastRecipeHash = recipeHash;
        const u64 sampleTime = recipe.sampleCount ? recipe.samples[recipe.sampleCount - 1].timeMicroseconds : 0;
        if (!_poseSources->SetDebugJointWeights(slot.poseSource, debugJointWeights) || !_poseSources->SetDebugIKOperations(slot.poseSource, std::span<const PoseIKDebugOperation>(debugIKOperations.data(), debugIKOperationCount)) || !_poseSources->SetLocalPose(slot.poseSource, slot.localPose, *skeleton, sampleTime))
        {
            ++_stats.failures;
            return false;
        }
        ++_stats.poseEvaluations;
        return true;
    }

    PoseSourceHandle ControllerPosePlayer::GetPoseSource(ControllerPlaybackHandle handle) const
    {
        const Slot* slot = GetSlot(handle);
        return slot ? slot->poseSource : PoseSourceHandle{};
    }

    AnimationParameterHandle ControllerPosePlayer::ResolveParameter(ControllerPlaybackHandle handle, u64 nameHash) const
    {
        const Slot* slot = GetSlot(handle);
        return slot ? _controllers.ResolveParameter(slot->animator, nameHash) : AnimationParameterHandle{};
    }

    bool ControllerPosePlayer::QueueAction(ControllerPlaybackHandle handle, u64 semanticID, u32 payload)
    {
        Slot* slot = GetSlot(handle);
        return slot && _controllers.QueueAction(slot->animator, semanticID, payload);
    }

    bool ControllerPosePlayer::GetState(ControllerPlaybackHandle handle, AnimationControllerState& state) const
    {
        const Slot* slot = GetSlot(handle);
        return slot && _controllers.GetState(slot->animator, state);
    }

    bool ControllerPosePlayer::GetParameterValues(ControllerPlaybackHandle handle, std::vector<vec3>& values) const
    {
        const Slot* slot = GetSlot(handle);
        return slot && _controllers.GetParameterValues(slot->animator, values);
    }

    ControllerPosePlayer::Slot* ControllerPosePlayer::GetSlot(ControllerPlaybackHandle handle)
    {
        if (!handle || handle.GetSlot() >= _slots.size())
            return nullptr;
        Slot& slot = _slots[handle.GetSlot()];
        return slot.live && slot.generation == handle.GetGeneration() ? &slot : nullptr;
    }

    const ControllerPosePlayer::Slot* ControllerPosePlayer::GetSlot(ControllerPlaybackHandle handle) const
    {
        if (!handle || handle.GetSlot() >= _slots.size())
            return nullptr;
        const Slot& slot = _slots[handle.GetSlot()];
        return slot.live && slot.generation == handle.GetGeneration() ? &slot : nullptr;
    }

    const ControllerPosePlayer::MaskRecord* ControllerPosePlayer::FindMask(const Slot& slot, FileFormat::AssetID assetID) const
    {
        const auto found = std::find_if(slot.masks.begin(), slot.masks.end(), [assetID](const MaskRecord& mask) { return mask.assetID == assetID; });
        return found == slot.masks.end() ? nullptr : &*found;
    }

    const ControllerPosePlayer::IKRigRecord* ControllerPosePlayer::FindIKRig(const Slot& slot, FileFormat::AssetID assetID) const
    {
        const auto found = std::find_if(slot.ikRigs.begin(), slot.ikRigs.end(), [assetID](const IKRigRecord& rig) { return rig.assetID == assetID; });
        return found == slot.ikRigs.end() ? nullptr : &*found;
    }

    const AnimationLoading::AnimationClipAssetView* ControllerPosePlayer::FindClip(const Slot& slot, FileFormat::AssetID assetID) const
    {
        const auto found = std::find_if(slot.clips.begin(), slot.clips.end(), [assetID](const ClipRecord& clip) { return clip.assetID == assetID; });
        return found != slot.clips.end() ? _assets->Get(found->handle) : nullptr;
    }
}
