#include "AnimationAssetReader.h"

#include <cstdint>
#include <cstring>
#include <limits>

namespace
{
    using AssetLoading::Diagnostic;
    using AssetLoading::DiagnosticCode;
    using AssetLoading::DiagnosticStage;

    template <typename Result>
    Result Fail(DiagnosticCode code, std::string_view field, u32 index = Diagnostic::NO_INDEX, u64 observed = 0, u64 expected = 0)
    {
        Result result;
        result.diagnostic = {code, field, index, observed, expected};
        result.diagnostic.stage = DiagnosticStage::Read;
        return result;
    }

    template <typename Root, typename Result>
    bool ReadRoot(std::span<const u8> payload, FileHeader::Type expectedType, Result& result)
    {
        if (payload.size() < sizeof(Root))
        {
            result = Fail<Result>(DiagnosticCode::PayloadTooSmall, "root", Diagnostic::NO_INDEX, payload.size(), sizeof(Root));
            return false;
        }

        std::memcpy(&result.view.root, payload.data(), sizeof(Root));
        if (result.view.root.header.type != expectedType)
        {
            result = Fail<Result>(DiagnosticCode::InvalidHeader, "root.header.type", Diagnostic::NO_INDEX, static_cast<u64>(result.view.root.header.type), static_cast<u64>(expectedType));
            return false;
        }
        if (result.view.root.header.version != FileFormat::Animation::DEVELOPMENT_VERSION)
        {
            result = Fail<Result>(DiagnosticCode::UnsupportedVersion, "root.header.version", Diagnostic::NO_INDEX, result.view.root.header.version, FileFormat::Animation::DEVELOPMENT_VERSION);
            return false;
        }
        return true;
    }

    template <typename T>
    bool SetSection(std::span<const u8> payload, size_t rootSize, u32 offset, u32 count, std::span<const T>& output)
    {
        if (count == 0)
            return offset == 0;
        if (offset < rootSize || offset % 16 != 0 || count > std::numeric_limits<size_t>::max() / sizeof(T))
            return false;
        const size_t byteCount = static_cast<size_t>(count) * sizeof(T);
        if (offset > payload.size() || byteCount > payload.size() - offset || reinterpret_cast<uintptr_t>(payload.data() + offset) % alignof(T) != 0)
            return false;
        output = {reinterpret_cast<const T*>(payload.data() + offset), count};
        return true;
    }

    template <typename Result, typename T>
    bool ReadSection(Result& result, std::span<const u8> payload, size_t rootSize, u32 offset, u32 count, std::span<const T>& output, std::string_view field)
    {
        if (SetSection(payload, rootSize, offset, count, output))
            return true;
        result = Fail<Result>(DiagnosticCode::InvalidRootSection, field, Diagnostic::NO_INDEX, offset, count);
        return false;
    }
} // namespace

namespace AnimationLoading
{
    RigFamilyAssetReadResult AnimationAssetReader::ReadRigFamily(std::span<const u8> payload)
    {
        RigFamilyAssetReadResult result;
        if (!ReadRoot<Animation::RigFamilyAsset>(payload, Animation::RIG_FAMILY_FILE_TYPE, result)) return result;
        const auto& root = result.view.root;
        if (!ReadSection(result, payload, sizeof(root), root.jointsOffset, root.numJoints, result.view.joints, "joints") ||
            !ReadSection(result, payload, sizeof(root), root.chainsOffset, root.numChains, result.view.chains, "chains") ||
            !ReadSection(result, payload, sizeof(root), root.chainJointIndicesOffset, root.numChainJointIndices, result.view.chainJointIndices, "chainJointIndices")) return result;
        return result;
    }

    SkeletonAssetReadResult AnimationAssetReader::ReadSkeleton(std::span<const u8> payload)
    {
        SkeletonAssetReadResult result;
        if (!ReadRoot<Animation::SkeletonAsset>(payload, Animation::SKELETON_FILE_TYPE, result)) return result;
        const auto& root = result.view.root;
        if (!ReadSection(result, payload, sizeof(root), root.jointsOffset, root.numJoints, result.view.joints, "joints") ||
            !ReadSection(result, payload, sizeof(root), root.familyBindingsOffset, root.numFamilyBindings, result.view.familyBindings, "familyBindings") ||
            !ReadSection(result, payload, sizeof(root), root.propagationRulesOffset, root.numPropagationRules, result.view.propagationRules, "propagationRules") ||
            !ReadSection(result, payload, sizeof(root), root.hierarchyDepthRangesOffset, root.numHierarchyDepthRanges, result.view.hierarchyDepthRanges, "hierarchyDepthRanges") ||
            !ReadSection(result, payload, sizeof(root), root.hierarchyDepthJointIndicesOffset, root.numHierarchyDepthJointIndices, result.view.hierarchyDepthJointIndices, "hierarchyDepthJointIndices") ||
            !ReadSection(result, payload, sizeof(root), root.attachmentsOffset, root.numAttachments, result.view.attachments, "attachments")) return result;
        return result;
    }

