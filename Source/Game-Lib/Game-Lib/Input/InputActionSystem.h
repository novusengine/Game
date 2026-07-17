#pragma once
#include <Base/Util/StringUtils.h>

#include <Input/InputSystem.h>

#include <robinhood/robinhood.h>

#include <array>
#include <bitset>
#include <deque>
#include <functional>
#include <filesystem>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace GameInputPriority
{
    inline constexpr i32 ImGui = 1000;
    inline constexpr i32 Modal = 900;
    inline constexpr i32 UI = 800;
    inline constexpr i32 Editor = 700;
    inline constexpr i32 Camera = 650;
    inline constexpr i32 Global = 600;
    inline constexpr i32 Gameplay = 500;
    inline constexpr i32 Debug = 100;
}

struct InputActionContextHandle
{
public:
    bool IsValid() const { return index != std::numeric_limits<u8>::max(); }
    bool operator==(const InputActionContextHandle&) const = default;

public:
    u8 index = std::numeric_limits<u8>::max();
};

struct InputActionHandle
{
public:
    bool IsValid() const { return index != std::numeric_limits<u16>::max(); }
    bool operator==(const InputActionHandle&) const = default;

public:
    u16 index = std::numeric_limits<u16>::max();
};

struct InputActionConnection
{
public:
    bool IsValid() const { return action.IsValid() && listenerID != 0; }
    bool operator==(const InputActionConnection&) const = default;

public:
    InputActionHandle action;
    u32 listenerID = 0;
};

static_assert(sizeof(InputActionContextHandle) == sizeof(u8));
static_assert(sizeof(InputActionHandle) == sizeof(u16));

struct InputActionEvent
{
public:
    InputActionHandle action;
    InputControl control;
    InputPhase phase = InputPhase::None;
    InputModifier modifiers = InputModifier::None;
    f32 value = 0.0f;
};

using InputActionCallback = std::function<InputReply(const InputActionEvent&)>;
using InputBindingCaptureCallback = std::function<void(std::optional<InputBinding>)>;

struct InputActionContextDesc
{
public:
    std::string name;
    i32 priority = GameInputPriority::Gameplay;
    i32 sortOrder = 0;
};

struct InputActionContextInfo
{
public:
    std::string name;
    i32 priority = 0;
    i32 sortOrder = 0;
};

struct InputActionDesc
{
public:
    static constexpr u32 MAX_BINDINGS = 2;

public:
    std::string name;
    std::string displayName;
    std::string category;
    std::array<std::optional<InputBinding>, MAX_BINDINGS> defaultBindings;
    InputReply defaultReply = InputReply::Consumed;
    InputActionCallback callback;
    i32 categorySortOrder = 0;
    i32 sortOrder = 0;
    bool rebindable = true;
};

struct InputActionRegistrationOptions
{
public:
    InputReply defaultReply = InputReply::Consumed;
    bool rebindable = true;
};

struct InputActionInfo
{
public:
    std::string name;
    std::string displayName;
    std::string category;
    std::array<std::optional<InputBinding>, InputActionDesc::MAX_BINDINGS> defaultBindings;
    std::array<std::optional<InputBinding>, InputActionDesc::MAX_BINDINGS> bindings;
    InputActionContextHandle context;
    InputReply defaultReply = InputReply::Consumed;
    i32 categorySortOrder = 0;
    i32 sortOrder = 0;
    bool rebindable = true;
};

enum class InputBindingConflictPolicy : u8
{
    Reject,
    Replace,
    Swap,
    Allow
};

enum class InputBindingConflictKind : u8
{
    Direct,
    ShadowsRequested,
    ShadowedByRequested
};

struct InputBindingConflict
{
public:
    InputActionHandle action;
    u32 bindingSlot = 0;
    InputBindingConflictKind kind = InputBindingConflictKind::Direct;
};

enum class InputBindingChangeStatus : u8
{
    Applied,
    AppliedWithConflicts,
    Queued,
    RejectedConflict,
    InvalidAction,
    InvalidSlot,
    InvalidBinding,
    InvalidPolicy,
    NotRebindable
};

struct InputBindingMutation
{
public:
    InputActionHandle action;
    u32 bindingSlot = 0;
    std::optional<InputBinding> previousBinding;
    std::optional<InputBinding> binding;
};

struct InputBindingChangeResult
{
public:
    bool Succeeded() const
    {
        return status == InputBindingChangeStatus::Applied || status == InputBindingChangeStatus::AppliedWithConflicts || status == InputBindingChangeStatus::Queued;
    }

public:
    InputBindingChangeStatus status = InputBindingChangeStatus::InvalidAction;
    InputBindingConflictPolicy requestedPolicy = InputBindingConflictPolicy::Reject;
    InputBindingConflictPolicy appliedPolicy = InputBindingConflictPolicy::Reject;
    InputActionHandle action;
    u32 bindingSlot = 0;
    std::optional<InputBinding> previousBinding;
    std::vector<InputBindingConflict> conflicts;
    std::vector<InputBindingMutation> mutations;
};

struct InputActionFrameMetrics
{
public:
    u32 actionEvents = 0;
    u32 listenerCallbacks = 0;
};

