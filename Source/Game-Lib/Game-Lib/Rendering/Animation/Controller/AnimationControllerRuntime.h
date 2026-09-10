#pragma once
#include "Game-Lib/Rendering/Animation/Controller/AnimationSetResolver.h"
#include "Game-Lib/Rendering/Animation/Controller/AnimationBlendSpace.h"

#include <array>
#include <memory>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Animation
{
    enum class GraphComparison : u8
    {
        Equal,
        NotEqual,
        Less,
        LessEqual,
        Greater,
        GreaterEqual
    };

    enum class BlendCurve : u8
    {
        Linear,
        SmoothStep
    };

    enum class GraphStateKind : u8
    {
        Clip,
        BlendSpace1D,
        BlendSpace2D
    };

    enum class AnimationLayerType : u8
    {
        Override,
        Additive
    };

    struct EditableGraphBlendSample
    {
        vec2 position = {};
        u64 clipSlotSemanticID = 0;
        std::string displayName;
    };

    struct EditableGraphParameter
    {
        std::string name;
        FileFormat::Animation::GraphValueType type = FileFormat::Animation::GraphValueType::Float;
        f32 defaultFloat = 0.0f;
        i32 defaultInt = 0;
        bool defaultBool = false;
        bool oneFrameTrigger = false;
        vec3 defaultFloat3 = {};
        u64 sourceGUID = 0;
        u16 stateVersion = 1;
    };

    struct EditableGraphState
    {
        u64 sourceGUID = 0;
        u64 clipSlotSemanticID = 0;
        u64 durationMicroseconds = 0;
        GraphStateKind kind = GraphStateKind::Clip;
        std::string inputX;
        std::string inputY;
        f32 smoothingHalfLifeSeconds = 0.0f;
        bool wrapX = false;
        f32 wrapMinimum = 0.0f;
        f32 wrapMaximum = 0.0f;
        std::vector<EditableGraphBlendSample> blendSamples;
        std::vector<BlendSpaceTriangle> blendTriangles;
        std::string displayName;
        vec2 editorPosition = {};
        u16 stateVersion = 1;
    };

    struct EditableGraphTransition
    {
        u64 sourceGUID = 0;
        u64 targetGUID = 0;
        std::string parameter;
        GraphComparison comparison = GraphComparison::Greater;
        f32 threshold = 0.0f;
        u64 durationMicroseconds = 0;
        BlendCurve curve = BlendCurve::Linear;
    };

    struct EditableGraphLayer
    {
        u64 sourceGUID = 0;
        u64 clipSlotSemanticID = 0;
        FileFormat::AssetID maskAssetID = FileFormat::INVALID_ASSET_ID;
        u64 durationMicroseconds = 0;
        AnimationLayerType type = AnimationLayerType::Override;
        std::string weightParameter;
        f32 constantWeight = 1.0f;
        u16 executionOrder = 0;
        std::string displayName;
        bool emitEvents = false;
        bool composeInSourceDomain = false;
    };

    struct EditableGraphActionSlot
    {
        u64 semanticID = 0;
        u64 clipSlotSemanticID = 0;
        FileFormat::AssetID maskAssetID = FileFormat::INVALID_ASSET_ID;
        u64 durationMicroseconds = 0;
        u64 blendInMicroseconds = 0;
        u64 blendOutMicroseconds = 0;
        AnimationLayerType type = AnimationLayerType::Override;
        u16 priority = 0;
        bool queueWhenBlocked = true;
        bool continueUnderlyingState = true;
        std::string displayName;
    };

    struct EditableGraphOutput
    {
        u64 semanticID = 0;
        std::string parameter;
    };

    struct EditableGraphIKOperation
    {
        u64 sourceGUID = 0;
        FileFormat::AssetID rigAssetID = FileFormat::INVALID_ASSET_ID;
        u64 solverSemanticID = 0;
        std::string goalParameter;
        u64 goalAttachmentSemanticID = 0;
        u64 effectorAttachmentSemanticID = 0;
        vec3 effectorAlignmentAxis = {};
        std::string weightParameter;
        f32 constantWeight = 1.0f;
        u16 executionOrder = 0;
        std::string displayName;
    };

    struct EditableAnimationGraph
    {
        static constexpr u16 CURRENT_VERSION = 4;
        u16 version = CURRENT_VERSION;
        std::vector<EditableGraphParameter> parameters;
        std::vector<EditableGraphState> states;
        std::vector<EditableGraphTransition> transitions;
        std::vector<EditableGraphLayer> layers;
        std::vector<EditableGraphActionSlot> actionSlots;
        std::vector<EditableGraphOutput> outputs;
        std::vector<EditableGraphIKOperation> ikOperations;
        u64 entryStateGUID = 0;
        u32 maximumSnapshotJoints = 256;
        u32 maximumPoseDomains = 4;
        bool retargetChainCorrection = false;
        u32 commandCapacity = 8;
        u32 actionCapacity = 8;
        std::string displayName;
    };

    struct CompiledGraphParameter
    {
        u64 nameHash = 0;
        u32 byteOffset = 0;
        FileFormat::Animation::GraphValueType type = FileFormat::Animation::GraphValueType::Float;
        bool oneFrameTrigger = false;
        u64 sourceGUID = 0;
        u16 stateVersion = 1;
    };

    struct CompiledGraphState
    {
        u64 sourceGUID = 0;
        u64 clipSlotSemanticID = 0;
        u64 durationMicroseconds = 0;
        GraphStateKind kind = GraphStateKind::Clip;
        u16 inputXParameter = std::numeric_limits<u16>::max();
        u16 inputYParameter = std::numeric_limits<u16>::max();
        u16 blendSampleOffset = 0;
        u16 blendSampleCount = 0;
        u16 blendTriangleOffset = 0;
        u16 blendTriangleCount = 0;
        f32 smoothingHalfLifeSeconds = 0.0f;
        bool wrapX = false;
        f32 wrapMinimum = 0.0f;
        f32 wrapMaximum = 0.0f;
        u16 stateVersion = 1;
    };

    struct CompiledGraphBlendSample
    {
        vec2 position = {};
        u64 clipSlotSemanticID = 0;
    };

    struct CompiledGraphTransition
    {
        u16 sourceState = 0;
        u16 targetState = 0;
        u16 parameterIndex = 0;
        GraphComparison comparison = GraphComparison::Greater;
        BlendCurve curve = BlendCurve::Linear;
        f32 threshold = 0.0f;
        u64 durationMicroseconds = 0;
    };

    struct CompiledGraphLayer
    {
        u64 sourceGUID = 0;
        u64 clipSlotSemanticID = 0;
        FileFormat::AssetID maskAssetID = FileFormat::INVALID_ASSET_ID;
        u64 durationMicroseconds = 0;
        AnimationLayerType type = AnimationLayerType::Override;
        u16 weightParameter = std::numeric_limits<u16>::max();
        f32 constantWeight = 1.0f;
        u16 executionOrder = 0;
        bool emitEvents = false;
        bool composeInSourceDomain = false;
    };

    struct CompiledGraphActionSlot
    {
        u64 semanticID = 0;
        u64 clipSlotSemanticID = 0;
        FileFormat::AssetID maskAssetID = FileFormat::INVALID_ASSET_ID;
        u64 durationMicroseconds = 0;
        u64 blendInMicroseconds = 0;
        u64 blendOutMicroseconds = 0;
        AnimationLayerType type = AnimationLayerType::Override;
        u16 priority = 0;
        bool queueWhenBlocked = true;
        bool continueUnderlyingState = true;
    };

    struct CompiledGraphOutput
    {
        u64 semanticID = 0;
        u16 parameterIndex = 0;
    };

    struct CompiledGraphIKOperation
    {
        u64 sourceGUID = 0;
        FileFormat::AssetID rigAssetID = FileFormat::INVALID_ASSET_ID;
        u64 solverSemanticID = 0;
        u16 goalParameter = std::numeric_limits<u16>::max();
        u64 goalAttachmentSemanticID = 0;
        u64 effectorAttachmentSemanticID = 0;
        vec3 effectorAlignmentAxis = {};
        u16 weightParameter = std::numeric_limits<u16>::max();
        f32 constantWeight = 1.0f;
        u16 executionOrder = 0;
    };

    struct CompiledAnimationGraph
    {
        u64 contentHash = 0;
        u32 parameterBytes = 0;
        u32 maximumSnapshotJoints = 0;
        u32 maximumPoseDomains = 4;
        bool retargetChainCorrection = false;
        u32 commandCapacity = 0;
        u32 actionCapacity = 0;
        u16 entryState = 0;
        std::vector<u8> defaultParameters;
        std::vector<CompiledGraphParameter> parameters;
        std::vector<CompiledGraphState> states;
        std::vector<CompiledGraphBlendSample> blendSamples;
        std::vector<BlendSpaceTriangle> blendTriangles;
        std::vector<CompiledGraphTransition> transitions;
        std::vector<CompiledGraphLayer> layers;
        std::vector<CompiledGraphActionSlot> actionSlots;
        std::vector<CompiledGraphOutput> outputs;
        std::vector<CompiledGraphIKOperation> ikOperations;
        std::vector<u8> controllerProgram;
    };

    struct GraphCompileDiagnostic
    {
        std::string field;
        u32 index = std::numeric_limits<u32>::max();
        std::string message;
        bool operator==(const GraphCompileDiagnostic&) const = default;
    };

    struct GraphCompileResult
    {
        std::shared_ptr<const CompiledAnimationGraph> graph;
        std::vector<GraphCompileDiagnostic> diagnostics;
        explicit operator bool() const { return graph != nullptr && diagnostics.empty(); }
    };

    // Compiles editable graph data into the compact representation consumed by controller instances.
    // Compilation reports authored-data errors before a graph reaches runtime evaluation.
    u64 HashAnimationName(std::string_view value);
    GraphCompileResult CompileAnimationGraph(const EditableAnimationGraph& source);

    class AnimatorHandle
    {
      public:
        constexpr AnimatorHandle() = default;
        explicit constexpr operator bool() const { return _value != 0; }
        constexpr bool operator==(const AnimatorHandle&) const = default;
        constexpr u64 GetRawValue() const { return _value; }

      private:
        friend class AnimationControllerStore;
        explicit constexpr AnimatorHandle(u64 value) : _value(value) {}
        constexpr u32 GetSlot() const { return static_cast<u32>(_value) - 1u; }
        constexpr u32 GetGeneration() const { return static_cast<u32>(_value >> 32u); }
        static constexpr AnimatorHandle Create(u32 slot, u32 generation) { return AnimatorHandle((static_cast<u64>(generation) << 32u) | (static_cast<u64>(slot) + 1u)); }
        u64 _value = 0;
    };

    enum class AnimationActionStatus : u8
    {
        Pending,
        Playing,
        Completed,
        Interrupted,
        Cancelled
    };

    enum class AnimationSeekEventPolicy : u8
    {
        ResetWithoutReplay = 0
    };

    struct AnimationActionToken
    {
        AnimatorHandle animator;
        u32 serial = 0;
        explicit operator bool() const { return animator && serial != 0; }
    };

    struct AnimationParameterHandle
    {
        AnimatorHandle animator;
        u32 graphGeneration = 0;
        u16 parameterIndex = 0;
        FileFormat::Animation::GraphValueType type = FileFormat::Animation::GraphValueType::Float;
        explicit operator bool() const { return animator && graphGeneration != 0; }
    };

    struct AnimationControllerConfig
    {
        FileFormat::AssetID targetSkeletonAssetID = FileFormat::INVALID_ASSET_ID;
        FileFormat::AssetID graphAssetID = FileFormat::INVALID_ASSET_ID;
        std::shared_ptr<const CompiledAnimationGraph> graph;
        u64 stableSeed = 0;
    };

    struct AnimationControllerOutput
    {
        u64 semanticID = 0;
        u32 payload = 0;
    };

    struct AnimationEventOutput
    {
        u32 eventTypeID = 0;
        u32 payloadTypeID = 0;
        u32 flags = 0;
        std::vector<u8> payload;
    };

    struct AnimationGraphOutput
    {
        u64 semanticID = 0;
        FileFormat::Animation::GraphValueType type = FileFormat::Animation::GraphValueType::Float;
        vec4 value = {};
    };

    struct PoseRecipeSample
    {
        FileFormat::AssetID clipAssetID = FileFormat::INVALID_ASSET_ID;
        u64 timeMicroseconds = 0;
        f32 weight = 0.0f;
        u32 stableVariationID = 0;
    };

    struct PoseRecipeLayer
    {
        PoseRecipeSample sample;
        FileFormat::AssetID maskAssetID = FileFormat::INVALID_ASSET_ID;
        f32 weight = 0.0f;
        AnimationLayerType type = AnimationLayerType::Override;
        u16 executionOrder = 0;
        bool composeInSourceDomain = false;
    };

    struct PoseRecipeIKOperation
    {
        FileFormat::AssetID rigAssetID = FileFormat::INVALID_ASSET_ID;
        u64 solverSemanticID = 0;
        vec3 goalModel = {};
        u64 goalAttachmentSemanticID = 0;
        u64 effectorAttachmentSemanticID = 0;
        vec3 effectorAlignmentAxis = {};
        f32 weight = 0.0f;
        u16 executionOrder = 0;
    };

    struct PoseRecipe
    {
        std::array<PoseRecipeSample, 6> samples = {};
        std::array<PoseRecipeLayer, 4> layers = {};
        std::array<PoseRecipeIKOperation, 4> ikOperations = {};
        u32 sampleCount = 0;
        u32 layerCount = 0;
        u32 ikOperationCount = 0;
        u16 sourceState = 0;
        u16 targetState = 0;
        f32 transitionWeight = 0.0f;
        bool usesSnapshot = false;
    };

    struct AnimationControllerState
    {
        u16 currentState = 0;
        u16 transitionTarget = 0;
        u64 stateTimeMicroseconds = 0;
        u64 transitionTimeMicroseconds = 0;
        u64 transitionDurationMicroseconds = 0;
        bool transitioning = false;
        bool hasContinuitySnapshot = false;
        u32 activeBlendSamples = 0;
        u32 activeBlendTriangle = std::numeric_limits<u32>::max();
        std::array<f32, 3> activeBlendWeights = {};
        std::array<f32, 3> synchronizationPlaybackRates = {1.0f, 1.0f, 1.0f};
        FileFormat::AssetID synchronizationLeader = FileFormat::INVALID_ASSET_ID;
        u64 previousSynchronizationMarker = 0;
        u64 nextSynchronizationMarker = 0;
        u64 requestedSlotSemanticID = 0;
        u64 resolvedSlotSemanticID = 0;
        FileFormat::AssetID resolvedClipAssetID = FileFormat::INVALID_ASSET_ID;
        u32 resolvedVariationID = 0;
        u32 resolvedFallbackDepth = 0;
        AnimationSlotResolution slotResolution = AnimationSlotResolution::MissingSlot;
        bool synchronizationFallback = false;
        u32 activeLayers = 0;
        std::array<f32, 4> activeLayerWeights = {};
        u32 activeIKOperations = 0;
        u64 activeActionSemanticID = 0;
        u64 activeActionTimeMicroseconds = 0;
        AnimationActionStatus activeActionStatus = AnimationActionStatus::Completed;
    };

    struct AnimationControllerStats
    {
        u32 liveControllers = 0;
        u32 staleHandleRejects = 0;
        u64 advances = 0;
        u64 poseRequests = 0;
        u64 transitions = 0;
        u64 interruptions = 0;
        u64 snapshots = 0;
        u64 rejectedWrites = 0;
        u64 queueOverflows = 0;
        u64 authoritativeEventOverflows = 0;
        u64 cosmeticEventDrops = 0;
        u64 reentrantMutationRejects = 0;
    };

    struct AnimationControllerBatchInput
    {
        AnimatorHandle animator;
        AnimationParameterHandle speed;
        AnimationParameterHandle grounded;
        AnimationParameterHandle direction;
        f32 speedValue = 0.0f;
        f32 directionValue = 0.0f;
        bool groundedValue = false;
    };

    // Describes what survived a graph publication and what was reset.
    struct AnimationGraphMigration
    {
        u32 parametersPreserved = 0;
        u32 actionsPreserved = 0;
        u32 actionsInterrupted = 0;
        bool statePreserved = false;
        bool transitionPreserved = false;
    };

    // Owns generation-checked animation graph instances, parameters, and transition state.
    // Fixed-step evaluation produces deterministic pose recipes without sampling clips directly.
    class AnimationControllerStore
    {
      public:
        AnimationControllerStore();
        ~AnimationControllerStore();
        AnimatorHandle Create(const AnimationControllerConfig& config);
        bool Destroy(AnimatorHandle handle);
        AnimationParameterHandle ResolveParameter(AnimatorHandle handle, u64 nameHash) const;
        bool SetBool(AnimationParameterHandle handle, bool value);
        bool SetInt(AnimationParameterHandle handle, i32 value);
        bool SetFloat(AnimationParameterHandle handle, f32 value);
        bool SetFloat3(AnimationParameterHandle handle, const vec3& value);
        bool Trigger(AnimationParameterHandle handle);
        bool BindAnimationData(AnimatorHandle handle, const AnimationLoading::AnimationSetAssetView& set, std::span<const AnimationClipDomainRecord> clips);
        bool QueueCommand(AnimatorHandle handle, u64 semanticID, u32 payload = 0);
        bool QueueAction(AnimatorHandle handle, u64 semanticID, u32 payload = 0);
        AnimationActionToken StartAction(AnimatorHandle handle, u64 semanticID, u32 payload = 0);
        bool CancelAction(AnimationActionToken token);
        bool GetActionStatus(AnimationActionToken token, AnimationActionStatus& status) const;
        u32 ApplyBatchInputs(std::span<const AnimationControllerBatchInput> inputs);
        void Advance(f64 deltaSeconds, bool applyTimeScale = true);
        bool Seek(AnimatorHandle handle, u64 timeMicroseconds, AnimationSeekEventPolicy eventPolicy = AnimationSeekEventPolicy::ResetWithoutReplay);
        PoseRecipe RequestPose(AnimatorHandle handle, const AnimationLoading::AnimationSetAssetView& set, std::span<const AnimationClipDomainRecord> clips, std::span<const FileFormat::Animation::LocalTransform> continuityPose = {});
        bool GetState(AnimatorHandle handle, AnimationControllerState& state) const;
        bool GetParameterValues(AnimatorHandle handle, std::vector<vec3>& values) const;
        bool GetBoundsMetadata(AnimatorHandle handle, const AnimationLoading::AnimationSetAssetView& set, std::span<const AnimationClipDomainRecord> clips, FileFormat::AssetID& clipAssetID, u64& timeMicroseconds, bool& supportsTightBounds);
        std::span<const AnimationControllerOutput> GetOutputs(AnimatorHandle handle) const;
        std::span<const AnimationEventOutput> GetAuthoritativeEvents(AnimatorHandle handle) const;
        std::span<const AnimationEventOutput> GetCosmeticEvents(AnimatorHandle handle) const;
        std::span<const AnimationGraphOutput> GetGraphOutputs(AnimatorHandle handle) const;
        void ClearOutputs(AnimatorHandle handle);
        bool ReplaceGraph(AnimatorHandle handle, FileFormat::AssetID graphAssetID, std::shared_ptr<const CompiledAnimationGraph> graph, AnimationGraphMigration* migration = nullptr, bool compatibleResources = true);
        AnimationControllerStats GetStats() const;

      private:
        struct Slot;
        Slot* GetSlot(AnimatorHandle handle);
        const Slot* GetSlot(AnimatorHandle handle) const;
        bool ValidateParameter(const AnimationParameterHandle& handle, FileFormat::Animation::GraphValueType type, Slot*& slot);
        std::vector<Slot> _slots;
        std::vector<u32> _freeSlots;
        mutable AnimationControllerStats _stats;
        f64 _accumulatorSeconds = 0.0;
        bool _updating = false;
    };
} // namespace Animation
