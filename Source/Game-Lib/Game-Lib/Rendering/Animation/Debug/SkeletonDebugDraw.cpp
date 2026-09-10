#include "SkeletonDebugDraw.h"
#include "Game-Lib/Rendering/Animation/Pose/AnimationClipSampler.h"

#include "Game-Lib/Application/EnttRegistries.h"
#include "Game-Lib/ECS/Singletons/EditorSelection.h"
#include "Game-Lib/ECS/Util/Transforms.h"
#include "Game-Lib/Rendering/Debug/DebugRenderer.h"
#include "Game-Lib/Util/AssetPath.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Base/CVarSystem/CVarSystem.h>
#include <Base/Util/DebugHandler.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <limits>

#include <glm/gtc/constants.hpp>

namespace
{
    AutoCVar_String CVAR_AnimationDebugReferenceClip(CVarCategory::Client | CVarCategory::Rendering, "animationDebugReferenceClip", "optional target-Skeleton golden clip for exact-time position/orientation error heatmap", "", CVarFlags::DoNotSave);
    AutoCVar_Int CVAR_AnimationDebugRigFamily(CVarCategory::Client | CVarCategory::Rendering, "animationDebugRigFamily", "family overlay bits: 1 canonical pose, 2 mapping links, 4 reference axes, 8 propagation", 0, CVarFlags::DoNotSave);
    AutoCVar_Int CVAR_AnimationDebugSkeletonEnabled(CVarCategory::Client | CVarCategory::Rendering, "animationDebugSkeletonEnabled", "draw the selected entity's Animation V2 skeleton", 0, CVarFlags::EditCheckbox | CVarFlags::DoNotSave);
    AutoCVar_String CVAR_AnimationDebugSkeletonPath(CVarCategory::Client | CVarCategory::Rendering, "animationDebugSkeletonPath", "mounted PACT path of the Animation V2 skeleton to inspect", "character.skeleton", CVarFlags::DoNotSave);
    AutoCVar_Int CVAR_AnimationDebugSkeletonPose(CVarCategory::Client | CVarCategory::Rendering, "animationDebugSkeletonPose", "Animation V2 debug pose: 0 rest, 1 current with rest fallback", 0, CVarFlags::DoNotSave);
    AutoCVar_Int CVAR_AnimationDebugSkeletonAxes(CVarCategory::Client | CVarCategory::Rendering, "animationDebugSkeletonAxes", "draw Animation V2 joint model-space axes", 1, CVarFlags::EditCheckbox | CVarFlags::DoNotSave);
    AutoCVar_Int CVAR_AnimationDebugSkeletonLabels(CVarCategory::Client | CVarCategory::Rendering, "animationDebugSkeletonLabels", "Animation V2 labels: 0 off, 1 joint indices, 2 selected semantic ID", 0, CVarFlags::DoNotSave);
    AutoCVar_Int CVAR_AnimationDebugSkeletonBounds(CVarCategory::Client | CVarCategory::Rendering, "animationDebugSkeletonBounds", "draw bounds around Animation V2 debug joints", 0, CVarFlags::EditCheckbox | CVarFlags::DoNotSave);
    AutoCVar_Int CVAR_AnimationDebugBoundsEnabled(CVarCategory::Client | CVarCategory::Rendering, "animationDebugBoundsEnabled", "draw selected pre-pose and actual CPU post-skin Animation V2 bounds; red indicates a containment violation", 0, CVarFlags::EditCheckbox | CVarFlags::DoNotSave);
    AutoCVar_Int CVAR_AnimationDebugSkeletonDepthColor(CVarCategory::Client | CVarCategory::Rendering, "animationDebugSkeletonDepthColor", "color Animation V2 joints by hierarchy depth", 1, CVarFlags::EditCheckbox | CVarFlags::DoNotSave);
    AutoCVar_Int CVAR_AnimationDebugSkeletonWeights(CVarCategory::Client | CVarCategory::Rendering, "animationDebugSkeletonWeights", "color Animation V2 joints by final layer weight", 0, CVarFlags::EditCheckbox | CVarFlags::DoNotSave);
    AutoCVar_Int CVAR_AnimationDebugIK(CVarCategory::Client | CVarCategory::Rendering, "animationDebugIK", "draw Animation V2 IK chains, goals, poles, and reach", 0, CVarFlags::EditCheckbox | CVarFlags::DoNotSave);
    AutoCVar_Int CVAR_AnimationDebugAttachments(CVarCategory::Client | CVarCategory::Rendering, "animationDebugAttachments", "draw Animation V2 semantic attachment sockets and local axes", 0, CVarFlags::EditCheckbox | CVarFlags::DoNotSave);
    AutoCVar_Int CVAR_AnimationDebugSkeletonSelectedJoint(CVarCategory::Client | CVarCategory::Rendering, "animationDebugSkeletonSelectedJoint", "highlight one Animation V2 joint index, or -1", -1, CVarFlags::DoNotSave);
    AutoCVar_Int CVAR_AnimationDebugSkeletonUseSelection(CVarCategory::Client | CVarCategory::Rendering, "animationDebugSkeletonUseSelection", "place Animation V2 skeleton at the selected entity transform", 1, CVarFlags::EditCheckbox | CVarFlags::DoNotSave);
    AutoCVar_Float CVAR_AnimationDebugSkeletonJointRadius(CVarCategory::Client | CVarCategory::Rendering, "animationDebugSkeletonJointRadius", "Animation V2 joint cross radius", 0.015, CVarFlags::EditFloatDrag | CVarFlags::DoNotSave);
    AutoCVar_Float CVAR_AnimationDebugSkeletonAxisLength(CVarCategory::Client | CVarCategory::Rendering, "animationDebugSkeletonAxisLength", "Animation V2 joint axis length", 0.08, CVarFlags::EditFloatDrag | CVarFlags::DoNotSave);
    AutoCVar_Int CVAR_AnimationDebugSkeletonMaxLines(CVarCategory::Client | CVarCategory::Rendering, "animationDebugSkeletonMaxLines", "hard per-frame Animation V2 skeleton debug line capacity", 8192, CVarFlags::DoNotSave);

