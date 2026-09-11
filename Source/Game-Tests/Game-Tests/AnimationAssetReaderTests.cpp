#include "Game-Lib/Rendering/Animation/Asset/AnimationAssetRegistry.h"
#include "Game-Lib/Rendering/Animation/Asset/AnimationAssetDocument.h"
#include "Game-Lib/Rendering/Animation/Asset/AnimationAssetValidator.h"
#include "Game-Lib/Rendering/Animation/Asset/AnimationBoundsSelector.h"

#include <Base/Memory/Bytebuffer.h>

#include <catch2/catch2.hpp>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <vector>

namespace
{
    namespace Format = FileFormat::Animation;

    std::vector<u8> SaveSkeleton(u64 childSemanticID = 2)
    {
        Format::SkeletonAsset asset;
        Format::SkeletonData data;
        Format::SkeletonJoint root;
        root.semanticID = 1;
        root.flags = Format::SkeletonJointFlags_Helper;
        data.joints.push_back(root);
        Format::SkeletonJoint child;
        child.semanticID = childSemanticID;
        child.parentJointIndex = 0;
        child.hierarchyDepth = 1;
        child.flags = Format::SkeletonJointFlags_Deformation;
        data.joints.push_back(child);
        data.hierarchyDepthRanges = {{0, 1}, {1, 1}};
        data.hierarchyDepthJointIndices = {0, 1};
        std::shared_ptr<Bytebuffer> buffer = Bytebuffer::BorrowRuntime(asset.GetSerializedSize(data));
        REQUIRE(asset.Save(buffer, data));
        return {buffer->GetDataPointer(), buffer->GetDataPointer() + buffer->writtenData};
    }

