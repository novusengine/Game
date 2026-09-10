#include "ControllerPosePlayer.h"
#include "Game-Lib/Rendering/Animation/Asset/AnimationAssetDocument.h"

#include "Game-Lib/Rendering/Animation/Asset/AnimationAssetValidator.h"
#include "Game-Lib/Rendering/Animation/Pose/AnimationPose.h"
#include "Game-Lib/Rendering/Animation/Pose/AnimationIK.h"

#include <Base/Util/StringUtils.h>
#include <tracy/Tracy.hpp>

#include <algorithm>
#include <cstring>
#include <limits>

namespace Animation
{
    ControllerPlaybackHandle ControllerPosePlayer::Create(AnimationLoading::SkeletonHandle skeleton, AnimationLoading::AnimationSetHandle animationSet, const AnimationLoading::SkeletonAssetView& skeletonView, const AnimationLoading::AnimationSetAssetView& setView, std::shared_ptr<const CompiledAnimationGraph> graph, u64 stableSeed, FileFormat::AssetID retargetProfileAssetID)
    {
        const bool familySet = setView.root.declaredPoseDomainType == FileFormat::Animation::PoseDomainType::RigFamily;
        const bool needsBridge = familySet && setView.root.declaredPoseDomainAssetID != skeletonView.root.rigFamilyAssetID;
        if (!skeleton || !animationSet || !graph || (!familySet && setView.root.declaredPoseDomainAssetID != _assets->GetRetainedAssetID(skeleton)) || (needsBridge && retargetProfileAssetID == FileFormat::INVALID_ASSET_ID))
            return {};
        FamilyPoseBinding targetBinding;
        FamilyPoseBridge bridge;
        u32 familyJointCount = 0;
        u32 targetFamilyJointCount = 0;
        if (familySet)
        {
            const auto handle = _assets->LoadRigFamily(skeletonView.root.rigFamilyAssetID);
            const auto* family = _assets->GetRetained(handle);
            bool valid = family && targetBinding.Initialize(skeletonView, *family);
            familyJointCount = family ? static_cast<u32>(family->joints.size()) : 0;
            targetFamilyJointCount = familyJointCount;
            if (valid && needsBridge)
            {
                const auto sourceHandle = _assets->LoadRigFamily(setView.root.declaredPoseDomainAssetID);
                const auto profileHandle = _assets->LoadRetargetProfile(retargetProfileAssetID);
                const auto* source = _assets->GetRetained(sourceHandle);
                const auto* profile = _assets->GetRetained(profileHandle);
                valid = source && profile && profile->root.sourceRigFamilyAssetID == setView.root.declaredPoseDomainAssetID && profile->root.targetRigFamilyAssetID == skeletonView.root.rigFamilyAssetID && bridge.Initialize(*profile, *source, *family);
                familyJointCount = source ? static_cast<u32>(source->joints.size()) : 0;
                _assets->Release(profileHandle);
                _assets->Release(sourceHandle);
            }
            _assets->Release(handle);
            if (!valid)
                return {};
        }
        const PoseSourceHandle poseSource = _poseSources->CreateRestPose(skeleton, skeletonView);
        if (!poseSource)
            return {};
        std::vector<ClipRecord> clips;
        std::vector<AnimationClipDomainRecord> domains;
        u32 domainCount = 0;
        for (const FileFormat::Animation::AnimationClipVariant& variant : setView.variants)
        {
            if (std::any_of(clips.begin(), clips.end(), [&variant](const ClipRecord& clip) { return clip.assetID == variant.animationClipAssetID; }))
                continue;
            const AnimationLoading::AnimationClipHandle handle = _assets->LoadAnimationClip(variant.animationClipAssetID);
            AssetLoading::Diagnostic diagnostic;
            const AnimationLoading::AnimationClipAssetView* view = _assets->GetRetained(handle, &diagnostic);
            std::shared_ptr<SourceDomain> sourceDomain;
            bool compatible = view != nullptr;
            if (view && familySet)
            {
                const auto existing = std::find_if(clips.begin(), clips.end(), [view](const ClipRecord& record) { return record.domain && record.domain->assetID == view->root.sourcePoseDomainAssetID && record.domain->type == view->root.sourcePoseDomainType; });
                if (existing != clips.end())
                    sourceDomain = existing->domain;
                else
                {
                    ++domainCount;
                    sourceDomain = std::make_shared<SourceDomain>();
                    sourceDomain->assets = _assets;
                    sourceDomain->assetID = view->root.sourcePoseDomainAssetID;
                    sourceDomain->type = view->root.sourcePoseDomainType;
                    sourceDomain->family = _assets->LoadRigFamily(setView.root.declaredPoseDomainAssetID);
                    const auto* family = _assets->GetRetained(sourceDomain->family);
                    compatible = family != nullptr;
                    if (compatible && sourceDomain->type == FileFormat::Animation::PoseDomainType::Skeleton)
                    {
                        sourceDomain->skeleton = _assets->LoadSkeleton(sourceDomain->assetID);
                        const auto* source = _assets->GetRetained(sourceDomain->skeleton);
                        compatible = source && source->root.rigFamilyAssetID == setView.root.declaredPoseDomainAssetID && sourceDomain->binding.Initialize(*source, *family);
                        if (compatible)
                            sourceDomain->samplingJoints.assign(source->joints.begin(), source->joints.end());
                    }
                    else if (compatible)
                    {
                        compatible = sourceDomain->assetID == setView.root.declaredPoseDomainAssetID;
                        for (const auto& joint : family->joints)
                        {
                            FileFormat::Animation::SkeletonJoint sampling;
                            sampling.restTransform = joint.canonicalTransform;
                            sourceDomain->samplingJoints.push_back(sampling);
                        }
                    }
                    compatible &= domainCount <= graph->maximumPoseDomains;
                }
            }
            else if (view)
                compatible = !AnimationLoading::AnimationAssetValidator::ValidateDomain(*view, FileFormat::Animation::PoseDomainType::Skeleton, setView.root.declaredPoseDomainAssetID);
            if (!compatible)
            {
                _assets->Release(handle);
                for (const ClipRecord& clip : clips)
                    _assets->Release(clip.handle);
                _poseSources->Destroy(poseSource);
                return {};
            }
            clips.push_back({variant.animationClipAssetID, handle, sourceDomain});
            domains.push_back({variant.animationClipAssetID, view->root.sourcePoseDomainType, view->root.sourcePoseDomainAssetID, view->root.durationMicroseconds, view->synchronizationMarkers, view->events, view->eventPayloadBytes, (view->root.flags & FileFormat::Animation::AnimationClipFlags_Looping) != 0});
            if (familySet)
                domains.back().compatibleFamilyAssetID = setView.root.declaredPoseDomainAssetID;
        }
        std::vector<MaskRecord> masks;
        // An independently timed target layer cannot reuse a converted domain.
        // Author it inside that source domain instead.
        if (familySet)
        {
            auto references = [&](u64 semantic, const SourceDomain* domain)
            {
                auto slot = std::find_if(setView.slots.begin(), setView.slots.end(), [semantic](const auto& value) { return value.semanticID == semantic; });
                if (slot == setView.slots.end())
                    return false;
                u32 depth = 0;
                while (slot->numVariants == 0 && slot->fallbackSlotIndex >= 0)
                {
                    if (++depth > setView.slots.size() || static_cast<u32>(slot->fallbackSlotIndex) >= setView.slots.size())
                        return false;
                    slot = setView.slots.begin() + slot->fallbackSlotIndex;
                }
                if (slot->variantOffset > setView.variants.size() || slot->numVariants > setView.variants.size() - slot->variantOffset)
                    return false;
                for (u32 index = 0; index < slot->numVariants; ++index)
                    for (const auto& record : clips)
                        if (record.assetID == setView.variants[slot->variantOffset + index].animationClipAssetID && record.domain.get() == domain)
                            return true;
                return false;
            };
            bool valid = true;
            for (const auto& record : clips)
            {
                const auto* domain = record.domain.get();
                if (!domain || (domain->type == FileFormat::Animation::PoseDomainType::Skeleton && domain->assetID == _assets->GetRetainedAssetID(skeleton)))
                    continue;
                u32 targetUses = 0;
                bool base = false;
                for (const auto& state : graph->states)
                    base |= references(state.clipSlotSemanticID, domain);
                for (const auto& sample : graph->blendSamples)
                    base |= references(sample.clipSlotSemanticID, domain);
                for (const auto& layer : graph->layers)
                    targetUses += !layer.composeInSourceDomain && references(layer.clipSlotSemanticID, domain) ? 1u : 0u;
                for (const auto& action : graph->actionSlots)
                    targetUses += references(action.clipSlotSemanticID, domain) ? 1u : 0u;
                valid &= targetUses + (base ? 1u : 0u) <= 1;
            }
            if (!valid)
            {
                for (const auto& clip : clips) _assets->Release(clip.handle);
                _poseSources->Destroy(poseSource);
                return {};
            }
        }
        auto resolveMask = [&](const AnimationLoading::BoneMaskAssetView& mask, std::vector<f32>& weights)
        {
            if (mask.root.poseDomainType == FileFormat::Animation::PoseDomainType::Skeleton)
                return ResolveBoneMask(mask, FileFormat::Animation::PoseDomainType::Skeleton, _assets->GetRetainedAssetID(skeleton), static_cast<u32>(skeletonView.joints.size()), weights);
            std::vector<f32> canonical;
            if (!familySet || !ResolveBoneMask(mask, FileFormat::Animation::PoseDomainType::RigFamily, skeletonView.root.rigFamilyAssetID, targetFamilyJointCount, canonical))
                return false;
            weights.assign(skeletonView.joints.size(), 0.0f);
            for (const auto& binding : skeletonView.familyBindings)
                weights[binding.skeletonJointIndex] = canonical[binding.familyJointIndex];
            return true;
        };
        for (const CompiledGraphLayer& layer : graph->layers)
        {
            if (layer.maskAssetID == FileFormat::INVALID_ASSET_ID || std::any_of(masks.begin(), masks.end(), [&layer](const MaskRecord& mask) { return mask.assetID == layer.maskAssetID; }))
                continue;
            const AnimationLoading::BoneMaskHandle handle = _assets->LoadBoneMask(layer.maskAssetID);
            const AnimationLoading::BoneMaskAssetView* view = _assets->GetRetained(handle);
            MaskRecord mask = {layer.maskAssetID, handle};
            if (!view || !resolveMask(*view, mask.weights))
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
            const AnimationLoading::BoneMaskAssetView* view = _assets->GetRetained(handle);
            MaskRecord mask = {actionSlot.maskAssetID, handle};
            if (!view || !resolveMask(*view, mask.weights))
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
            const AnimationLoading::IKRigAssetView* view = _assets->GetRetained(handle);
            bool valid = view && view->root.skeletonAssetID == _assets->GetRetainedAssetID(skeleton);
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
            const AnimationLoading::IKRigAssetView* view = _assets->GetRetained(rig.handle);
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
        const AnimatorHandle animator = _controllers.Create({_assets->GetRetainedAssetID(skeleton), FileFormat::INVALID_ASSET_ID, graph, stableSeed});
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
        slot.targetFamily = familySet ? _assets->LoadRigFamily(skeletonView.root.rigFamilyAssetID) : AnimationLoading::RigFamilyHandle{};
        slot.retargetProfile = needsBridge ? _assets->LoadRetargetProfile(retargetProfileAssetID) : AnimationLoading::RetargetProfileHandle{};
        slot.animator = animator;
        slot.graph = graph;
        slot.stableSeed = stableSeed;
        slot.poseSource = poseSource;
        slot.clips = std::move(clips);
        slot.masks = std::move(masks);
        slot.ikRigs = std::move(ikRigs);
        slot.clipDomains = std::move(domains);
        slot.targetBinding = std::move(targetBinding);
        slot.bridge = std::move(bridge);
        slot.hasBridge = needsBridge;
        slot.correctRetargetChains = graph->retargetChainCorrection;
        slot.familyJointCount = familyJointCount;
        slot.localPose.reserve(skeletonView.joints.size());
        for (const FileFormat::Animation::SkeletonJoint& joint : skeletonView.joints)
            slot.localPose.push_back(joint.restTransform);
        slot.continuityPose = slot.localPose;
        slot.samplePose.resize(skeletonView.joints.size());
        slot.lastRecipeHash = 0;
        slot.live = true;
        _poseToController[poseSource.GetRawValue()] = slotIndex;
        return ControllerPlaybackHandle::Create(slotIndex, slot.generation);
    }

    bool ControllerPosePlayer::QueueGraph(ControllerPlaybackHandle handle, std::shared_ptr<const CompiledAnimationGraph> graph, std::string& error)
    {
        error.clear();
        Slot* slot = GetSlot(handle);
        if (!slot || !graph)
        {
            error = "Controller or compiled graph is unavailable";
            return false;
        }
        const auto* set = _assets->GetRetained(slot->animationSet);
        if (!set)
        {
            error = "The animation set is unavailable";
            return false;
        }
        const auto skeleton = _assets->LoadSkeleton(_assets->GetRetainedAssetID(slot->skeleton));
        const auto animationSet = _assets->LoadAnimationSet(_assets->GetRetainedAssetID(slot->animationSet));
        const auto* skeletonView = _assets->GetRetained(skeleton);
        const auto* setView = _assets->GetRetained(animationSet);
        const auto seed = slot->stableSeed;
        const auto profile = _assets->GetRetainedAssetID(slot->retargetProfile);
        const auto candidate = skeletonView && setView ? Create(skeleton, animationSet, *skeletonView, *setView, graph, seed, profile) : ControllerPlaybackHandle{};
        if (!candidate)
        {
            _assets->Release(skeleton);
            _assets->Release(animationSet);
            error = "Replacement resource binding failed; running graph unchanged";
            return false;
        }
        // Creating the candidate can grow the slot vector.
        slot = GetSlot(handle);
        const auto* resources = GetSlot(candidate);
        const auto* replacementSet = _assets->GetRetained(resources->animationSet);
        const auto resolves = [&](u64 semantic) { return static_cast<bool>(ResolveAnimationSlot(*replacementSet, resources->clipDomains, semantic, seed)); };
        bool resolved = true;
        for (const auto& state : graph->states) resolved &= state.kind != GraphStateKind::Clip || resolves(state.clipSlotSemanticID);
        for (const auto& sample : graph->blendSamples) resolved &= resolves(sample.clipSlotSemanticID);
        for (const auto& layer : graph->layers) resolved &= resolves(layer.clipSlotSemanticID);
        for (const auto& action : graph->actionSlots) resolved &= resolves(action.clipSlotSemanticID);
        for (const auto& operation : graph->ikOperations)
        {
            const auto* rig = FindIKRig(*resources, operation.rigAssetID);
            const auto* view = rig ? _assets->GetRetained(rig->handle) : nullptr;
            resolved &= view && std::any_of(view->solvers.begin(), view->solvers.end(), [&](const auto& solver) { return solver.semanticID == operation.solverSemanticID; });
        }
        if (!resolved)
        {
            Destroy(candidate);
            error = "Replacement has unresolved clip slots or IK solvers; running graph unchanged";
            return false;
        }
        if (slot->pendingResources)
            Destroy(slot->pendingResources);
        slot->pendingResources = candidate;
        slot->pendingResourcesCompatible = true;
        slot->pendingGraph = std::move(graph);
        return true;
    }

    bool ControllerPosePlayer::GetGraphPublication(ControllerPlaybackHandle handle, u64& contentHash, bool& pending, AnimationGraphMigration& migration) const
    {
        const Slot* slot = GetSlot(handle);
        if (!slot || !slot->graph)
            return false;
        contentHash = slot->graph->contentHash;
        pending = slot->pendingGraph != nullptr;
        migration = slot->migration;
        return true;
    }

    bool ControllerPosePlayer::Destroy(ControllerPlaybackHandle handle)
    {
        Slot* slot = GetSlot(handle);
        if (!slot)
            return false;
        if (slot->pendingResources)
            Destroy(slot->pendingResources);
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
        if (slot->targetFamily) _assets->Release(slot->targetFamily);
        if (slot->retargetProfile) _assets->Release(slot->retargetProfile);
        _assets->Release(slot->skeleton);
        const u32 slotIndex = handle.GetSlot();
        *slot = {};
        slot->generation = handle.GetGeneration() + 1u;
        if (slot->generation == 0)
            slot->generation = 1;
        _freeSlots.push_back(slotIndex);
        return true;
    }

    bool ControllerPosePlayer::QueueAsset(AnimationDocumentType type, FileFormat::AssetID assetID, std::span<const u8> payload, std::string& error)
    {
        AnimationAssetDocument validation;
        if (assetID == FileFormat::INVALID_ASSET_ID || !validation.Open(type, payload, error)) return false;
        if (_pendingAsset) { error = "An asset publication is already queued"; return false; }
        for (const auto& slot : _slots)
            if (slot.live && slot.pendingGraph) { error = "Wait for the queued graph publication"; return false; }
        _pendingAsset = std::make_unique<AssetPublication>(AssetPublication{type, assetID, {payload.begin(), payload.end()}});
        _assetPublicationStatus = "Publication queued";
        return true;
    }

    void ControllerPosePlayer::PublishPendingAsset()
    {
        ZoneScopedN("Animation asset publication");
        if (!_pendingAsset) return;
        auto publication = std::move(_pendingAsset);
        // Registry snapshots share immutable bytes. Only the editor transaction copies tables.
        auto previous = *_assets;
        switch (publication->type)
        {
            case AnimationDocumentType::Skeleton: _assets->Release(_assets->PublishSkeleton(publication->assetID, publication->payload)); break;
            case AnimationDocumentType::RigFamily: _assets->Release(_assets->PublishRigFamily(publication->assetID, publication->payload)); break;
            case AnimationDocumentType::Clip: _assets->Release(_assets->PublishAnimationClip(publication->assetID, publication->payload)); break;
            case AnimationDocumentType::Set: _assets->Release(_assets->PublishAnimationSet(publication->assetID, publication->payload)); break;
            case AnimationDocumentType::Mask: _assets->Release(_assets->PublishBoneMask(publication->assetID, publication->payload)); break;
            case AnimationDocumentType::IK: _assets->Release(_assets->PublishIKRig(publication->assetID, publication->payload)); break;
        }
        std::vector<ControllerPlaybackHandle> live;
        for (u32 i = 0; i < _slots.size(); ++i)
            if (_slots[i].live) live.push_back(ControllerPlaybackHandle::Create(i, _slots[i].generation));
        std::string error;
        bool valid = true;
        for (const auto handle : live)
        {
            const auto graph = GetSlot(handle)->graph;
            if (!QueueGraph(handle, graph, error)) { valid = false; break; }
            auto* slot = GetSlot(handle);
            const auto* candidate = GetSlot(slot->pendingResources);
            const auto* oldSkeleton = _assets->GetRetained(slot->skeleton);
            const auto* newSkeleton = _assets->GetRetained(candidate->skeleton);
            valid = oldSkeleton && newSkeleton && oldSkeleton->joints.size() == newSkeleton->joints.size();
            for (u32 i = 0; valid && i < oldSkeleton->joints.size(); ++i)
                valid = oldSkeleton->joints[i].semanticID == newSkeleton->joints[i].semanticID && oldSkeleton->joints[i].parentJointIndex == newSkeleton->joints[i].parentJointIndex;
            if (!valid) { error = "Live skeleton topology changed; rebind the model before applying"; break; }
            slot->pendingResourcesCompatible = publication->type != AnimationDocumentType::Skeleton && publication->type != AnimationDocumentType::RigFamily;
            if (slot->clipDomains.size() != candidate->clipDomains.size()) slot->pendingResourcesCompatible = false;
            for (const auto& domain : slot->clipDomains)
            {
                const auto next = std::find_if(candidate->clipDomains.begin(), candidate->clipDomains.end(), [&](const auto& value) { return value.clipAssetID == domain.clipAssetID; });
                if (next == candidate->clipDomains.end() || next->domainType != domain.domainType || next->domainAssetID != domain.domainAssetID || next->durationMicroseconds != domain.durationMicroseconds) slot->pendingResourcesCompatible = false;
            }
        }
        if (!valid)
        {
            for (const auto handle : live)
            {
                auto* slot = GetSlot(handle);
                if (slot->pendingResources) Destroy(slot->pendingResources);
                slot->pendingResources = {};
                slot->pendingGraph.reset();
            }
            *_assets = std::move(previous);
            _assetPublicationStatus = "Rejected; running assets unchanged: " + error;
            return;
        }
        const bool reset = std::any_of(live.begin(), live.end(), [&](auto handle) { return !GetSlot(handle)->pendingResourcesCompatible; });
        _assetPublicationStatus = reset ? "Published; pose-domain/timing change resets state and cancels actions" : "Published; compatible controller state preserved";
    }

    void ControllerPosePlayer::Advance(f64 deltaSeconds, bool applyTimeScale)
    {
        ZoneScopedN("Animation controller advance and publication");
        PublishPendingAsset();
        // Publication happens before controller evaluation and render-frame pose requests.
        for (Slot& slot : _slots)
        {
            if (!slot.live || !slot.pendingGraph)
                continue;
            if (_controllers.ReplaceGraph(slot.animator, FileFormat::INVALID_ASSET_ID, slot.pendingGraph, &slot.migration, slot.pendingResourcesCompatible))
            {
                const auto candidateHandle = slot.pendingResources;
                Slot* candidate = GetSlot(candidateHandle);
                if (candidate)
                {
                    std::swap(slot.skeleton, candidate->skeleton);
                    std::swap(slot.animationSet, candidate->animationSet);
                    _poseSources->RebindSkeleton(slot.poseSource, slot.skeleton);
                    std::swap(slot.clips, candidate->clips);
                    std::swap(slot.masks, candidate->masks);
                    std::swap(slot.ikRigs, candidate->ikRigs);
                    std::swap(slot.clipDomains, candidate->clipDomains);
                    std::swap(slot.targetBinding, candidate->targetBinding);
                    std::swap(slot.bridge, candidate->bridge);
                    std::swap(slot.targetFamily, candidate->targetFamily);
                    std::swap(slot.retargetProfile, candidate->retargetProfile);
                    slot.hasBridge = candidate->hasBridge;
                    slot.familyJointCount = candidate->familyJointCount;
                    slot.correctRetargetChains = candidate->correctRetargetChains;
                    const auto* set = _assets->GetRetained(slot.animationSet);
                    _controllers.BindAnimationData(slot.animator, *set, slot.clipDomains);
                    vec3 expansion = {};
                    for (const auto& rig : slot.ikRigs)
                        expansion = glm::max(expansion, _assets->GetRetained(rig.handle)->root.conservativeExtentExpansion);
                    _poseSources->SetConservativeBoundsExpansion(slot.poseSource, expansion);
                    slot.pendingResources = {};
                    Destroy(candidateHandle);
                }
                slot.graph = std::move(slot.pendingGraph);
                slot.lastRecipeHash = 0;
                _poseSources->Invalidate(slot.poseSource);
            }
        }
        _controllers.Advance(deltaSeconds, applyTimeScale);
        for (Slot& slot : _slots)
        {
            if (!slot.live)
                continue;
            const AnimationLoading::AnimationSetAssetView* set = _assets->GetRetained(slot.animationSet);
            if (!set)
                continue;
            FileFormat::AssetID clipAssetID = FileFormat::INVALID_ASSET_ID;
            u64 timeMicroseconds = 0;
            bool supportsTightBounds = false;
            if (_controllers.GetBoundsMetadata(slot.animator, *set, slot.clipDomains, clipAssetID, timeMicroseconds, supportsTightBounds))
                _poseSources->SetBoundsMetadata(slot.poseSource, clipAssetID, timeMicroseconds, supportsTightBounds && slot.familyJointCount == 0);
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
        const bool succeeded = EvaluateRequestedPose(slot);
        if (!succeeded)
            _poseSources->Invalidate(poseSource);
        return succeeded;
    }

    bool ControllerPosePlayer::EvaluateRequestedPose(Slot& slot, PoseRecipe* diagnosticRecipe)
    {
        ZoneScopedN("Animation recipe evaluation");
        const AnimationLoading::SkeletonAssetView* skeleton = _assets->GetRetained(slot.skeleton);
        const AnimationLoading::AnimationSetAssetView* animationSet = _assets->GetRetained(slot.animationSet);
        if (!skeleton || !animationSet || (slot.targetFamily && !_assets->GetRetained(slot.targetFamily)) || (slot.retargetProfile && !_assets->GetRetained(slot.retargetProfile)))
        {
            ++_stats.failures;
            return false;
        }
        const PoseRecipe recipe = _controllers.RequestPose(slot.animator, *animationSet, slot.clipDomains, slot.continuityPose);
        if (diagnosticRecipe) *diagnosticRecipe = recipe;
        std::array<std::pair<FileFormat::AssetID, FileFormat::Animation::PoseDomainType>, 10> activeDomains;
        u32 activeCount = 0;
        bool validDomains = true;
        auto addDomain = [&](FileFormat::AssetID clipID)
        {
            const auto* clip = FindClip(slot, clipID);
            if (!clip) { validDomains = false; return; }
            const auto record = std::find_if(slot.clips.begin(), slot.clips.end(), [clipID](const auto& value) { return value.assetID == clipID; });
            if (record != slot.clips.end() && record->domain && (!_assets->GetRetained(record->domain->family) || (record->domain->skeleton && !_assets->GetRetained(record->domain->skeleton))))
                validDomains = false;
            const auto key = std::pair(clip->root.sourcePoseDomainAssetID, clip->root.sourcePoseDomainType);
            if (std::find(activeDomains.begin(), activeDomains.begin() + activeCount, key) == activeDomains.begin() + activeCount)
                activeDomains[activeCount++] = key;
        };
        for (u32 index = 0; index < recipe.sampleCount; ++index) addDomain(recipe.samples[index].clipAssetID);
        for (u32 index = 0; index < recipe.layerCount; ++index)
            if (!recipe.layers[index].composeInSourceDomain) addDomain(recipe.layers[index].sample.clipAssetID);
        _stats.lastActiveDomains = activeCount;
        _stats.peakActiveDomains = std::max(_stats.peakActiveDomains, activeCount);
        if (!validDomains)
        {
            ++_stats.failures;
            return false;
        }
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
            recipeHash = XXHash64::hash(&layer.composeInSourceDomain, sizeof(layer.composeInSourceDomain), recipeHash);
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
        {
            ++_stats.cachedPoseHits;
            return true;
        }

        std::vector<FileFormat::Animation::LocalTransform> composed = slot.continuityPose;
        bool hasComposedPose = recipe.usesSnapshot;
        f32 accumulatedWeight = recipe.usesSnapshot ? std::max(0.0f, 1.0f - recipe.transitionWeight) : 0.0f;
        std::array<bool, 6> consumed = {};
        std::array<bool, 4> consumedLayers = {};
        for (u32 sampleIndex = 0; sampleIndex < recipe.sampleCount; ++sampleIndex)
        {
            if (consumed[sampleIndex])
                continue;
            const PoseRecipeSample& sample = recipe.samples[sampleIndex];
            const AnimationLoading::AnimationClipAssetView* clip = FindClip(slot, sample.clipAssetID);
            if (!clip)
                return false;
            std::array<PoseRecipeSample, 6> group;
            u32 groupCount = 0;
            f32 groupWeight = 0.0f;
            for (u32 candidate = sampleIndex; candidate < recipe.sampleCount; ++candidate)
            {
                const auto* member = FindClip(slot, recipe.samples[candidate].clipAssetID);
                if (!consumed[candidate] && member && member->root.sourcePoseDomainType == clip->root.sourcePoseDomainType && member->root.sourcePoseDomainAssetID == clip->root.sourcePoseDomainAssetID)
                {
                    consumed[candidate] = true;
                    group[groupCount++] = recipe.samples[candidate];
                    groupWeight += recipe.samples[candidate].weight;
                }
            }
            std::array<PoseRecipeLayer, 4> sourceLayers;
            u32 sourceLayerCount = 0;
            for (u32 index = 0; index < recipe.layerCount; ++index)
            {
                const auto& layer = recipe.layers[index];
                const auto* layerClip = FindClip(slot, layer.sample.clipAssetID);
                if (!consumedLayers[index] && layer.composeInSourceDomain && layerClip && layerClip->root.sourcePoseDomainType == clip->root.sourcePoseDomainType && layerClip->root.sourcePoseDomainAssetID == clip->root.sourcePoseDomainAssetID)
                {
                    sourceLayers[sourceLayerCount++] = layer;
                    consumedLayers[index] = true;
                }
            }
            if (!SampleTargetPose(slot, std::span<const PoseRecipeSample>(group.data(), groupCount), *skeleton, slot.samplePose, std::span<const PoseRecipeLayer>(sourceLayers.data(), sourceLayerCount)))
            {
                ++_stats.failures;
                return false;
            }
            if (!hasComposedPose)
            {
                composed = slot.samplePose;
                accumulatedWeight = groupWeight;
                hasComposedPose = true;
            }
            else
            {
                const f32 denominator = accumulatedWeight + groupWeight;
                const f32 blendWeight = denominator > 0.0f ? groupWeight / denominator : 0.0f;
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
            // Source-local layers have no contribution while their base domain is inactive.
            if (recipe.layers[layerIndex].composeInSourceDomain && !consumedLayers[layerIndex])
                continue;
            if (consumedLayers[layerIndex])
            {
                const auto& layer = recipe.layers[layerIndex];
                const auto* mask = FindMask(slot, layer.maskAssetID);
                for (u32 index = 0; index < debugJointWeights.size(); ++index)
                    debugJointWeights[index] = std::max(debugJointWeights[index], layer.weight * (mask ? mask->weights[index] : 1.0f));
                continue;
            }
            const PoseRecipeLayer& layer = recipe.layers[layerIndex];
            const AnimationLoading::AnimationClipAssetView* clip = FindClip(slot, layer.sample.clipAssetID);
            const MaskRecord* mask = layer.maskAssetID == FileFormat::INVALID_ASSET_ID ? nullptr : FindMask(slot, layer.maskAssetID);
            if (!clip || (layer.maskAssetID != FileFormat::INVALID_ASSET_ID && !mask) || !SampleTargetPose(slot, std::span<const PoseRecipeSample>(&layer.sample, 1), *skeleton, slot.samplePose))
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
        slot.continuityPose = composed;
        for (u32 operationIndex = 0; operationIndex < recipe.ikOperationCount; ++operationIndex)
        {
            ZoneNamedN(ikZone, "Animation target IK and limits", true);
            const PoseRecipeIKOperation& operation = recipe.ikOperations[operationIndex];
            const IKRigRecord* rigRecord = FindIKRig(slot, operation.rigAssetID);
            const AnimationLoading::IKRigAssetView* rig = rigRecord ? _assets->GetRetained(rigRecord->handle) : nullptr;
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
                if (chain.poleJointIndex < skeleton->joints.size())
                    polePosition = vec3(slot.ikWorkspace.modelTransforms[chain.poleJointIndex][3]);
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
            {
                slot.ikWorkspace.current = false;
                if (!UpdateModelTransforms(skeleton->joints, composed, 0, slot.ikWorkspace.modelTransforms))
                    return false;
                slot.ikWorkspace.current = true;
                if (solver->type == FileFormat::Animation::IKSolverType::TwoBone || solver->type == FileFormat::Animation::IKSolverType::IterativeChain)
                    solve.finalError = glm::length(vec3(mat4x4(slot.ikWorkspace.modelTransforms[chain.effectorJointIndex]) * vec4(effectorLocalPoint, 1.0f)) - goalModel);
                else
                {
                    const auto& transform = slot.ikWorkspace.modelTransforms[chainJoints.front()];
                    const vec3 axis = glm::normalize(vec3(mat4x4(transform) * vec4(chain.preferredBendDirection, 0.0f)));
                    const vec3 direction = goalModel - vec3(transform[3]);
                    solve.finalError = glm::dot(direction, direction) > 1.0e-8f ? glm::acos(std::clamp(glm::dot(axis, glm::normalize(direction)), -1.0f, 1.0f)) : 0.0f;
                }
                solve.status = IKSolveStatus::Clamped;
            }
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

    bool ControllerPosePlayer::SampleTargetPose(Slot& slot, std::span<const PoseRecipeSample> samples, const AnimationLoading::SkeletonAssetView& target, std::vector<FileFormat::Animation::LocalTransform>& output, std::span<const PoseRecipeLayer> sourceLayers)
    {
        ZoneScopedN("Animation sampling blend and retarget");
        if (samples.empty())
            return false;
        const PoseRecipeSample& sample = samples.front();
        const auto record = std::find_if(slot.clips.begin(), slot.clips.end(), [&sample](const ClipRecord& value) { return value.assetID == sample.clipAssetID; });
        if (record == slot.clips.end())
            return false;
        const auto* clip = _assets->GetRetained(record->handle);
        if (!clip)
            return false;
        const bool additive = (clip->root.flags & FileFormat::Animation::AnimationClipFlags_Additive) != 0;
        output.resize(target.joints.size());
        const bool native = !record->domain || (record->domain->type == FileFormat::Animation::PoseDomainType::Skeleton && record->domain->assetID == _assets->GetRetainedAssetID(slot.skeleton));
        if (!native && (!_assets->GetRetained(record->domain->family) || (record->domain->skeleton && !_assets->GetRetained(record->domain->skeleton))))
            return false;
        AnimationLoading::SkeletonAssetView source;
        source.joints = native ? target.joints : std::span<const FileFormat::Animation::SkeletonJoint>(record->domain->samplingJoints);
        std::vector<FileFormat::Animation::LocalTransform> sourcePose(source.joints.size());
        std::vector<FileFormat::Animation::LocalTransform> sampled(source.joints.size());
        f32 weight = 0.0f;
        bool initialized = false;
        for (const auto& member : samples)
        {
            const auto* memberClip = FindClip(slot, member.clipAssetID);
            if (!memberClip || memberClip->root.sourcePoseDomainType != clip->root.sourcePoseDomainType || memberClip->root.sourcePoseDomainAssetID != clip->root.sourcePoseDomainAssetID || ((memberClip->root.flags & FileFormat::Animation::AnimationClipFlags_Additive) != 0) != additive || SampleClipLocalPose(*memberClip, source, static_cast<f64>(member.timeMicroseconds) / 1000000.0, (memberClip->root.flags & FileFormat::Animation::AnimationClipFlags_Looping) != 0, sampled) != ClipSampleError::None)
                return false;
            const f32 total = weight + member.weight;
            if (!initialized)
                sourcePose = sampled;
            else if (!BlendLocalPoses(sourcePose, sampled, total > 0 ? member.weight / total : 0, sourcePose))
                return false;
            initialized = true;
            weight = total;
        }
        for (const auto& layer : sourceLayers)
        {
            const auto* layerClip = FindClip(slot, layer.sample.clipAssetID);
            const auto* mask = layer.maskAssetID == FileFormat::INVALID_ASSET_ID ? nullptr : FindMask(slot, layer.maskAssetID);
            if (!layerClip || (layer.maskAssetID != FileFormat::INVALID_ASSET_ID && !mask) || SampleClipLocalPose(*layerClip, source, static_cast<f64>(layer.sample.timeMicroseconds) / 1000000.0, (layerClip->root.flags & FileFormat::Animation::AnimationClipFlags_Looping) != 0, sampled) != ClipSampleError::None)
                return false;
            std::vector<f32> sourceWeights;
            if (mask)
            {
                if (native)
                    sourceWeights = mask->weights;
                else
                {
                    std::vector<f32> canonicalWeights(slot.targetBinding.GetFamilyJointCount(), 0.0f);
                    for (const auto& binding : target.familyBindings)
                        canonicalWeights[binding.familyJointIndex] = mask->weights[binding.skeletonJointIndex];
                    if (slot.hasBridge)
                    {
                        const auto* profile = _assets->GetRetained(slot.retargetProfile);
                        if (!profile)
                            return false;
                        std::vector<f32> sourceCanonicalWeights(slot.familyJointCount, 0.0f);
                        for (const auto& mapping : profile->jointMappings)
                            sourceCanonicalWeights[mapping.sourceJointIndex] = std::max(sourceCanonicalWeights[mapping.sourceJointIndex], canonicalWeights[mapping.targetJointIndex]);
                        canonicalWeights = std::move(sourceCanonicalWeights);
                    }
                    sourceWeights.assign(source.joints.size(), 0.0f);
                    if (record->domain->type == FileFormat::Animation::PoseDomainType::RigFamily)
                        sourceWeights = std::move(canonicalWeights);
                    else
                        for (const auto& binding : _assets->GetRetained(record->domain->skeleton)->familyBindings)
                            sourceWeights[binding.skeletonJointIndex] = canonicalWeights[binding.familyJointIndex];
                }
            }
            if (layer.type == AnimationLayerType::Additive)
            {
                if ((layerClip->root.flags & FileFormat::Animation::AnimationClipFlags_Additive) == 0 || !ApplyAdditiveLocalPose(sourcePose, sampled, layer.weight, sourceWeights, sourcePose))
                    return false;
            }
            else if (!BlendOverrideLocalPoses(sourcePose, sampled, layer.weight, sourceWeights, sourcePose))
                return false;
            ++_stats.layerOperations;
        }
        if (native)
        {
            output = std::move(sourcePose);
            return true;
        }
        const SourceDomain& domain = *record->domain;
        ZoneNamedN(retargetZone, "Animation family conversion and chain correction", true);
        std::vector<FileFormat::Animation::LocalTransform> canonical(slot.familyJointCount);
        if (domain.type == FileFormat::Animation::PoseDomainType::RigFamily)
            canonical = std::move(sourcePose);
        else if (!domain.binding.ToFamily(sourcePose, canonical, additive))
            return false;
        if (slot.hasBridge)
        {
            std::vector<FileFormat::Animation::LocalTransform> bridged;
            if (!slot.bridge.ConvertPose(canonical, bridged, additive))
                return false;
            canonical = std::move(bridged);
        }
        if (!slot.targetBinding.FromFamily(canonical, output, additive, slot.correctRetargetChains))
            return false;
        ++_stats.familyConversions;
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
        return found != slot.clips.end() ? _assets->GetRetained(found->handle) : nullptr;
    }
}