    const std::array<Color, 8> DEPTH_COLORS = {Color::PastelRed, Color::PastelOrange, Color::PastelYellow, Color::PastelGreen, Color::PastelBlue, Color::PastelPurple, Color::Cyan, Color::White};
    constexpr std::array<u8, 16> HEX_SEGMENTS = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F, 0x77, 0x7C, 0x39, 0x5E, 0x79, 0x71};

    std::string NormalizeSkeletonPath(const char* path)
    {
        std::string result = path ? path : "";
        std::replace(result.begin(), result.end(), '\\', '/');
        std::transform(result.begin(), result.end(), result.begin(), [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
        constexpr std::string_view prefixes[] = {"data/pact/data/staging/", "data/pact/data/custom/", "pact/data/staging/", "pact/data/custom/"};
        for (std::string_view prefix : prefixes)
            if (result.starts_with(prefix))
                return result.substr(prefix.size());
        return result;
    }

    vec3 WorldTransformPoint(const mat4x4& transform, const vec3& point)
    {
        return vec3(transform * vec4(point, 1.0f));
    }

    vec3 WorldTransformVector(const mat4x4& transform, const vec3& vector)
    {
        return vec3(transform * vec4(vector, 0.0f));
    }
}

namespace Animation
{
    SkeletonDebugDraw::SkeletonDebugDraw(DebugRenderer* debugRenderer, AnimationLoading::AnimationAssetRegistry* assets) : _debugRenderer(debugRenderer), _assets(assets) {}

    SkeletonDebugDraw::~SkeletonDebugDraw()
    {
        ReleaseSkeleton();
    }

    void SkeletonDebugDraw::Update()
    {
        _stats = {};
        const bool skeletonEnabled = CVAR_AnimationDebugSkeletonEnabled.Get() != 0;
        const bool boundsEnabled = CVAR_AnimationDebugBoundsEnabled.Get() != 0;
        if (!skeletonEnabled && !boundsEnabled)
        {
            if (_wasEnabled)
                NC_LOG_INFO("ANIMATION_DEBUG_SKELETON disabled zero_work=1");
            _wasEnabled = false;
            _reportedReady = false;
            return;
        }
        _wasEnabled = true;
        _stats.enabled = true;
        if (boundsEnabled && _hasAnimationBounds)
        {
            DrawBounds(_selectedWorldBounds, _animationBoundsViolation ? Color::Red : (_tightAnimationBounds ? Color::Green : Color::Yellow));
            DrawBounds(_actualWorldBounds, _animationBoundsViolation ? Color::Red : Color::Cyan);
        }
        if (!skeletonEnabled || !EnsureSkeletonLoaded() || !EvaluateRequestedPose())
            return;
        EvaluateReference();

        const bool livePose = CVAR_AnimationDebugSkeletonPose.Get() != 0 && !_stats.currentPoseFallback;
        mat4x4 worldTransform = livePose ? _currentWorldTransform : mat4x4(1.0f);
        if (!livePose && CVAR_AnimationDebugSkeletonUseSelection.Get() != 0)
        {
            EnttRegistries* registries = ServiceLocator::GetEnttRegistries();
            if (registries && registries->gameRegistry)
            {
                entt::registry& registry = *registries->gameRegistry;
                const auto& selection = registry.ctx().get<ECS::Singletons::EditorSelection>();
                if (selection.selectedEntity != entt::null && registry.valid(selection.selectedEntity))
                    if (const auto* transform = registry.try_get<ECS::Components::Transform>(selection.selectedEntity))
                        worldTransform = transform->GetMatrix();
            }
        }
        DrawSkeleton(worldTransform);
        if (CVAR_AnimationDebugRigFamily.Get() != 0)
            DrawFamily(worldTransform);
        if (!_reportedReady)
        {
            _reportedReady = true;
            NC_LOG_INFO("ANIMATION_DEBUG_SKELETON ready joints={} lines={} dropped={} evaluations={}", _stats.joints, _stats.lines, _stats.droppedLines, _stats.evaluations);
        }
    }

    bool SkeletonDebugDraw::SetCurrentLocalPose(FileFormat::AssetID skeletonAssetID, std::span<const FileFormat::Animation::LocalTransform> localPose, u64 sampleTimeMicroseconds, const mat4x4& worldTransform)
    {
        if (CVAR_AnimationDebugSkeletonEnabled.Get() == 0 || skeletonAssetID != Util::AssetPath::Hash(NormalizeSkeletonPath(CVAR_AnimationDebugSkeletonPath.Get())))
            return false;
        _currentLocalPose.assign(localPose.begin(), localPose.end());
        _sampleTimeMicroseconds = sampleTimeMicroseconds;
        _currentWorldTransform = worldTransform;
        return true;
    }

    void SkeletonDebugDraw::SetJointWeights(std::span<const f32> weights)
    {
        _jointWeights.assign(weights.begin(), weights.end());
    }

    void SkeletonDebugDraw::SetIKOperations(std::span<const PoseIKDebugOperation> operations)
    {
        _ikOperations = {};
        _ikOperationCount = static_cast<u32>(std::min(operations.size(), _ikOperations.size()));
        std::copy_n(operations.begin(), _ikOperationCount, _ikOperations.begin());
    }

    void SkeletonDebugDraw::ClearCurrentLocalPose()
    {
        _currentLocalPose.clear();
    }

    bool SkeletonDebugDraw::WantsAnimationBounds() const
    {
        return CVAR_AnimationDebugBoundsEnabled.Get() != 0;
    }

    void SkeletonDebugDraw::SetAnimationBounds(const FileFormat::Animation::Bounds& selectedWorldBounds, const FileFormat::Animation::Bounds& actualWorldBounds, bool tightSelection, bool violation)
    {
        _selectedWorldBounds = selectedWorldBounds;
        _actualWorldBounds = actualWorldBounds;
        _tightAnimationBounds = tightSelection;
        _animationBoundsViolation = violation;
        _hasAnimationBounds = true;
    }

    void SkeletonDebugDraw::ClearAnimationBounds()
    {
        _hasAnimationBounds = false;
    }

    bool SkeletonDebugDraw::EnsureSkeletonLoaded()
    {
        const std::string path = NormalizeSkeletonPath(CVAR_AnimationDebugSkeletonPath.Get());
        if (path.empty())
            return false;
        const FileFormat::AssetID pathID = Util::AssetPath::Hash(path);
        if (!_skeletonHandle || pathID != _skeletonPathID || _assets->GetAssetID(_skeletonHandle) != pathID)
        {
            ReleaseSkeleton();
            _loadedPath = path;
            _skeletonPathID = pathID;
            _skeletonHandle = _assets->LoadSkeleton(pathID);
            AssetLoading::Diagnostic diagnostic;
            _skeleton = _assets->Get(_skeletonHandle, &diagnostic);
            if (!_skeleton)
            {
                NC_LOG_ERROR("ANIMATION_DEBUG_SKELETON load_failed path={} asset={} diagnostic={}", path, pathID, AssetLoading::Describe(diagnostic));
                return false;
            }
            NC_LOG_INFO("ANIMATION_DEBUG_SKELETON loaded path={} asset={} joints={}", path, pathID, _skeleton->joints.size());
        }
        _stats.loaded = _skeleton != nullptr;
        _stats.joints = _skeleton ? static_cast<u32>(_skeleton->joints.size()) : 0;
        return _skeleton != nullptr;
    }

    bool SkeletonDebugDraw::EvaluateRequestedPose()
    {
        const bool currentRequested = CVAR_AnimationDebugSkeletonPose.Get() != 0;
        if (currentRequested && _currentLocalPose.size() == _skeleton->joints.size())
            _evaluatedPose = EvaluatePose(_skeleton->joints, _currentLocalPose);
        else
        {
            _stats.currentPoseFallback = currentRequested;
            _evaluatedPose = EvaluateRestPose(*_skeleton);
        }
        ++_stats.evaluations;
        if (!_evaluatedPose)
        {
            NC_LOG_ERROR("ANIMATION_DEBUG_SKELETON evaluation_failed error={} joint={}", static_cast<u32>(_evaluatedPose.error), _evaluatedPose.errorJointIndex);
            return false;
        }
        return true;
    }

    void SkeletonDebugDraw::EvaluateReference()
    {
        _referenceErrors.clear();
        const std::string path = NormalizeSkeletonPath(CVAR_AnimationDebugReferenceClip.Get());
        if (path != _referencePath)
        {
            if (_referenceHandle) _assets->Release(_referenceHandle);
            _referenceHandle = {};
            _referencePath = path;
        }
        if (path.empty() || _stats.currentPoseFallback)
            return;
        if (_referenceHandle && _assets->GetAssetID(_referenceHandle) != Util::AssetPath::Hash(path))
        {
            _assets->Release(_referenceHandle);
            _referenceHandle = {};
        }
        if (!_referenceHandle)
            _referenceHandle = _assets->LoadAnimationClip(Util::AssetPath::Hash(path));
        const auto* reference = _assets->Get(_referenceHandle);
        if (!reference || reference->root.sourcePoseDomainType != FileFormat::Animation::PoseDomainType::Skeleton || reference->root.sourcePoseDomainAssetID != _skeletonPathID)
            return;
        std::vector<FileFormat::Animation::LocalTransform> sampled(_skeleton->joints.size());
        if (SampleClipLocalPose(*reference, *_skeleton, static_cast<f64>(_sampleTimeMicroseconds) / 1000000.0, true, sampled) != ClipSampleError::None)
            return;
        const auto expected = EvaluatePose(_skeleton->joints, sampled);
        if (!expected || _currentLocalPose.size() != sampled.size())
            return;
        _referenceErrors.resize(sampled.size());
        for (u32 index = 0; index < sampled.size(); ++index)
        {
            const f32 positionError = glm::length(vec3(expected.modelTransforms[index][3]) - vec3(_evaluatedPose.modelTransforms[index][3]));
            const f32 angularError = glm::degrees(2.0f * std::acos(std::clamp(std::abs(glm::dot(sampled[index].rotation, _currentLocalPose[index].rotation)), 0.0f, 1.0f)));
            _referenceErrors[index] = std::max(positionError / 0.001f, angularError / 0.1f);
            _stats.maximumReferencePositionError = std::max(_stats.maximumReferencePositionError, positionError);
            _stats.maximumReferenceAngleError = std::max(_stats.maximumReferenceAngleError, angularError);
        }
        _stats.referenceCompared = true;
    }

    void SkeletonDebugDraw::DrawFamily(const mat4x4& worldTransform)
    {
        if (_skeleton->root.rigFamilyAssetID == FileFormat::INVALID_ASSET_ID)
            return;
        if (_familyHandle && _assets->GetAssetID(_familyHandle) != _skeleton->root.rigFamilyAssetID)
        {
            _assets->Release(_familyHandle);
            _familyHandle = {};
        }
        if (!_familyHandle)
        {
            _familyHandle = _assets->LoadRigFamily(_skeleton->root.rigFamilyAssetID);
            const auto* family = _assets->Get(_familyHandle);
            if (!family || !_familyBinding.Initialize(*_skeleton, *family))
            {
                _assets->Release(_familyHandle);
                _familyHandle = {};
                return;
            }
        }
        const auto* family = _assets->Get(_familyHandle);
        if (!family)
            return;
        std::vector<FileFormat::Animation::LocalTransform> concrete, canonical(family->joints.size());
        if (CVAR_AnimationDebugSkeletonPose.Get() != 0 && _currentLocalPose.size() == _skeleton->joints.size())
            concrete = _currentLocalPose;
        else
            for (const auto& joint : _skeleton->joints)
                concrete.push_back(joint.restTransform);
        if (!_familyBinding.ToFamily(concrete, canonical))
            return;
        std::vector<FileFormat::Animation::SkeletonJoint> joints(family->joints.size());
        for (u32 index = 0; index < joints.size(); ++index)
            joints[index].parentJointIndex = family->joints[index].parentJointIndex;
        const auto pose = EvaluatePose(joints, canonical);
        if (!pose)
            return;
        const i32 mode = CVAR_AnimationDebugRigFamily.Get();
        const mat4x4 displaced = glm::translate(worldTransform, vec3(2.0f, 0, 0));
        for (u32 index = 0; index < joints.size(); ++index)
        {
            const auto& transform = pose.modelTransforms[index];
            const vec3 position = WorldTransformPoint(displaced, transform[3]);
            const bool required = (family->joints[index].flags & FileFormat::Animation::RigFamilyJointFlags_Required) != 0;
            if ((mode & 1) != 0 && joints[index].parentJointIndex >= 0)
                EmitLine(WorldTransformPoint(displaced, pose.modelTransforms[joints[index].parentJointIndex][3]), position, required ? Color::Cyan : Color::PastelPurple);
            if ((mode & 4) != 0)
                for (u32 axis = 0; axis < 3; ++axis)
                    EmitLine(position, position + glm::normalize(WorldTransformVector(displaced, transform[axis])) * 0.07f, axis == 0 ? Color::Red : (axis == 1 ? Color::Green : Color::Blue));
        }
        if ((mode & 1) != 0)
            for (const auto& chain : family->chains)
                for (u32 index = 1; index < chain.numJointIndices; ++index)
                {
                    const u32 from = family->chainJointIndices[chain.jointIndexOffset + index - 1];
                    const u32 to = family->chainJointIndices[chain.jointIndexOffset + index];
                    EmitLine(WorldTransformPoint(displaced, pose.modelTransforms[from][3]), WorldTransformPoint(displaced, pose.modelTransforms[to][3]), Color::Yellow);
                }
        if ((mode & 2) != 0)
            for (const auto& binding : _skeleton->familyBindings)
                EmitLine(WorldTransformPoint(worldTransform, _evaluatedPose.modelTransforms[binding.skeletonJointIndex][3]), WorldTransformPoint(displaced, pose.modelTransforms[binding.familyJointIndex][3]), Color::PastelBlue);
        if ((mode & 8) != 0)
            for (const auto& rule : _skeleton->propagationRules)
                EmitLine(WorldTransformPoint(worldTransform, _evaluatedPose.modelTransforms[rule.sourceJointIndex][3]), WorldTransformPoint(worldTransform, _evaluatedPose.modelTransforms[rule.targetJointIndex][3]), Color::Magenta);
    }

    void SkeletonDebugDraw::DrawSkeleton(const mat4x4& worldTransform)
    {
        const f32 radius = std::max(CVAR_AnimationDebugSkeletonJointRadius.GetFloat(), 0.0001f);
        const f32 axisLength = std::max(CVAR_AnimationDebugSkeletonAxisLength.GetFloat(), 0.0001f);
        const i32 selectedJoint = CVAR_AnimationDebugSkeletonSelectedJoint.Get();
        const i32 labelMode = CVAR_AnimationDebugSkeletonLabels.Get();
        vec3 boundsMin(std::numeric_limits<f32>::max());
        vec3 boundsMax(std::numeric_limits<f32>::lowest());

        for (u32 jointIndex = 0; jointIndex < _skeleton->joints.size(); ++jointIndex)
        {
            const FileFormat::Animation::SkeletonJoint& joint = _skeleton->joints[jointIndex];
            const mat4a& jointTransform = _evaluatedPose.modelTransforms[jointIndex];
            const vec3 position = WorldTransformPoint(worldTransform, jointTransform[3]);
            boundsMin = glm::min(boundsMin, position);
            boundsMax = glm::max(boundsMax, position);
            Color color = CVAR_AnimationDebugSkeletonDepthColor.Get() != 0 ? DEPTH_COLORS[joint.hierarchyDepth % DEPTH_COLORS.size()] : Color::PastelBlue;
            if (CVAR_AnimationDebugSkeletonWeights.Get() != 0 && _jointWeights.size() == _skeleton->joints.size())
            {
                const f32 weight = std::clamp(_jointWeights[jointIndex], 0.0f, 1.0f);
                color = weight <= 0.001f ? Color::PastelBlue : (weight < 0.5f ? Color::Cyan : (weight < 0.999f ? Color::Yellow : Color::Red));
            }
            if (static_cast<i32>(jointIndex) == selectedJoint)
                color = Color::Yellow;
            if (_referenceErrors.size() == _skeleton->joints.size())
                color = _referenceErrors[jointIndex] <= 1.0f ? Color::Green : (_referenceErrors[jointIndex] <= 2.0f ? Color::Yellow : Color::Red);

            if (joint.parentJointIndex >= 0)
            {
                const vec3 parentPosition = WorldTransformPoint(worldTransform, _evaluatedPose.modelTransforms[joint.parentJointIndex][3]);
                EmitLine(parentPosition, position, color);
            }

            const f32 jointRadius = static_cast<i32>(jointIndex) == selectedJoint ? radius * 2.0f : radius;
            EmitLine(position - vec3(jointRadius, 0.0f, 0.0f), position + vec3(jointRadius, 0.0f, 0.0f), color);
            EmitLine(position - vec3(0.0f, jointRadius, 0.0f), position + vec3(0.0f, jointRadius, 0.0f), color);
            EmitLine(position - vec3(0.0f, 0.0f, jointRadius), position + vec3(0.0f, 0.0f, jointRadius), color);

            const vec3 axisX = glm::normalize(WorldTransformVector(worldTransform, jointTransform[0]));
            const vec3 axisY = glm::normalize(WorldTransformVector(worldTransform, jointTransform[1]));
            const vec3 axisZ = glm::normalize(WorldTransformVector(worldTransform, jointTransform[2]));
            if (CVAR_AnimationDebugSkeletonAxes.Get() != 0)
            {
                EmitLine(position, position + axisX * axisLength, Color::Red);
                EmitLine(position, position + axisY * axisLength, Color::Green);
                EmitLine(position, position + axisZ * axisLength, Color::Blue);
            }
            if (labelMode == 1)
                DrawLabel(jointIndex, position + axisY * radius, axisX, axisY, color, 4);
            else if (labelMode == 2 && static_cast<i32>(jointIndex) == selectedJoint)
                DrawLabel(joint.semanticID, position + axisY * radius, axisX, axisY, color, 16);
        }

        if (CVAR_AnimationDebugIK.Get() != 0)
        {
            for (u32 operationIndex = 0; operationIndex < _ikOperationCount; ++operationIndex)
            {
                const PoseIKDebugOperation& operation = _ikOperations[operationIndex];
                if (operation.jointCount == 0 || operation.jointIndices[0] >= _evaluatedPose.modelTransforms.size())
                    continue;
                const Color chainColor = operation.clamped ? Color::Red : Color::PastelOrange;
                const vec3 root = WorldTransformPoint(worldTransform, _evaluatedPose.modelTransforms[operation.jointIndices[0]][3]);
                const vec3 goal = WorldTransformPoint(worldTransform, operation.goalModel);
                const vec3 pole = WorldTransformPoint(worldTransform, operation.poleModel);
                for (u32 joint = 1; joint < operation.jointCount; ++joint)
                {
                    if (operation.jointIndices[joint - 1] >= _evaluatedPose.modelTransforms.size() || operation.jointIndices[joint] >= _evaluatedPose.modelTransforms.size())
                        continue;
                    EmitLine(WorldTransformPoint(worldTransform, _evaluatedPose.modelTransforms[operation.jointIndices[joint - 1]][3]), WorldTransformPoint(worldTransform, _evaluatedPose.modelTransforms[operation.jointIndices[joint]][3]), chainColor);
                }
                EmitLine(root, goal, chainColor);
                EmitLine(root, pole, Color::Cyan);
                EmitLine(goal - vec3(radius, 0.0f, 0.0f), goal + vec3(radius, 0.0f, 0.0f), chainColor);
                EmitLine(goal - vec3(0.0f, radius, 0.0f), goal + vec3(0.0f, radius, 0.0f), chainColor);
                EmitLine(goal - vec3(0.0f, 0.0f, radius), goal + vec3(0.0f, 0.0f, radius), chainColor);
                _debugRenderer->DrawSphere3D(goal, radius * 2.5f, 16, chainColor);
                if (operation.maximumReach > 0.0f)
                {
                    constexpr u32 reachSegments = 24;
                    for (u32 segment = 0; segment < reachSegments; ++segment)
                    {
                        const f32 first = glm::two_pi<f32>() * static_cast<f32>(segment) / static_cast<f32>(reachSegments);
                        const f32 second = glm::two_pi<f32>() * static_cast<f32>(segment + 1u) / static_cast<f32>(reachSegments);
                        EmitLine(root + vec3(std::cos(first), std::sin(first), 0.0f) * operation.maximumReach, root + vec3(std::cos(second), std::sin(second), 0.0f) * operation.maximumReach, chainColor);
                    }
                }
            }
        }

        if (CVAR_AnimationDebugAttachments.Get() != 0)
        {
            const std::array<Color, 3> attachmentColors = {Color::Cyan, Color::PastelOrange, Color::PastelPurple};
            for (u32 attachmentIndex = 0; attachmentIndex < _skeleton->attachments.size(); ++attachmentIndex)
            {
                const FileFormat::Animation::SkeletonAttachment& attachment = _skeleton->attachments[attachmentIndex];
                if (attachment.jointIndex >= _evaluatedPose.modelTransforms.size())
                    continue;
                const mat4a socketModel = Math::AffineMatrix::MatrixMul(_evaluatedPose.modelTransforms[attachment.jointIndex], ComposeLocalTransform(attachment.localTransform));
                const vec3 socket = WorldTransformPoint(worldTransform, socketModel[3]);
                const vec3 axisX = glm::normalize(WorldTransformVector(worldTransform, socketModel[0]));
                const vec3 axisY = glm::normalize(WorldTransformVector(worldTransform, socketModel[1]));
                const vec3 axisZ = glm::normalize(WorldTransformVector(worldTransform, socketModel[2]));
                const Color color = attachmentColors[attachmentIndex % attachmentColors.size()];
                _debugRenderer->DrawSphere3D(socket, radius * 2.0f, 12, color);
                EmitLine(socket, socket + axisX * axisLength * 1.5f, Color::Red);
                EmitLine(socket, socket + axisY * axisLength * 1.5f, Color::Green);
                EmitLine(socket, socket + axisZ * axisLength * 1.5f, Color::Blue);
            }
        }

        if (CVAR_AnimationDebugSkeletonBounds.Get() != 0 && !_skeleton->joints.empty())
        {
            const std::array<vec3, 8> corners = {vec3(boundsMin.x, boundsMin.y, boundsMin.z), vec3(boundsMax.x, boundsMin.y, boundsMin.z), vec3(boundsMax.x, boundsMax.y, boundsMin.z), vec3(boundsMin.x, boundsMax.y, boundsMin.z), vec3(boundsMin.x, boundsMin.y, boundsMax.z), vec3(boundsMax.x, boundsMin.y, boundsMax.z), vec3(boundsMax.x, boundsMax.y, boundsMax.z), vec3(boundsMin.x, boundsMax.y, boundsMax.z)};
            constexpr std::array<std::array<u8, 2>, 12> edges = {{{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}}};
            for (const auto& edge : edges)
                EmitLine(corners[edge[0]], corners[edge[1]], Color::PastelPurple);
        }

        if (selectedJoint != _lastLoggedSelectedJoint)
        {
            _lastLoggedSelectedJoint = selectedJoint;
            if (selectedJoint >= 0 && static_cast<u32>(selectedJoint) < _skeleton->joints.size())
                NC_LOG_INFO("ANIMATION_DEBUG_SKELETON selected_joint index={} semantic={} depth={} parent={}", selectedJoint, _skeleton->joints[selectedJoint].semanticID, _skeleton->joints[selectedJoint].hierarchyDepth, _skeleton->joints[selectedJoint].parentJointIndex);
        }
    }

    void SkeletonDebugDraw::DrawBounds(const FileFormat::Animation::Bounds& bounds, Color color)
    {
        const vec3 minimum = bounds.center - bounds.extents;
        const vec3 maximum = bounds.center + bounds.extents;
        const std::array<vec3, 8> corners = {vec3(minimum.x, minimum.y, minimum.z), vec3(maximum.x, minimum.y, minimum.z), vec3(maximum.x, maximum.y, minimum.z), vec3(minimum.x, maximum.y, minimum.z), vec3(minimum.x, minimum.y, maximum.z), vec3(maximum.x, minimum.y, maximum.z), vec3(maximum.x, maximum.y, maximum.z), vec3(minimum.x, maximum.y, maximum.z)};
        constexpr std::array<std::array<u8, 2>, 12> edges = {{{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}}};
        for (const auto& edge : edges)
            EmitLine(corners[edge[0]], corners[edge[1]], color);
    }

    void SkeletonDebugDraw::DrawLabel(u64 value, const vec3& origin, const vec3& right, const vec3& up, Color color, u32 maximumDigits)
    {
        std::array<u8, 16> digits = {};
        u32 digitCount = 0;
        do
        {
            digits[digitCount++] = static_cast<u8>(value & 0xFu);
            value >>= 4u;
        } while (value != 0 && digitCount < maximumDigits);

        const f32 size = std::max(CVAR_AnimationDebugSkeletonJointRadius.GetFloat(), 0.0001f);
        const std::array<std::array<vec2, 2>, 7> segments = {{{vec2(0.0f, 2.0f), vec2(1.0f, 2.0f)}, {vec2(1.0f, 2.0f), vec2(1.0f, 1.0f)}, {vec2(1.0f, 1.0f), vec2(1.0f, 0.0f)}, {vec2(0.0f, 0.0f), vec2(1.0f, 0.0f)}, {vec2(0.0f, 1.0f), vec2(0.0f, 0.0f)}, {vec2(0.0f, 2.0f), vec2(0.0f, 1.0f)}, {vec2(0.0f, 1.0f), vec2(1.0f, 1.0f)}}};
        for (u32 outputDigit = 0; outputDigit < digitCount; ++outputDigit)
        {
            const u8 digit = digits[digitCount - outputDigit - 1];
            for (u32 segment = 0; segment < segments.size(); ++segment)
            {
                if ((HEX_SEGMENTS[digit] & (1u << segment)) == 0)
                    continue;
                const vec2 offset(static_cast<f32>(outputDigit) * 1.4f, 0.0f);
                const vec2 from2D = (segments[segment][0] + offset) * size;
                const vec2 to2D = (segments[segment][1] + offset) * size;
                EmitLine(origin + right * from2D.x + up * from2D.y, origin + right * to2D.x + up * to2D.y, color);
            }
        }
    }

    void SkeletonDebugDraw::EmitLine(const vec3& from, const vec3& to, Color color)
    {
        const u32 capacity = static_cast<u32>(std::max(CVAR_AnimationDebugSkeletonMaxLines.Get(), 0));
        if (_stats.lines >= capacity)
        {
            ++_stats.droppedLines;
            if (!_reportedCapacity)
            {
                _reportedCapacity = true;
                NC_LOG_WARNING("ANIMATION_DEBUG_SKELETON capacity_exceeded max_lines={}", capacity);
            }
            return;
        }
        _debugRenderer->DrawLine3D(from, to, color);
        ++_stats.lines;
    }

    void SkeletonDebugDraw::ReleaseSkeleton()
    {
        if (_referenceHandle && _assets) _assets->Release(_referenceHandle);
        _referenceHandle = {};
        _referencePath.clear();
        if (_familyHandle && _assets)
            _assets->Release(_familyHandle);
        _familyHandle = {};
        _familyBinding = {};
        if (_skeletonHandle && _assets)
            _assets->Release(_skeletonHandle);
        _skeletonHandle = {};
        _skeletonPathID = FileFormat::INVALID_ASSET_ID;
        _skeleton = nullptr;
        _evaluatedPose = {};
        _loadedPath.clear();
    }
} // namespace Animation