class InputActionSystem;

class InputActionConnections
{
public:
    explicit InputActionConnections(InputActionSystem& inputActions);
    ~InputActionConnections();

    InputActionConnections(const InputActionConnections&) = delete;
    InputActionConnections& operator=(const InputActionConnections&) = delete;
    InputActionConnections(InputActionConnections&& other) noexcept;
    InputActionConnections& operator=(InputActionConnections&& other) noexcept;

    InputActionConnection Connect(InputActionHandle action, InputActionCallback callback);
    InputActionConnection Connect(u64 actionNameHash, InputActionCallback callback);
    bool Disconnect(InputActionConnection connection);
    void DisconnectAll();

private:
    InputActionSystem* _inputActions = nullptr;
    std::vector<InputActionConnection> _connections;
};

class InputActionSystem
{
public:
    static constexpr u32 MAX_CONTEXTS = InputSystem::MAX_CONTEXTS - 1;

    explicit InputActionSystem(InputSystem& inputSystem, std::filesystem::path bindingPersistencePath = {});
    ~InputActionSystem();

    void BeginFrame();

    InputActionContextHandle CreateContext(const InputActionContextDesc& desc);
    InputActionContextHandle CreateContext(const std::string& name, i32 priority, i32 sortOrder = 0);
    InputActionContextHandle GetContext(u64 nameHash) const;
    bool SetContextActive(InputActionContextHandle context, bool active);
    bool SetContextActive(u64 contextNameHash, bool active);
    bool IsContextActive(InputActionContextHandle context) const;
    bool IsContextActive(u64 contextNameHash) const;

    InputActionHandle RegisterAction(InputActionContextHandle context, const InputActionDesc& desc);
    InputActionHandle RegisterAction(u64 contextNameHash, const InputActionDesc& desc);
    InputActionHandle RegisterAction(InputActionContextHandle context, const std::string& name, const std::string& displayName, const std::string& category, const InputBinding& binding, InputActionCallback callback = {});
    InputActionHandle RegisterAction(u64 contextNameHash, const std::string& name, const std::string& displayName, const std::string& category, const InputBinding& binding, InputActionCallback callback = {});
    InputActionHandle RegisterAction(InputActionContextHandle context, const std::string& name, const std::string& displayName, const std::string& category, const InputBinding& binding, InputActionRegistrationOptions options, InputActionCallback callback = {});
    InputActionHandle RegisterAction(u64 contextNameHash, const std::string& name, const std::string& displayName, const std::string& category, const InputBinding& binding, InputActionRegistrationOptions options, InputActionCallback callback = {});
    InputActionHandle GetAction(u64 nameHash) const;

    static std::optional<InputBinding> TryCreateBinding(u32 device, u32 code, u32 modifiers, u32 modifierMatch);
    static bool IsBindingValid(const InputBinding& binding);

    InputActionConnection Connect(InputActionHandle action, InputActionCallback callback);
    InputActionConnection Connect(u64 actionNameHash, InputActionCallback callback);
    bool Disconnect(InputActionConnection connection);
    u32 Disconnect(std::span<const InputActionConnection> connections);

    std::vector<InputBindingConflict> FindBindingConflicts(InputActionHandle action, u32 bindingSlot, const InputBinding& binding) const;
    InputBindingChangeResult SetBinding(InputActionHandle action, u32 bindingSlot, std::optional<InputBinding> binding, InputBindingConflictPolicy policy = InputBindingConflictPolicy::Reject);
    InputBindingChangeResult ResetBinding(InputActionHandle action, u32 bindingSlot, InputBindingConflictPolicy policy = InputBindingConflictPolicy::Reject);

    bool BeginBindingCapture(InputBindingCaptureCallback callback);
    bool CancelBindingCapture(bool notifyCallback = true);
    bool IsBindingCaptureActive() const { return static_cast<bool>(_bindingCaptureCallback); }

    bool LoadBindings();
    bool SaveBindings();

    bool IsDown(InputActionHandle action) const;
    bool WasPressed(InputActionHandle action) const;
    bool WasReleased(InputActionHandle action) const;

    u32 GetActionCount() const { return static_cast<u32>(_actions.size()); }
    u32 GetContextCount() const { return static_cast<u32>(_contexts.size()); }
    std::span<const InputActionHandle> GetActions() const;
    std::span<const InputActionContextHandle> GetContexts() const;
    const InputActionInfo* GetActionInfo(InputActionHandle action) const;
    const InputActionContextInfo* GetContextInfo(InputActionContextHandle context) const;

    void SetMetricsEnabled(bool enabled)
    {
        if (enabled && !_metricsEnabled)
            _frameMetrics = {};

        _metricsEnabled = enabled;
    }
    const InputActionFrameMetrics& GetFrameMetrics() const { return _frameMetrics; }

private:
    struct BindingReference
    {
    public:
        u16 actionIndex = 0;
        u8 bindingSlot = 0;
    };

    struct Context
    {
    public:
        InputActionContextInfo info;
        InputContextHandle inputContext;
        std::array<std::vector<BindingReference>, INPUT_CONTROL_COUNT> bindingsByControl;
        vec2 scrollRemainder = vec2(0.0f);
        u32 creationOrder = 0;
    };