    std::vector<u8> SaveClip()
    {
        Format::AnimationClipAsset asset;
        asset.sourcePoseDomainAssetID = 0xAABBCCDDu;
        asset.sourcePoseDomainType = Format::PoseDomainType::Skeleton;
        asset.flags = Format::AnimationClipFlags_Looping;
        asset.sampleRateHz = 2;
        asset.sampleCount = 3;
        asset.durationMicroseconds = 1'000'000;
        Format::AnimationClipData data;
        Format::AnimationTrack track;
        track.sourceJointIndex = 0;
        track.flags = Format::AnimationTrackFlags_HasTranslationSamples | Format::AnimationTrackFlags_HasRotationSamples | Format::AnimationTrackFlags_HasScaleSamples;
        track.numTranslations = 3;
        track.numRotations = 3;
        track.numScales = 3;
        data.tracks.push_back(track);
        data.translationSamples = {vec3(0.0f), vec3(0.25f, 0.0f, 0.0f), vec3(1.0f, 0.0f, 0.0f)};
        data.rotationSamples = {quat(1.0f, 0.0f, 0.0f, 0.0f), quat(0.9238795f, 0.0f, 0.3826834f, 0.0f), quat(1.0f, 0.0f, 0.0f, 0.0f)};
        data.scaleSamples = {vec3(1.0f), vec3(1.1f), vec3(1.0f)};
        data.synchronizationMarkers = {{11, 0, 0}, {12, 500'000, 0}};
        data.actionWindows = {{21, 250'000, 750'000}};
        data.events = {{500'000, 31, 32, 0, 0, 2}};
        data.eventPayloadBytes = {0xAB, 0xCD};
        std::shared_ptr<Bytebuffer> buffer = Bytebuffer::BorrowRuntime(asset.GetSerializedSize(data));
        REQUIRE(asset.Save(buffer, data));
        return {buffer->GetDataPointer(), buffer->GetDataPointer() + buffer->writtenData};
    }

    std::vector<u8> SaveBounds(bool withGlobalFallback = false)
    {
        Format::AnimationBoundsAsset asset;
        asset.modelAssetID = 10;
        asset.skeletonAssetID = 20;
        asset.flags = withGlobalFallback ? Format::AnimationBoundsAssetFlags_HasGlobalFallback : 0;
        Format::AnimationBoundsData data;
        if (withGlobalFallback)
        {
            Format::ClipBounds global;
            global.fallbackBounds.center = vec3(0.0f);
            global.fallbackBounds.extents = vec3(8.0f);
            global.fallbackBounds.sphereRadius = 14.0f;
            data.clips.push_back(global);
        }
        Format::ClipBounds clip;
        clip.animationClipAssetID = 30;
        clip.segmentOffset = 0;
        clip.numSegments = 2;
        clip.fallbackBounds.center = vec3(0.0f);
        clip.fallbackBounds.extents = vec3(3.0f);
        clip.fallbackBounds.sphereRadius = 6.0f;
        data.clips.push_back(clip);
        Format::AnimatedBoundsSegment first;
        first.startTimeMicroseconds = 0;
        first.endTimeMicroseconds = 500'000;
        first.bounds.extents = vec3(1.0f);
        first.bounds.sphereRadius = 2.0f;
        data.segments.push_back(first);
        Format::AnimatedBoundsSegment second = first;
        second.startTimeMicroseconds = 500'000;
        second.endTimeMicroseconds = 1'000'000;
        second.bounds.center = vec3(1.0f, 0.0f, 0.0f);
        data.segments.push_back(second);
        std::shared_ptr<Bytebuffer> buffer = Bytebuffer::BorrowRuntime(asset.GetSerializedSize(data));
        REQUIRE(asset.Save(buffer, data));
        return {buffer->GetDataPointer(), buffer->GetDataPointer() + buffer->writtenData};
    }

    template <typename T>
    T& Root(std::vector<u8>& payload)
    {
        return *reinterpret_cast<T*>(payload.data());
    }

    template <typename Asset, typename Data>
    std::vector<u8> SaveEmptyAsset()
    {
        Asset asset;
        Data data;
        std::shared_ptr<Bytebuffer> buffer = Bytebuffer::BorrowRuntime(asset.GetSerializedSize(data));
        REQUIRE(asset.Save(buffer, data));
        return {buffer->GetDataPointer(), buffer->GetDataPointer() + buffer->writtenData};
    }
} // namespace

TEST_CASE("Animation V2 has focused structural readers for every asset root", "[AnimationAsset]")
{
    CHECK(AnimationLoading::AnimationAssetReader::ReadRigFamily(SaveEmptyAsset<Format::RigFamilyAsset, Format::RigFamilyData>()));
    CHECK(AnimationLoading::AnimationAssetReader::ReadSkeleton(SaveEmptyAsset<Format::SkeletonAsset, Format::SkeletonData>()));
    CHECK(AnimationLoading::AnimationAssetReader::ReadAnimationClip(SaveEmptyAsset<Format::AnimationClipAsset, Format::AnimationClipData>()));
    CHECK(AnimationLoading::AnimationAssetReader::ReadAnimationSet(SaveEmptyAsset<Format::AnimationSetAsset, Format::AnimationSetData>()));
    CHECK(AnimationLoading::AnimationAssetReader::ReadAnimationGraph(SaveEmptyAsset<Format::AnimationGraphAsset, Format::AnimationGraphData>()));
    CHECK(AnimationLoading::AnimationAssetReader::ReadBoneMask(SaveEmptyAsset<Format::BoneMaskAsset, Format::BoneMaskData>()));
    CHECK(AnimationLoading::AnimationAssetReader::ReadIKRig(SaveEmptyAsset<Format::IKRigAsset, Format::IKRigData>()));
    CHECK(AnimationLoading::AnimationAssetReader::ReadRetargetProfile(SaveEmptyAsset<Format::RetargetProfileAsset, Format::RetargetProfileData>()));
    CHECK(AnimationLoading::AnimationAssetReader::ReadAnimationBounds(SaveEmptyAsset<Format::AnimationBoundsAsset, Format::AnimationBoundsData>()));
}

TEST_CASE("Camera-facing skeleton joints must be leaves", "[AnimationAsset][CameraFacing]")
{
    auto bytes = SaveSkeleton();
    auto& root = Root<Format::SkeletonAsset>(bytes);
    auto* joints = reinterpret_cast<Format::SkeletonJoint*>(bytes.data() + root.jointsOffset);
    joints[1].flags |= Format::SkeletonJointFlags_CameraFacing;
    REQUIRE(AnimationLoading::AnimationAssetValidator::Validate(AnimationLoading::AnimationAssetReader::ReadSkeleton(bytes)));
    joints[0].flags |= Format::SkeletonJointFlags_CameraFacing;
    CHECK(AnimationLoading::AnimationAssetValidator::Validate(AnimationLoading::AnimationAssetReader::ReadSkeleton(bytes)).diagnostic.code == AssetLoading::DiagnosticCode::InvalidValue);
}

TEST_CASE("Animation V2 readers accept valid Skeleton and Clip assets without creating runtime poses", "[AnimationAsset]")
{
    const std::vector<u8> skeletonPayload = SaveSkeleton();
    const AnimationLoading::SkeletonAssetReadResult skeleton = AnimationLoading::AnimationAssetValidator::Validate(AnimationLoading::AnimationAssetReader::ReadSkeleton(skeletonPayload));
    REQUIRE(skeleton);
    CHECK(skeleton.view.joints.size() == 2);
    CHECK(skeleton.view.hierarchyDepthRanges.size() == 2);
    CHECK(skeleton.view.joints[1].flags == Format::SkeletonJointFlags_Deformation);
    CHECK(skeleton.view.root.rigFamilyAssetID == FileFormat::INVALID_ASSET_ID);

    const std::vector<u8> clipPayload = SaveClip();
    const AnimationLoading::AnimationClipAssetReadResult clip = AnimationLoading::AnimationAssetValidator::Validate(AnimationLoading::AnimationAssetReader::ReadAnimationClip(clipPayload));
    REQUIRE(clip);
    CHECK(clip.view.tracks.size() == 1);
    CHECK(clip.view.root.sampleCount == 3);
    CHECK(clip.view.root.sourcePoseDomainType == Format::PoseDomainType::Skeleton);
    CHECK_FALSE(AnimationLoading::AnimationAssetValidator::ValidateDomain(clip.view, Format::PoseDomainType::Skeleton, clip.view.root.sourcePoseDomainAssetID));
}

TEST_CASE("Animation V2 readers reject malformed roots before exposing spans", "[AnimationAsset]")
{
    std::vector<u8> skeletonPayload = SaveSkeleton();
    skeletonPayload.resize(sizeof(Format::SkeletonAsset) - 1);
    CHECK(AnimationLoading::AnimationAssetReader::ReadSkeleton(skeletonPayload).diagnostic.code == AssetLoading::DiagnosticCode::PayloadTooSmall);

    skeletonPayload = SaveSkeleton();
    Root<Format::SkeletonAsset>(skeletonPayload).jointsOffset += 1;
    CHECK(AnimationLoading::AnimationAssetReader::ReadSkeleton(skeletonPayload).diagnostic.code == AssetLoading::DiagnosticCode::InvalidRootSection);

    skeletonPayload = SaveSkeleton();
    Root<Format::SkeletonAsset>(skeletonPayload).numJoints = std::numeric_limits<u32>::max();
    CHECK(AnimationLoading::AnimationAssetReader::ReadSkeleton(skeletonPayload).diagnostic.code == AssetLoading::DiagnosticCode::InvalidRootSection);

    skeletonPayload = SaveSkeleton();
    Root<Format::SkeletonAsset>(skeletonPayload).header.version++;
    CHECK(AnimationLoading::AnimationAssetReader::ReadSkeleton(skeletonPayload).diagnostic.code == AssetLoading::DiagnosticCode::UnsupportedVersion);
}

TEST_CASE("Animation V2 semantic validation rejects hierarchy sample and domain errors deterministically", "[AnimationAsset]")
{
    std::vector<u8> skeletonPayload = SaveSkeleton();
    Format::SkeletonAsset& skeletonRoot = Root<Format::SkeletonAsset>(skeletonPayload);
    auto* joints = reinterpret_cast<Format::SkeletonJoint*>(skeletonPayload.data() + skeletonRoot.jointsOffset);
    joints[1].parentJointIndex = 1;
    const AnimationLoading::SkeletonAssetReadResult hierarchyFailure = AnimationLoading::AnimationAssetValidator::Validate(AnimationLoading::AnimationAssetReader::ReadSkeleton(skeletonPayload));
    CHECK(hierarchyFailure.diagnostic.code == AssetLoading::DiagnosticCode::InvalidIndex);
    CHECK(hierarchyFailure.diagnostic.stage == AssetLoading::DiagnosticStage::Validate);
    CHECK(hierarchyFailure.diagnostic.field == "joints.parentJointIndex");

    std::vector<u8> clipPayload = SaveClip();
    Format::AnimationClipAsset& clipRoot = Root<Format::AnimationClipAsset>(clipPayload);
    auto* tracks = reinterpret_cast<Format::AnimationTrack*>(clipPayload.data() + clipRoot.tracksOffset);
    tracks[0].numRotations = 2;
    const AnimationLoading::AnimationClipAssetReadResult sampleFailure = AnimationLoading::AnimationAssetValidator::Validate(AnimationLoading::AnimationAssetReader::ReadAnimationClip(clipPayload));
    CHECK(sampleFailure.diagnostic.code == AssetLoading::DiagnosticCode::InvalidRange);
    CHECK(sampleFailure.diagnostic.field == "tracks.rotations");

    const AnimationLoading::AnimationClipAssetReadResult validClip = AnimationLoading::AnimationAssetValidator::Validate(AnimationLoading::AnimationAssetReader::ReadAnimationClip(SaveClip()));
    REQUIRE(validClip);
    const AssetLoading::Diagnostic domainFailure = AnimationLoading::AnimationAssetValidator::ValidateDomain(validClip.view, Format::PoseDomainType::RigFamily, 123);
    CHECK(domainFailure.code == AssetLoading::DiagnosticCode::IncompatibleDomain);
    CHECK(domainFailure.stage == AssetLoading::DiagnosticStage::Compatibility);
}

TEST_CASE("Animation bounds validate and select tight or explicit conservative fallback bounds", "[AnimationAsset][AnimationBounds]")
{
    const std::vector<u8> validPayload = SaveBounds();
    const AnimationLoading::AnimationBoundsAssetReadResult valid = AnimationLoading::AnimationAssetValidator::Validate(AnimationLoading::AnimationAssetReader::ReadAnimationBounds(validPayload));
    REQUIRE(valid);
    REQUIRE(valid.view.clips.size() == 1);
    CHECK(valid.view.clips[0].animationClipAssetID == 30);
    Format::Bounds staticFallback;
    staticFallback.extents = vec3(10.0f);
    staticFallback.sphereRadius = 20.0f;

    AnimationLoading::SelectedAnimationBounds selected = AnimationLoading::SelectAnimationBounds(&valid.view, 10, 20, 30, 250'000, true, staticFallback);
    CHECK(selected.selection == AnimationLoading::AnimationBoundsSelection::Segment);
    CHECK(selected.segmentIndex == 0);
    selected = AnimationLoading::SelectAnimationBounds(&valid.view, 10, 20, 30, 750'000, true, staticFallback);
    CHECK(selected.segmentIndex == 1);
    selected = AnimationLoading::SelectAnimationBounds(&valid.view, 10, 20, 30, 1000000, true, staticFallback);
    CHECK(selected.selection == AnimationLoading::AnimationBoundsSelection::ClipFallback);
    CHECK(selected.bounds.extents == valid.view.clips[0].fallbackBounds.extents);
    selected = AnimationLoading::SelectAnimationBounds(&valid.view, 10, 20, 30, 250'000, false, staticFallback);
    CHECK(selected.selection == AnimationLoading::AnimationBoundsSelection::ClipFallback);
    selected = AnimationLoading::SelectAnimationBounds(&valid.view, 10, 99, 30, 250'000, true, staticFallback);
    CHECK(selected.selection == AnimationLoading::AnimationBoundsSelection::StaticFallbackIncompatibleBinding);
    selected = AnimationLoading::SelectAnimationBounds(&valid.view, 10, 20, 99, 250'000, true, staticFallback);
    CHECK(selected.selection == AnimationLoading::AnimationBoundsSelection::StaticFallbackMissingClip);

    std::vector<u8> corrupt = SaveBounds();
    Format::AnimationBoundsAsset& root = Root<Format::AnimationBoundsAsset>(corrupt);
    auto* segments = reinterpret_cast<Format::AnimatedBoundsSegment*>(corrupt.data() + root.segmentsOffset);
    segments[1].startTimeMicroseconds++;
    CHECK(AnimationLoading::AnimationAssetValidator::Validate(AnimationLoading::AnimationAssetReader::ReadAnimationBounds(corrupt)).diagnostic.code == AssetLoading::DiagnosticCode::InvalidValue);
    corrupt = SaveBounds();
    Format::AnimationBoundsAsset& boundsRoot = Root<Format::AnimationBoundsAsset>(corrupt);
    auto* clips = reinterpret_cast<Format::ClipBounds*>(corrupt.data() + boundsRoot.clipsOffset);
    clips[0].fallbackBounds.extents = vec3(0.5f);
    CHECK(AnimationLoading::AnimationAssetValidator::Validate(AnimationLoading::AnimationAssetReader::ReadAnimationBounds(corrupt)).diagnostic.code == AssetLoading::DiagnosticCode::InvalidValue);
}

TEST_CASE("Game validates the current NBS binding-dependent AnimationBounds fixture", "[AnimationAsset][AnimationBounds][NBS]")
{
    const char* path = std::getenv("NOVUS_NBS_PHASE8_BOUNDS");
    if (!path)
        return;
    std::ifstream input(path, std::ios::binary);
    REQUIRE(input);
    const std::vector<u8> payload((std::istreambuf_iterator<char>(input)), {});
    const AnimationLoading::AnimationBoundsAssetReadResult bounds = AnimationLoading::AnimationAssetValidator::Validate(AnimationLoading::AnimationAssetReader::ReadAnimationBounds(payload));
    INFO("diagnostic=" << static_cast<u32>(bounds.diagnostic.code) << " field=" << bounds.diagnostic.field << " index=" << bounds.diagnostic.index << " observed=" << bounds.diagnostic.observed << " expected=" << bounds.diagnostic.expected);
    REQUIRE(bounds);
    CHECK(bounds.view.root.modelAssetID == 0x1C60093F74E4AA33ull);
    CHECK(bounds.view.root.skeletonAssetID == 0xFC5117E7F4409C54ull);
    REQUIRE(bounds.view.clips.size() >= 2);
    CHECK(bounds.view.clips[0].animationClipAssetID == FileFormat::INVALID_ASSET_ID);
    CHECK(bounds.view.clips[1].animationClipAssetID != FileFormat::INVALID_ASSET_ID);
    CHECK(bounds.view.segments.size() >= 8);
}

TEST_CASE("Animation bounds use a validated global fallback for graph compositions and missing clips", "[AnimationAsset][AnimationBounds]")
{
    const std::vector<u8> payload = SaveBounds(true);
    const AnimationLoading::AnimationBoundsAssetReadResult valid = AnimationLoading::AnimationAssetValidator::Validate(AnimationLoading::AnimationAssetReader::ReadAnimationBounds(payload));
    REQUIRE(valid);
    Format::Bounds staticFallback;
    staticFallback.extents = vec3(100.0f);
    staticFallback.sphereRadius = 200.0f;
    const AnimationLoading::SelectedAnimationBounds selected = AnimationLoading::SelectAnimationBounds(&valid.view, 10, 20, 999, 0, false, staticFallback);
    CHECK(selected.selection == AnimationLoading::AnimationBoundsSelection::ClipFallback);
    CHECK(selected.bounds.extents == vec3(8.0f));

    std::vector<u8> corrupt = SaveBounds(true);
    Format::AnimationBoundsAsset& root = Root<Format::AnimationBoundsAsset>(corrupt);
    auto* clips = reinterpret_cast<Format::ClipBounds*>(corrupt.data() + root.clipsOffset);
    clips[0].fallbackBounds.extents = vec3(1.0f);
    CHECK(AnimationLoading::AnimationAssetValidator::Validate(AnimationLoading::AnimationAssetReader::ReadAnimationBounds(corrupt)).diagnostic.code == AssetLoading::DiagnosticCode::InvalidValue);
}

TEST_CASE("Game validates NBS AnimationSet and synchronization marker fixtures", "[AnimationAsset][AnimationSet][Synchronization][NBS]")
{
    const char* setPath = std::getenv("NOVUS_NBS_PHASE13_SET");
    const char* clipPath = std::getenv("NOVUS_NBS_PHASE13_WALK_CLIP");
    if (!setPath || !clipPath)
        return;
    std::ifstream setInput(setPath, std::ios::binary);
    REQUIRE(setInput);
    const std::vector<u8> setPayload((std::istreambuf_iterator<char>(setInput)), {});
    const AnimationLoading::AnimationSetAssetReadResult animationSet = AnimationLoading::AnimationAssetValidator::Validate(AnimationLoading::AnimationAssetReader::ReadAnimationSet(setPayload));
    INFO(AssetLoading::Describe(animationSet.diagnostic));
    REQUIRE(animationSet);
    CHECK(animationSet.view.root.declaredPoseDomainType == Format::PoseDomainType::Skeleton);
    CHECK(animationSet.view.root.declaredPoseDomainAssetID == 0xFC5117E7F4409C54ull);
    CHECK(animationSet.view.slots.size() == 15);
    CHECK(animationSet.view.variants.size() == 17);
    CHECK(std::count_if(animationSet.view.slots.begin(), animationSet.view.slots.end(), [](const Format::AnimationSetSlot& slot) { return (slot.flags & Format::AnimationSetSlotFlags_Required) != 0; }) == 3);

    std::ifstream clipInput(clipPath, std::ios::binary);
    REQUIRE(clipInput);
    const std::vector<u8> clipPayload((std::istreambuf_iterator<char>(clipInput)), {});
    const AnimationLoading::AnimationClipAssetReadResult clip = AnimationLoading::AnimationAssetValidator::Validate(AnimationLoading::AnimationAssetReader::ReadAnimationClip(clipPayload));
    INFO(AssetLoading::Describe(clip.diagnostic));
    REQUIRE(clip);
    REQUIRE(clip.view.synchronizationMarkers.size() == 2);
    const f64 leftContactPhase = static_cast<f64>(clip.view.synchronizationMarkers[0].timeMicroseconds) / clip.view.root.durationMicroseconds;
    const f64 rightContactPhase = static_cast<f64>(clip.view.synchronizationMarkers[1].timeMicroseconds) / clip.view.root.durationMicroseconds;
    CHECK(leftContactPhase == Catch::Approx(0.155488).margin(0.02));
    CHECK(rightContactPhase == Catch::Approx(0.649390).margin(0.02));
    CHECK(clip.view.synchronizationMarkers[1].timeMicroseconds < clip.view.root.durationMicroseconds);
    CHECK(clip.view.synchronizationMarkers[0].semanticID != clip.view.synchronizationMarkers[1].semanticID);
    const auto rootTrack = std::find_if(clip.view.tracks.begin(), clip.view.tracks.end(), [](const Format::AnimationTrack& track) { return track.sourceJointIndex == 0; });
    REQUIRE(rootTrack != clip.view.tracks.end());
    REQUIRE((rootTrack->flags & Format::AnimationTrackFlags_HasTranslationSamples) != 0);
    REQUIRE(rootTrack->numTranslations == clip.view.root.sampleCount);
    const vec3 firstRootTranslation = clip.view.translationSamples[rootTrack->translationOffset];
    for (u32 index = 1; index < rootTrack->numTranslations; ++index)
    {
        const vec3 translation = clip.view.translationSamples[rootTrack->translationOffset + index];
        CHECK(translation.x == Catch::Approx(firstRootTranslation.x).margin(1.0e-6));
        CHECK(translation.z == Catch::Approx(firstRootTranslation.z).margin(1.0e-6));
    }
}

TEST_CASE("Animation authoring leases retain exact generations after reload and release", "[AnimationAsset][Authoring]")
{
    AnimationLoading::AnimationAssetRegistry registry;
    const auto first = registry.PublishSkeleton(100, SaveSkeleton(2));
    auto lease = registry.Lease(first);
    REQUIRE(lease);
    const auto second = registry.PublishSkeleton(100, SaveSkeleton(3));
    REQUIRE(registry.Lease(first));
    CHECK(registry.Lease(first)->joints[1].semanticID == 2);
    CHECK(registry.Get(second)->joints[1].semanticID == 3);
    CHECK(registry.Get(first) == nullptr);
    const u64 retainedBytes = registry.GetStats().residentBytes;
    REQUIRE(registry.Release(first));
    CHECK_FALSE(registry.Lease(first));
    CHECK(registry.GetStats().residentBytes < retainedBytes);
    CHECK(lease->joints[1].semanticID == 2);
    REQUIRE(registry.Release(second));
    CHECK(lease->joints[1].semanticID == 2);
}

TEST_CASE("Animation asset drafts preserve samples and reject invalid numeric edits", "[AnimationAsset][Authoring]")
{
    ::Animation::AnimationAssetDocument document;
    std::string error;
    REQUIRE(document.Open(::Animation::AnimationDocumentType::Clip, SaveClip(), error));
    const auto original = document.GetPayload();
    REQUIRE(document.Edit("/data/events/0/timeMicroseconds", "600000", error));
    REQUIRE(document.Compile(error));
    const auto accepted = document.GetPayload();
    auto clip = AnimationLoading::AnimationAssetReader::ReadAnimationClip(accepted);
    REQUIRE(clip);
    CHECK(clip.view.events[0].timeMicroseconds == 600000);
    CHECK(clip.view.translationSamples[2].x == 1.0f);
    CHECK_FALSE(document.Edit("/data/events/0/timeMicroseconds", "-1", error));
    REQUIRE(document.Edit("/data/events/0/timeMicroseconds", "999999999", error));
    CHECK_FALSE(document.Compile(error));
    CHECK(document.GetPayload() == accepted);
    REQUIRE(document.Undo());
    REQUIRE(document.Compile(error));
    CHECK(document.GetPayload() == accepted);
    document.Revert();
    REQUIRE(document.Compile(error));
    CHECK(document.GetPayload() == original);
}

TEST_CASE("Animation clip metadata cannot be loaded against different sample streams", "[AnimationAsset][Authoring]")
{
    ::Animation::AnimationAssetDocument document;
    std::string error;
    REQUIRE(document.Open(::Animation::AnimationDocumentType::Clip, SaveClip(), error));
    const auto source = document.Serialize();
    REQUIRE(document.LoadSource(source, error));
    REQUIRE(document.Compile(error));
    std::vector<u8> changed = SaveClip();
    const auto& root = Root<Format::AnimationClipAsset>(changed);
    reinterpret_cast<vec3*>(changed.data() + root.translationSamplesOffset)[1].x = 0.75f;
    REQUIRE(document.Open(::Animation::AnimationDocumentType::Clip, changed, error));
    const auto accepted = document.Serialize();
    CHECK_FALSE(document.LoadSource(source, error));
    CHECK(error.find("different sample streams") != std::string::npos);
    CHECK(document.Serialize() == accepted);
    REQUIRE(document.Compile(error));
    CHECK(AnimationLoading::AnimationAssetReader::ReadAnimationClip(document.GetPayload()).view.translationSamples[1].x == 0.75f);
}

TEST_CASE("Asset binary sources preserve typed drafts and reject malformed files atomically", "[AnimationAsset][Authoring][BinarySource]")
{
    ::Animation::AnimationAssetDocument document;
    std::string error;
    for (bool skeleton : {false, true})
    {
        REQUIRE(document.Open(skeleton ? ::Animation::AnimationDocumentType::Skeleton : ::Animation::AnimationDocumentType::Clip, skeleton ? SaveSkeleton() : SaveClip(), error));
        const auto bytes = document.Serialize();
        const auto payload = document.GetPayload();
        REQUIRE(document.LoadSource(bytes, error));
        REQUIRE(document.Compile(error));
        CHECK(document.GetPayload() == payload);
        const auto revision = document.GetRevision();
        for (size_t size = 0; size < bytes.size(); ++size)
        {
            CHECK_FALSE(document.LoadSource(std::span(bytes).first(size), error));
            CHECK(document.GetRevision() == revision);
        }
        for (size_t offset : {size_t{0}, size_t{4}, size_t{8}})
        {
            auto corrupt = bytes; corrupt[offset] = 255;
            CHECK_FALSE(document.LoadSource(corrupt, error));
        }
        auto trailing = bytes; trailing.push_back(0);
        CHECK_FALSE(document.LoadSource(trailing, error));
        CHECK(document.Serialize() == bytes);
    }
}

TEST_CASE("Animation mask authoring paints hierarchy falloff and mirrors without cascading", "[AnimationAsset][Authoring]")
{
    Format::BoneMaskAsset root;
    root.poseDomainAssetID = 100;
    root.poseDomainType = Format::PoseDomainType::Skeleton;
    Format::BoneMaskData data;
    data.jointWeights = {0, 0};
    auto buffer = Bytebuffer::BorrowRuntime(root.GetSerializedSize(data));
    REQUIRE(root.Save(buffer, data));
    ::Animation::AnimationAssetDocument document;
    std::string error;
    REQUIRE(document.Open(::Animation::AnimationDocumentType::Mask, {buffer->GetDataPointer(), buffer->writtenData}, error));
    const auto skeletonBytes = SaveSkeleton();
    const auto skeleton = AnimationLoading::AnimationAssetReader::ReadSkeleton(skeletonBytes);
    REQUIRE(skeleton);
    REQUIRE(document.PaintMask(skeleton.view.joints, 0, 1.0f, 0.5f, true, error));
    REQUIRE(document.Compile(error));
    auto mask = AnimationLoading::AnimationAssetReader::ReadBoneMask(document.GetPayload());
    CHECK(mask.view.jointWeights[0] == 255);
    CHECK(mask.view.jointWeights[1] == 128);
    const std::array<std::pair<u32, u32>, 2> pairs = {{{0, 1}, {1, 0}}};
    REQUIRE(document.MirrorMask(pairs, error));
    REQUIRE(document.Compile(error));
    mask = AnimationLoading::AnimationAssetReader::ReadBoneMask(document.GetPayload());
    CHECK(mask.view.jointWeights[0] == 128);
    CHECK(mask.view.jointWeights[1] == 255);
    const auto accepted = document.GetPayload();
    CHECK_FALSE(document.Edit("/data/jointWeights/0", "256", error));
    CHECK(document.Compile(error));
    CHECK(document.GetPayload() == accepted);
}

TEST_CASE("Animation V2 registry caches failures and rejects stale handles after atomic reload", "[AnimationAsset]")
{
    AnimationLoading::AnimationAssetRegistry registry;
    const AnimationLoading::SkeletonHandle first = registry.PublishSkeleton(100, SaveSkeleton(2));
    REQUIRE(first);
    REQUIRE(registry.Get(first));
    CHECK(registry.Get(first)->joints[1].semanticID == 2);

    const AnimationLoading::SkeletonHandle replacement = registry.PublishSkeleton(100, SaveSkeleton(3));
    REQUIRE(replacement);
    CHECK(first.GetRawValue() != replacement.GetRawValue());
    AssetLoading::Diagnostic staleDiagnostic;
    CHECK(registry.Get(first, &staleDiagnostic) == nullptr);
    CHECK(staleDiagnostic.code == AssetLoading::DiagnosticCode::StaleHandle);
    REQUIRE(registry.Get(replacement));
    CHECK(registry.Get(replacement)->joints[1].semanticID == 3);
    std::vector<u8> invalidReload = SaveSkeleton(4);
    Root<Format::SkeletonAsset>(invalidReload).numJoints = std::numeric_limits<u32>::max();
    const AnimationLoading::SkeletonHandle preserved = registry.PublishSkeleton(100, invalidReload);
    CHECK(preserved == replacement);
    REQUIRE(registry.Get(replacement));
    CHECK(registry.Get(replacement)->joints[1].semanticID == 3);
    CHECK(registry.Release(first));
    CHECK_FALSE(registry.Release(first));
    CHECK(registry.Release(replacement));
    CHECK_FALSE(registry.Release(replacement));

    const AnimationLoading::SkeletonHandle missingFirst = registry.LoadSkeleton(200);
    const AnimationLoading::SkeletonHandle missingSecond = registry.LoadSkeleton(200);
    CHECK(missingFirst == missingSecond);
    AssetLoading::Diagnostic missingDiagnostic;
    CHECK(registry.Get(missingFirst, &missingDiagnostic) == nullptr);
    CHECK(missingDiagnostic.code == AssetLoading::DiagnosticCode::MissingDependency);
    CHECK(missingDiagnostic.requestedAssetID == 200);
    const auto failures = registry.GetFailureDescriptions();
    REQUIRE(failures.size() == 1);
    CHECK(failures[0].find("200") != std::string::npos);
    CHECK_FALSE(failures[0].empty());

    const AnimationLoading::AnimationAssetRegistryStats stats = registry.GetStats();
    CHECK(stats.residentAssets == 2);
    CHECK(stats.residentFailures == 1);
    CHECK(stats.references == 2);
    CHECK(stats.cacheHits == 1);
    CHECK(stats.failures == 2);
    CHECK(stats.reloads == 1);
    CHECK(stats.staleHandleRejections == 1);
    CHECK(stats.skeletonsWithoutRigFamily == 1);
}

TEST_CASE("Animation V2 reads the current NBS Skeleton fixture when supplied", "[AnimationAsset][NBS]")
{
    const char* path = std::getenv("NOVUS_NBS_PHASE2_SKELETON");
    if (path == nullptr)
    {
        WARN("NOVUS_NBS_PHASE2_SKELETON is not set; skipping external NBS fixture");
        return;
    }
    std::ifstream input(path, std::ios::binary);
    REQUIRE(input);
    const std::vector<u8> payload((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    constexpr FileFormat::AssetID skeletonAssetID = 0xFC5117E7F4409C54ull;
    AnimationLoading::AnimationAssetRegistry registry;
    const AnimationLoading::SkeletonHandle handle = registry.PublishSkeleton(skeletonAssetID, payload);
    AssetLoading::Diagnostic diagnostic;
    const AnimationLoading::SkeletonAssetView* result = registry.Get(handle, &diagnostic);
    INFO(AssetLoading::Describe(diagnostic));
    REQUIRE(result);
    CHECK(registry.GetAssetID(handle) == skeletonAssetID);
    CHECK(result->joints.size() == 65);
    CHECK(result->hierarchyDepthRanges.size() > 1);
    CHECK(std::count_if(result->joints.begin(), result->joints.end(), [](const Format::SkeletonJoint& joint) { return (joint.flags & Format::SkeletonJointFlags_Deformation) != 0; }) == 52);
    CHECK(std::count_if(result->joints.begin(), result->joints.end(), [](const Format::SkeletonJoint& joint) { return joint.parentJointIndex == -1; }) == 1);
    CHECK(registry.GetStats().skeletonsWithoutRigFamily == 1);
}