    AnimationClipAssetReadResult AnimationAssetReader::ReadAnimationClip(std::span<const u8> payload)
    {
        AnimationClipAssetReadResult result;
        if (!ReadRoot<Animation::AnimationClipAsset>(payload, Animation::CLIP_FILE_TYPE, result)) return result;
        const auto& root = result.view.root;
        if (!ReadSection(result, payload, sizeof(root), root.tracksOffset, root.numTracks, result.view.tracks, "tracks") ||
            !ReadSection(result, payload, sizeof(root), root.translationSamplesOffset, root.numTranslationSamples, result.view.translationSamples, "translationSamples") ||
            !ReadSection(result, payload, sizeof(root), root.rotationSamplesOffset, root.numRotationSamples, result.view.rotationSamples, "rotationSamples") ||
            !ReadSection(result, payload, sizeof(root), root.scaleSamplesOffset, root.numScaleSamples, result.view.scaleSamples, "scaleSamples") ||
            !ReadSection(result, payload, sizeof(root), root.synchronizationMarkersOffset, root.numSynchronizationMarkers, result.view.synchronizationMarkers, "synchronizationMarkers") ||
            !ReadSection(result, payload, sizeof(root), root.actionWindowsOffset, root.numActionWindows, result.view.actionWindows, "actionWindows") ||
            !ReadSection(result, payload, sizeof(root), root.eventsOffset, root.numEvents, result.view.events, "events") ||
            !ReadSection(result, payload, sizeof(root), root.eventPayloadBytesOffset, root.numEventPayloadBytes, result.view.eventPayloadBytes, "eventPayloadBytes")) return result;
        return result;
    }

    AnimationSetAssetReadResult AnimationAssetReader::ReadAnimationSet(std::span<const u8> payload)
    {
        AnimationSetAssetReadResult result;
        if (!ReadRoot<Animation::AnimationSetAsset>(payload, Animation::ANIMATION_SET_FILE_TYPE, result)) return result;
        const auto& root = result.view.root;
        if (!ReadSection(result, payload, sizeof(root), root.slotsOffset, root.numSlots, result.view.slots, "slots") ||
            !ReadSection(result, payload, sizeof(root), root.variantsOffset, root.numVariants, result.view.variants, "variants")) return result;
        return result;
    }

    AnimationGraphAssetReadResult AnimationAssetReader::ReadAnimationGraph(std::span<const u8> payload)
    {
        AnimationGraphAssetReadResult result;
        if (!ReadRoot<Animation::AnimationGraphAsset>(payload, Animation::GRAPH_FILE_TYPE, result)) return result;
        const auto& root = result.view.root;
        if (!ReadSection(result, payload, sizeof(root), root.defaultParameterDataOffset, root.defaultParameterDataSize, result.view.defaultParameterData, "defaultParameterData") ||
            !ReadSection(result, payload, sizeof(root), root.parametersOffset, root.numParameters, result.view.parameters, "parameters") ||
            !ReadSection(result, payload, sizeof(root), root.commandsOffset, root.numCommands, result.view.commands, "commands") ||
            !ReadSection(result, payload, sizeof(root), root.actionSlotsOffset, root.numActionSlots, result.view.actionSlots, "actionSlots") ||
            !ReadSection(result, payload, sizeof(root), root.stateMigrationEntriesOffset, root.numStateMigrationEntries, result.view.stateMigrationEntries, "stateMigrationEntries") ||
            !ReadSection(result, payload, sizeof(root), root.controllerProgramOffset, root.controllerProgramSize, result.view.controllerProgram, "controllerProgram") ||
            !ReadSection(result, payload, sizeof(root), root.poseProgramOffset, root.poseProgramSize, result.view.poseProgram, "poseProgram")) return result;
        return result;
    }

    BoneMaskAssetReadResult AnimationAssetReader::ReadBoneMask(std::span<const u8> payload)
    {
        BoneMaskAssetReadResult result;
        if (!ReadRoot<Animation::BoneMaskAsset>(payload, Animation::BONE_MASK_FILE_TYPE, result)) return result;
        const auto& root = result.view.root;
        if (!ReadSection(result, payload, sizeof(root), root.jointWeightsOffset, root.numJointWeights, result.view.jointWeights, "jointWeights")) return result;
        return result;
    }

    IKRigAssetReadResult AnimationAssetReader::ReadIKRig(std::span<const u8> payload)
    {
        IKRigAssetReadResult result;
        if (!ReadRoot<Animation::IKRigAsset>(payload, Animation::IK_RIG_FILE_TYPE, result)) return result;
        const auto& root = result.view.root;
        if (!ReadSection(result, payload, sizeof(root), root.chainsOffset, root.numChains, result.view.chains, "chains") ||
            !ReadSection(result, payload, sizeof(root), root.chainJointIndicesOffset, root.numChainJointIndices, result.view.chainJointIndices, "chainJointIndices") ||
            !ReadSection(result, payload, sizeof(root), root.jointLimitsOffset, root.numJointLimits, result.view.jointLimits, "jointLimits") ||
            !ReadSection(result, payload, sizeof(root), root.solversOffset, root.numSolvers, result.view.solvers, "solvers")) return result;
        return result;
    }

    RetargetProfileAssetReadResult AnimationAssetReader::ReadRetargetProfile(std::span<const u8> payload)
    {
        RetargetProfileAssetReadResult result;
        if (!ReadRoot<Animation::RetargetProfileAsset>(payload, Animation::RETARGET_PROFILE_FILE_TYPE, result)) return result;
        const auto& root = result.view.root;
        if (!ReadSection(result, payload, sizeof(root), root.jointMappingsOffset, root.numJointMappings, result.view.jointMappings, "jointMappings")) return result;
        return result;
    }

    AnimationBoundsAssetReadResult AnimationAssetReader::ReadAnimationBounds(std::span<const u8> payload)
    {
        AnimationBoundsAssetReadResult result;
        if (!ReadRoot<Animation::AnimationBoundsAsset>(payload, Animation::ANIMATION_BOUNDS_FILE_TYPE, result)) return result;
        const auto& root = result.view.root;
        if (!ReadSection(result, payload, sizeof(root), root.clipsOffset, root.numClips, result.view.clips, "clips") ||
            !ReadSection(result, payload, sizeof(root), root.segmentsOffset, root.numSegments, result.view.segments, "segments")) return result;
        return result;
    }
} // namespace AnimationLoading