    struct Listener
    {
    public:
        InputActionCallback callback;
        u32 id = 0;
        bool active = true;
    };

    struct Action
    {
    public:
        InputActionInfo info;
        std::vector<Listener> listeners;
        std::array<bool, InputActionDesc::MAX_BINDINGS> bindingDown = {};
        u32 lastDispatchSerial = 0;
        u8 downBindingCount = 0;
        bool pressed = false;
        bool released = false;
        bool transientMarked = false;
    };

    struct PendingConnection
    {
    public:
        InputActionHandle action;
        InputActionCallback callback;
        u32 listenerID = 0;
        u32 order = 0;
        bool active = true;
    };

    struct PendingBindingChange
    {
    public:
        InputActionHandle action;
        u32 bindingSlot = 0;
        std::optional<InputBinding> binding;
        InputBindingConflictPolicy policy = InputBindingConflictPolicy::Reject;
        u32 order = 0;
    };

    struct PendingContextState
    {
    public:
        InputActionContextHandle context;
        u32 order = 0;
        bool active = false;
    };

    struct PendingBindingReference
    {
    public:
        InputActionHandle action;
        u32 bindingSlot = 0;
        InputBinding binding;
        u32 order = 0;
    };

    struct PersistedBindingOverride
    {
    public:
        std::optional<InputBinding> binding;
        bool present = false;
    };

    using PersistedActionOverrides = std::array<PersistedBindingOverride, InputActionDesc::MAX_BINDINGS>;

    bool IsContextValid(InputActionContextHandle context) const;
    bool IsActionValid(InputActionHandle action) const;
    static bool BindingModifiersOverlap(const InputBinding& left, const InputBinding& right);
    bool ContextDispatchesBefore(InputActionContextHandle left, InputActionContextHandle right) const;
    void RebuildSortedActions() const;
    void RebuildSortedContexts() const;
    void AddBindingReference(InputActionHandle action, u32 bindingSlot, const InputBinding& binding);
    void RemoveBindingReference(InputActionHandle action, u32 bindingSlot, const InputBinding& binding);
    void SetBindingDirect(InputActionHandle action, u32 bindingSlot, std::optional<InputBinding> binding, InputBindingChangeResult& result);
    InputBindingChangeResult ApplyBindingChange(InputActionHandle action, u32 bindingSlot, std::optional<InputBinding> binding, InputBindingConflictPolicy policy);
    u32 NextDeferredMutationOrder();
    void FlushDeferredMutations();
    void MarkTransient(u16 actionIndex);
    InputReply HandleContextEvent(InputActionContextHandle context, const InputEvent& event);
    InputReply HandleButtonEvent(InputActionContextHandle context, const InputEvent& event);
    InputReply HandleScrollEvent(InputActionContextHandle context, const InputEvent& event);
    bool HasMatchingBinding(const Context& context, InputControl control, InputModifier modifiers) const;
    InputReply DispatchScrollDirection(Context& context, InputControl control, InputModifier modifiers, f32 value, bool triggered);
    InputReply HandleActionEvent(Action& action, InputActionHandle handle, u32 bindingSlot, const InputEvent& event, f32 value);
    InputReply HandleBindingCaptureEvent(const InputEvent& event);
    void FinishBindingCapture(std::optional<InputBinding> binding, bool notifyCallback);
    void ApplyPersistedBindings(InputActionHandle action);

private:
    InputSystem& _inputSystem;
    InputContextHandle _bindingCaptureContext;
    InputBindingCaptureCallback _bindingCaptureCallback;
    std::optional<InputControl> _captureReleaseControl;
    std::bitset<INPUT_CONTROL_COUNT> _captureIgnoredControls;
    std::filesystem::path _bindingPersistencePath;
    robin_hood::unordered_map<std::string, PersistedActionOverrides> _persistedBindingOverrides;
    std::deque<Context> _contexts;
    std::deque<Action> _actions;
    mutable std::vector<InputActionHandle> _sortedActions;
    mutable std::vector<InputActionContextHandle> _sortedContexts;
    std::vector<u16> _transientActions;
    std::vector<PendingConnection> _pendingConnections;
    std::vector<PendingBindingChange> _pendingBindingChanges;
    std::vector<PendingContextState> _pendingContextStates;
    std::vector<PendingBindingReference> _pendingBindingReferences;
    robin_hood::unordered_map<u64, u16> _actionHashToIndex;
    robin_hood::unordered_map<u64, u8> _contextHashToIndex;
    u32 _nextListenerID = 1;
    u32 _nextContextCreationOrder = 0;
    u32 _dispatchSerial = 0;
    u32 _nextDeferredMutationOrder = 1;
    u32 _callbackDispatchDepth = 0;
    bool _listenersNeedCleanup = false;
    bool _flushingDeferredMutations = false;
    bool _metricsEnabled = false;
    mutable bool _sortedActionsDirty = false;
    mutable bool _sortedContextsDirty = false;
    InputActionFrameMetrics _frameMetrics;
};
