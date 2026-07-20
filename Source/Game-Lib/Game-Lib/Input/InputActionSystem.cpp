#include "InputActionSystem.h"

#include <Base/Util/DebugHandler.h>

#include <json/json.hpp>
#include <xxhash/xxhash64.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace
{
    constexpr u32 INPUT_BINDINGS_VERSION = 1;

    InputReply MaxReply(InputReply left, InputReply right)
    {
        return static_cast<u8>(left) >= static_cast<u8>(right) ? left : right;
    }

    bool IsModifierKey(Key key)
    {
        return key == Key::LeftShift || key == Key::RightShift
            || key == Key::LeftControl || key == Key::RightControl
            || key == Key::LeftAlt || key == Key::RightAlt
            || key == Key::LeftSuper || key == Key::RightSuper;
    }

    std::optional<InputBinding> ReadPersistedBinding(const nlohmann::json& value)
    {
        if (!value.is_object())
            return std::nullopt;

        auto deviceValue = value.find("device");
        auto codeValue = value.find("code");
        auto modifiersValue = value.find("modifiers");
        auto modifierMatchValue = value.find("modifierMatch");
        if (deviceValue == value.end() || !deviceValue->is_number_unsigned()
            || codeValue == value.end() || !codeValue->is_number_unsigned()
            || modifiersValue == value.end() || !modifiersValue->is_number_unsigned()
            || modifierMatchValue == value.end() || !modifierMatchValue->is_number_unsigned())
        {
            return std::nullopt;
        }

        const u32 device = deviceValue->get<u32>();
        const u32 code = codeValue->get<u32>();
        const u32 modifiers = modifiersValue->get<u32>();
        const u32 modifierMatch = modifierMatchValue->get<u32>();
        return InputActionSystem::TryCreateBinding(device, code, modifiers, modifierMatch);
    }

    nlohmann::ordered_json WritePersistedBinding(const InputBinding& binding)
    {
        return {
            { "device", static_cast<u32>(binding.control.device) },
            { "code", static_cast<u32>(binding.control.code) },
            { "modifiers", static_cast<u32>(binding.modifiers) },
            { "modifierMatch", static_cast<u32>(binding.modifierMatch) }
        };
    }

    bool ReplaceFile(const std::filesystem::path& temporaryPath, const std::filesystem::path& destinationPath)
    {
#if defined(_WIN32)
        return MoveFileExW(temporaryPath.c_str(), destinationPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
        std::error_code error;
        std::filesystem::rename(temporaryPath, destinationPath, error);
        return !error;
#endif
    }
}

InputActionConnections::InputActionConnections(InputActionSystem& inputActions) : _inputActions(&inputActions)
{
    _connections.reserve(8);
}

InputActionConnections::~InputActionConnections()
{
    DisconnectAll();
}

InputActionConnections::InputActionConnections(InputActionConnections&& other) noexcept
    : _inputActions(std::exchange(other._inputActions, nullptr)), _connections(std::move(other._connections))
{
}

InputActionConnections& InputActionConnections::operator=(InputActionConnections&& other) noexcept
{
    if (this == &other)
        return *this;

    DisconnectAll();
    _inputActions = std::exchange(other._inputActions, nullptr);
    _connections = std::move(other._connections);
    return *this;
}

InputActionConnection InputActionConnections::Connect(InputActionHandle action, InputActionCallback callback)
{
    if (!_inputActions)
        return {};

    InputActionConnection connection = _inputActions->Connect(action, std::move(callback));
    if (connection.IsValid())
        _connections.push_back(connection);

    return connection;
}

InputActionConnection InputActionConnections::Connect(u64 actionNameHash, InputActionCallback callback)
{
    if (!_inputActions)
        return {};

    InputActionConnection connection = _inputActions->Connect(actionNameHash, std::move(callback));
    if (connection.IsValid())
        _connections.push_back(connection);

    return connection;
}

bool InputActionConnections::Disconnect(InputActionConnection connection)
{
    if (!_inputActions)
        return false;

    auto existing = std::find(_connections.begin(), _connections.end(), connection);
    if (existing == _connections.end())
        return false;

    const bool disconnected = _inputActions->Disconnect(connection);
    _connections.erase(existing);
    return disconnected;
}

void InputActionConnections::DisconnectAll()
{
    if (_inputActions)
        _inputActions->Disconnect(_connections);

    _connections.clear();
}

InputActionSystem::InputActionSystem(InputSystem& inputSystem, std::filesystem::path bindingPersistencePath)
    : _inputSystem(inputSystem), _bindingPersistencePath(std::move(bindingPersistencePath))
{
    _sortedContexts.reserve(16);
    _sortedActions.reserve(1024);
    _transientActions.reserve(1024);
    _pendingConnections.reserve(16);
    _pendingBindingChanges.reserve(16);
    _pendingContextStates.reserve(8);
    _pendingBindingReferences.reserve(16);

    _bindingCaptureContext = _inputSystem.CreateContext("BindingCapture", GameInputPriority::Modal, [this](const InputEvent& event)
    {
        return HandleBindingCaptureEvent(event);
    });

    if (!_bindingPersistencePath.empty())
        LoadBindings();
}

InputActionSystem::~InputActionSystem()
{
    _bindingCaptureCallback = {};

    for (Action& action : _actions)
    {
        action.listeners.clear();
    }

    if (_bindingCaptureContext.IsValid())
        _inputSystem.DestroyContext(_bindingCaptureContext);

    for (Context& context : _contexts)
    {
        _inputSystem.DestroyContext(context.inputContext);
    }
}

void InputActionSystem::BeginFrame()
{
    for (u16 actionIndex : _transientActions)
    {
        Action& action = _actions[actionIndex];
        action.pressed = false;
        action.released = false;
        action.transientMarked = false;
    }

    _transientActions.clear();

    if (_metricsEnabled)
        _frameMetrics = {};
}

InputActionContextHandle InputActionSystem::CreateContext(const InputActionContextDesc& desc)
{
    if (desc.name.empty())
        return {};

    const u64 nameHash = XXHash64::hash(desc.name.c_str(), desc.name.length(), 0);
    auto existingContext = _contextHashToIndex.find(nameHash);
    if (existingContext != _contextHashToIndex.end())
    {
        NC_LOG_CRITICAL("InputActionSystem: Cannot create context '{}'; hash is already used by context '{}'", desc.name, _contexts[existingContext->second].info.name);
        return {};
    }

    if (_contexts.size() >= MAX_CONTEXTS)
    {
        NC_LOG_CRITICAL("InputActionSystem: Cannot create context '{}'; the action context capacity has been reached", desc.name);
        return {};
    }

    const InputActionContextHandle handle = { static_cast<u8>(_contexts.size()) };
    Context& context = _contexts.emplace_back();
    context.info.name = desc.name;
    context.info.priority = desc.priority;
    context.info.sortOrder = desc.sortOrder;
    context.creationOrder = _nextContextCreationOrder++;
    context.inputContext = _inputSystem.CreateContext(desc.name, desc.priority, [this, handle](const InputEvent& event)
    {
        return HandleContextEvent(handle, event);
    });

    if (!context.inputContext.IsValid())
    {
        _contexts.pop_back();
        return {};
    }

    _contextHashToIndex[nameHash] = handle.index;
    _sortedContexts.push_back(handle);
    _sortedContextsDirty = true;

    return handle;
}

InputActionContextHandle InputActionSystem::CreateContext(const std::string& name, i32 priority, i32 sortOrder)
{
    return CreateContext({ name, priority, sortOrder });
}

InputActionContextHandle InputActionSystem::GetContext(u64 nameHash) const
{
    auto context = _contextHashToIndex.find(nameHash);
    if (context == _contextHashToIndex.end())
        return {};

    return { context->second };
}

bool InputActionSystem::SetContextActive(InputActionContextHandle context, bool active)
{
    if (!IsContextValid(context))
        return false;

    if (_callbackDispatchDepth != 0)
    {
        _pendingContextStates.push_back({ context, NextDeferredMutationOrder(), active });
        return true;
    }

    if (!active)
        _contexts[context.index].scrollRemainder = vec2(0.0f);

    return _inputSystem.SetContextActive(_contexts[context.index].inputContext, active);
}

bool InputActionSystem::SetContextActive(u64 contextNameHash, bool active)
{
    return SetContextActive(GetContext(contextNameHash), active);
}

bool InputActionSystem::IsContextActive(InputActionContextHandle context) const
{
    return IsContextValid(context) && _inputSystem.IsContextActive(_contexts[context.index].inputContext);
}

bool InputActionSystem::IsContextActive(u64 contextNameHash) const
{
    return IsContextActive(GetContext(contextNameHash));
}

InputActionHandle InputActionSystem::RegisterAction(InputActionContextHandle context, const InputActionDesc& desc)
{
    if (!IsContextValid(context) || desc.name.empty())
        return {};

    const u64 nameHash = XXHash64::hash(desc.name.c_str(), desc.name.length(), 0);
    auto existingAction = _actionHashToIndex.find(nameHash);
    if (existingAction != _actionHashToIndex.end())
    {
        const Action& action = _actions[existingAction->second];
        NC_LOG_CRITICAL("InputActionSystem: Cannot register action '{}'; hash is already used by action '{}'", desc.name, action.info.name);
        return {};
    }

    if (_actions.size() >= std::numeric_limits<u16>::max())
    {
        NC_LOG_CRITICAL("InputActionSystem: Cannot register action '{}'; the action capacity has been reached", desc.name);
        return {};
    }

    const InputActionHandle handle = { static_cast<u16>(_actions.size()) };
    Action& action = _actions.emplace_back();
    action.info.name = desc.name;
    action.info.displayName = desc.displayName.empty() ? action.info.name : desc.displayName;
    action.info.category = desc.category;
    action.info.defaultBindings = desc.defaultBindings;
    action.info.bindings = desc.defaultBindings;
    action.info.context = context;
    action.info.defaultReply = desc.defaultReply;
    action.info.categorySortOrder = desc.categorySortOrder;
    action.info.sortOrder = desc.sortOrder;
    action.info.rebindable = desc.rebindable;

    if (desc.callback)
    {
        const u32 listenerID = _nextListenerID++;
        if (_nextListenerID == 0)
            _nextListenerID = 1;

        action.listeners.push_back({ desc.callback, listenerID, true });
    }

    _actionHashToIndex[nameHash] = handle.index;
    _sortedActions.push_back(handle);
    _sortedActionsDirty = true;

    for (u32 bindingSlot = 0; bindingSlot < InputActionDesc::MAX_BINDINGS; bindingSlot++)
    {
        if (!action.info.bindings[bindingSlot])
            continue;

        if (!IsBindingValid(*action.info.bindings[bindingSlot]))
        {
            NC_LOG_ERROR("InputActionSystem: Action '{}' has an invalid default binding in slot {}", desc.name, bindingSlot);
            action.info.defaultBindings[bindingSlot].reset();
            action.info.bindings[bindingSlot].reset();
        }
    }

    ApplyPersistedBindings(handle);

    for (u32 bindingSlot = 0; bindingSlot < InputActionDesc::MAX_BINDINGS; bindingSlot++)
    {
        if (!action.info.bindings[bindingSlot])
            continue;

        if (_callbackDispatchDepth != 0)
        {
            _pendingBindingReferences.push_back({ handle, bindingSlot, *action.info.bindings[bindingSlot], NextDeferredMutationOrder() });
        }
        else
        {
            AddBindingReference(handle, bindingSlot, *action.info.bindings[bindingSlot]);
        }
    }

    return handle;
}

InputActionHandle InputActionSystem::RegisterAction(InputActionContextHandle context, const std::string& name, const std::string& displayName, const std::string& category, const InputBinding& binding, InputActionCallback callback)
{
    return RegisterAction(context, name, displayName, category, binding, {}, std::move(callback));
}

InputActionHandle InputActionSystem::RegisterAction(InputActionContextHandle context, const std::string& name, const std::string& displayName, const std::string& category, const InputBinding& binding, InputActionRegistrationOptions options, InputActionCallback callback)
{
    InputActionDesc desc;
    desc.name = name;
    desc.displayName = displayName;
    desc.category = category;
    desc.defaultBindings[0] = binding;
    desc.defaultReply = options.defaultReply;
    desc.callback = std::move(callback);
    desc.rebindable = options.rebindable;

    return RegisterAction(context, desc);
}

InputActionHandle InputActionSystem::RegisterAction(u64 contextNameHash, const InputActionDesc& desc)
{
    return RegisterAction(GetContext(contextNameHash), desc);
}

InputActionHandle InputActionSystem::RegisterAction(u64 contextNameHash, const std::string& name, const std::string& displayName, const std::string& category, const InputBinding& binding, InputActionCallback callback)
{
    return RegisterAction(GetContext(contextNameHash), name, displayName, category, binding, std::move(callback));
}

InputActionHandle InputActionSystem::RegisterAction(u64 contextNameHash, const std::string& name, const std::string& displayName, const std::string& category, const InputBinding& binding, InputActionRegistrationOptions options, InputActionCallback callback)
{
    return RegisterAction(GetContext(contextNameHash), name, displayName, category, binding, options, std::move(callback));
}

InputActionHandle InputActionSystem::GetAction(u64 nameHash) const
{
    auto action = _actionHashToIndex.find(nameHash);
    if (action == _actionHashToIndex.end())
        return {};

    return { action->second };
}

InputActionConnection InputActionSystem::Connect(InputActionHandle action, InputActionCallback callback)
{
    if (!IsActionValid(action) || !callback)
        return {};

    const u32 listenerID = _nextListenerID++;
    if (_nextListenerID == 0)
        _nextListenerID = 1;

    if (_callbackDispatchDepth != 0)
        _pendingConnections.push_back({ action, std::move(callback), listenerID, NextDeferredMutationOrder(), true });
    else
        _actions[action.index].listeners.push_back({ std::move(callback), listenerID, true });

    return { action, listenerID };
}

InputActionConnection InputActionSystem::Connect(u64 actionNameHash, InputActionCallback callback)
{
    return Connect(GetAction(actionNameHash), std::move(callback));
}

bool InputActionSystem::Disconnect(InputActionConnection connection)
{
    if (!connection.IsValid() || !IsActionValid(connection.action))
        return false;

    auto pendingConnection = std::find_if(_pendingConnections.begin(), _pendingConnections.end(), [connection](const PendingConnection& candidate)
    {
        return candidate.action == connection.action && candidate.listenerID == connection.listenerID;
    });

    if (pendingConnection != _pendingConnections.end())
    {
        pendingConnection->active = false;
        return true;
    }

    std::vector<Listener>& listeners = _actions[connection.action.index].listeners;
    auto listener = std::find_if(listeners.begin(), listeners.end(), [connection](const Listener& candidate)
    {
        return candidate.id == connection.listenerID;
    });

    if (listener == listeners.end())
        return false;

    if (_callbackDispatchDepth != 0)
    {
        listener->active = false;
        _listenersNeedCleanup = true;
    }
    else
    {
        listeners.erase(listener);
    }

    return true;
}

u32 InputActionSystem::Disconnect(std::span<const InputActionConnection> connections)
{
    u32 disconnectedCount = 0;
    for (InputActionConnection connection : connections)
    {
        if (Disconnect(connection))
            disconnectedCount++;
    }

    return disconnectedCount;
}

std::vector<InputBindingConflict> InputActionSystem::FindBindingConflicts(InputActionHandle actionHandle, u32 bindingSlot, const InputBinding& binding) const
{
    std::vector<InputBindingConflict> conflicts;
    if (!IsActionValid(actionHandle) || bindingSlot >= InputActionDesc::MAX_BINDINGS || !IsBindingValid(binding))
        return conflicts;

    const InputActionContextHandle requestedContext = _actions[actionHandle.index].info.context;
    for (u32 candidateIndex = 0; candidateIndex < _actions.size(); candidateIndex++)
    {
        const Action& candidate = _actions[candidateIndex];
        for (u32 candidateSlot = 0; candidateSlot < InputActionDesc::MAX_BINDINGS; candidateSlot++)
        {
            if (candidateIndex == actionHandle.index && candidateSlot == bindingSlot)
                continue;

            const std::optional<InputBinding>& candidateBinding = candidate.info.bindings[candidateSlot];
            if (!candidateBinding || candidateBinding->control != binding.control || !BindingModifiersOverlap(*candidateBinding, binding))
                continue;

            InputBindingConflictKind kind = InputBindingConflictKind::Direct;
            if (candidate.info.context != requestedContext)
            {
                kind = ContextDispatchesBefore(candidate.info.context, requestedContext) ? InputBindingConflictKind::ShadowsRequested : InputBindingConflictKind::ShadowedByRequested;
            }

            conflicts.push_back({ { static_cast<u16>(candidateIndex) }, candidateSlot, kind });
        }
    }

    return conflicts;
}

InputBindingChangeResult InputActionSystem::SetBinding(InputActionHandle actionHandle, u32 bindingSlot, std::optional<InputBinding> binding, InputBindingConflictPolicy policy)
{
    InputBindingChangeResult result;
    result.action = actionHandle;
    result.bindingSlot = bindingSlot;
    result.requestedPolicy = policy;
    result.appliedPolicy = policy;

    if (!IsActionValid(actionHandle))
        return result;

    if (bindingSlot >= InputActionDesc::MAX_BINDINGS)
    {
        result.status = InputBindingChangeStatus::InvalidSlot;
        return result;
    }

    Action& action = _actions[actionHandle.index];
    result.previousBinding = action.info.bindings[bindingSlot];
    if (!action.info.rebindable)
    {
        result.status = InputBindingChangeStatus::NotRebindable;
        return result;
    }

    if (binding && !IsBindingValid(*binding))
    {
        result.status = InputBindingChangeStatus::InvalidBinding;
        return result;
    }

    if (policy > InputBindingConflictPolicy::Allow)
    {
        result.status = InputBindingChangeStatus::InvalidPolicy;
        return result;
    }

    if (binding)
        result.conflicts = FindBindingConflicts(actionHandle, bindingSlot, *binding);

    if (action.info.bindings[bindingSlot] == binding)
    {
        result.status = result.conflicts.empty() ? InputBindingChangeStatus::Applied : InputBindingChangeStatus::AppliedWithConflicts;
        return result;
    }

    if (_callbackDispatchDepth != 0)
    {
        _pendingBindingChanges.push_back({ actionHandle, bindingSlot, binding, policy, NextDeferredMutationOrder() });
        result.status = InputBindingChangeStatus::Queued;
        return result;
    }

    result = ApplyBindingChange(actionHandle, bindingSlot, binding, policy);
    FlushDeferredMutations();
    return result;
}

InputBindingChangeResult InputActionSystem::ResetBinding(InputActionHandle action, u32 bindingSlot, InputBindingConflictPolicy policy)
{
    if (!IsActionValid(action))
        return {};

    if (bindingSlot >= InputActionDesc::MAX_BINDINGS)
    {
        InputBindingChangeResult result;
        result.action = action;
        result.bindingSlot = bindingSlot;
        result.status = InputBindingChangeStatus::InvalidSlot;
        return result;
    }

    return SetBinding(action, bindingSlot, _actions[action.index].info.defaultBindings[bindingSlot], policy);
}

bool InputActionSystem::BeginBindingCapture(InputBindingCaptureCallback callback)
{
    if (!callback || IsBindingCaptureActive() || !_bindingCaptureContext.IsValid())
        return false;

    _captureReleaseControl.reset();
    _captureIgnoredControls.reset();

    for (u32 key = 0; key < INPUT_KEY_COUNT; key++)
    {
        const InputControl control = InputControl::Keyboard(static_cast<Key>(key));
        if (_inputSystem.IsDown(control))
            _captureIgnoredControls.set(GetInputControlIndex(control));
    }

    for (u32 button = 0; button < INPUT_MOUSE_BUTTON_COUNT; button++)
    {
        const InputControl control = InputControl::Mouse(static_cast<MouseButton>(button));
        if (_inputSystem.IsDown(control))
            _captureIgnoredControls.set(GetInputControlIndex(control));
    }

    _bindingCaptureCallback = std::move(callback);
    if (!_inputSystem.SetContextActive(_bindingCaptureContext, true))
    {
        _bindingCaptureCallback = {};
        return false;
    }

    return true;
}

bool InputActionSystem::CancelBindingCapture(bool notifyCallback)
{
    if (!IsBindingCaptureActive())
        return false;

    FinishBindingCapture(std::nullopt, notifyCallback);
    return true;
}

bool InputActionSystem::LoadBindings()
{
    _persistedBindingOverrides.clear();
    if (_bindingPersistencePath.empty() || !std::filesystem::exists(_bindingPersistencePath))
        return true;

    try
    {
        std::ifstream stream(_bindingPersistencePath);
        if (!stream)
            return false;

        nlohmann::json root;
        stream >> root;
        if (!root.is_object() || root.value("version", 0u) != INPUT_BINDINGS_VERSION)
            return false;

        auto actions = root.find("actions");
        if (actions == root.end() || !actions->is_object())
            return true;

        for (auto action = actions->begin(); action != actions->end(); ++action)
        {
            if (!action.value().is_object())
                continue;

            PersistedActionOverrides overrides;
            for (u32 bindingSlot = 0; bindingSlot < InputActionDesc::MAX_BINDINGS; bindingSlot++)
            {
                const std::string slotName = std::to_string(bindingSlot + 1);
                auto value = action.value().find(slotName);
                if (value == action.value().end())
                    continue;

                if (value->is_null())
                {
                    overrides[bindingSlot].present = true;
                    continue;
                }

                std::optional<InputBinding> binding = ReadPersistedBinding(*value);
                if (binding)
                {
                    overrides[bindingSlot].binding = binding;
                    overrides[bindingSlot].present = true;
                }
            }

            if (std::ranges::any_of(overrides, [](const PersistedBindingOverride& overrideValue) { return overrideValue.present; }))
                _persistedBindingOverrides[action.key()] = std::move(overrides);
        }
    }
    catch (const std::exception& exception)
    {
        NC_LOG_WARNING("InputActionSystem: Failed to load bindings from '{}': {}", _bindingPersistencePath.string(), exception.what());
        _persistedBindingOverrides.clear();
        return false;
    }

    return true;
}

bool InputActionSystem::SaveBindings()
{
    if (_bindingPersistencePath.empty())
        return false;

    robin_hood::unordered_map<std::string, PersistedActionOverrides> savedOverrides = _persistedBindingOverrides;
    for (const Action& action : _actions)
    {
        savedOverrides.erase(action.info.name);
        if (!action.info.rebindable)
            continue;

        PersistedActionOverrides overrides;
        for (u32 bindingSlot = 0; bindingSlot < InputActionDesc::MAX_BINDINGS; bindingSlot++)
        {
            if (action.info.bindings[bindingSlot] == action.info.defaultBindings[bindingSlot])
                continue;

            overrides[bindingSlot].binding = action.info.bindings[bindingSlot];
            overrides[bindingSlot].present = true;
        }

        if (std::ranges::any_of(overrides, [](const PersistedBindingOverride& overrideValue) { return overrideValue.present; }))
            savedOverrides[action.info.name] = std::move(overrides);
    }

    nlohmann::ordered_json root = {
        { "version", INPUT_BINDINGS_VERSION },
        { "actions", nlohmann::ordered_json::object() }
    };
    nlohmann::ordered_json& actions = root["actions"];

    std::vector<std::string> actionNames;
    actionNames.reserve(savedOverrides.size());

    for (const auto& [actionName, overrides] : savedOverrides)
        actionNames.push_back(actionName);

    std::ranges::sort(actionNames);

    for (const std::string& actionName : actionNames)
    {
        const PersistedActionOverrides& overrides = savedOverrides.at(actionName);
        nlohmann::ordered_json actionJson = nlohmann::ordered_json::object();

        for (u32 bindingSlot = 0; bindingSlot < InputActionDesc::MAX_BINDINGS; bindingSlot++)
        {
            if (!overrides[bindingSlot].present)
                continue;

            const std::string slotName = std::to_string(bindingSlot + 1);
            actionJson[slotName] = overrides[bindingSlot].binding ? WritePersistedBinding(*overrides[bindingSlot].binding) : nlohmann::ordered_json(nullptr);
        }

        actions[actionName] = std::move(actionJson);
    }

    std::error_code error;
    if (!_bindingPersistencePath.parent_path().empty())
        std::filesystem::create_directories(_bindingPersistencePath.parent_path(), error);

    if (error)
        return false;

    std::filesystem::path temporaryPath = _bindingPersistencePath;
    temporaryPath += ".tmp";
    {
        std::ofstream stream(temporaryPath, std::ios::trunc);
        if (!stream)
            return false;

        stream << root.dump(4);
        stream.flush();

        if (!stream)
            return false;
    }

    if (!ReplaceFile(temporaryPath, _bindingPersistencePath))
    {
        std::filesystem::remove(temporaryPath, error);
        return false;
    }

    _persistedBindingOverrides = std::move(savedOverrides);
    return true;
}

bool InputActionSystem::IsDown(InputActionHandle action) const
{
    return IsActionValid(action) && _actions[action.index].downBindingCount != 0;
}

bool InputActionSystem::WasPressed(InputActionHandle action) const
{
    return IsActionValid(action) && _actions[action.index].pressed;
}

bool InputActionSystem::WasReleased(InputActionHandle action) const
{
    return IsActionValid(action) && _actions[action.index].released;
}

const InputActionInfo* InputActionSystem::GetActionInfo(InputActionHandle action) const
{
    if (!IsActionValid(action))
        return nullptr;

    return &_actions[action.index].info;
}

const InputActionContextInfo* InputActionSystem::GetContextInfo(InputActionContextHandle context) const
{
    if (!IsContextValid(context))
        return nullptr;

    return &_contexts[context.index].info;
}

std::span<const InputActionHandle> InputActionSystem::GetActions() const
{
    if (_sortedActionsDirty)
        RebuildSortedActions();

    return _sortedActions;
}

std::span<const InputActionContextHandle> InputActionSystem::GetContexts() const
{
    if (_sortedContextsDirty)
        RebuildSortedContexts();

    return _sortedContexts;
}

bool InputActionSystem::IsContextValid(InputActionContextHandle context) const
{
    return context.IsValid() && context.index < _contexts.size();
}

bool InputActionSystem::IsActionValid(InputActionHandle action) const
{
    return action.IsValid() && action.index < _actions.size();
}

std::optional<InputBinding> InputActionSystem::TryCreateBinding(u32 device, u32 code, u32 modifiers, u32 modifierMatch)
{
    if (device > std::numeric_limits<u8>::max() || code > std::numeric_limits<u16>::max() || modifiers > std::numeric_limits<u8>::max() || modifierMatch > std::numeric_limits<u8>::max())
    {
        return std::nullopt;
    }

    InputBinding binding;
    binding.control = { static_cast<InputDevice>(device), static_cast<u16>(code) };
    binding.modifiers = static_cast<InputModifier>(modifiers);
    binding.modifierMatch = static_cast<ModifierMatch>(modifierMatch);
    if (!IsBindingValid(binding))
        return std::nullopt;

    return binding;
}

bool InputActionSystem::IsBindingValid(const InputBinding& binding)
{
    const InputControl control = binding.control;
    bool controlValid = false;
    if (control.device == InputDevice::Mouse)
    {
        controlValid = control.code <= static_cast<u16>(MouseButton::Button8);
    }
    else if (control.device == InputDevice::MouseWheel)
    {
        controlValid = control.code <= static_cast<u16>(MouseWheelDirection::Right);
    }
    else if (control.device == InputDevice::Keyboard)
    {
        const u16 code = control.code;
        controlValid = code == static_cast<u16>(Key::Space)
            || code == static_cast<u16>(Key::Apostrophe)
            || (code >= static_cast<u16>(Key::Comma) && code <= static_cast<u16>(Key::Num9))
            || code == static_cast<u16>(Key::Semicolon)
            || code == static_cast<u16>(Key::Equal)
            || (code >= static_cast<u16>(Key::A) && code <= static_cast<u16>(Key::RightBracket))
            || code == static_cast<u16>(Key::GraveAccent)
            || (code >= static_cast<u16>(Key::World1) && code <= static_cast<u16>(Key::World2))
            || (code >= static_cast<u16>(Key::Escape) && code <= static_cast<u16>(Key::End))
            || (code >= static_cast<u16>(Key::CapsLock) && code <= static_cast<u16>(Key::Pause))
            || (code >= static_cast<u16>(Key::F1) && code <= static_cast<u16>(Key::F25))
            || (code >= static_cast<u16>(Key::Keypad0) && code <= static_cast<u16>(Key::KeypadEqual))
            || (code >= static_cast<u16>(Key::LeftShift) && code <= static_cast<u16>(Key::Menu));
    }

    if (!controlValid)
        return false;

    constexpr u32 VALID_MODIFIERS = static_cast<u32>(InputModifier::Shift)
        | static_cast<u32>(InputModifier::Control)
        | static_cast<u32>(InputModifier::Alt)
        | static_cast<u32>(InputModifier::Super);
    if ((static_cast<u32>(binding.modifiers) & ~VALID_MODIFIERS) != 0)
        return false;

    return binding.modifierMatch <= ModifierMatch::Any;
}

bool InputActionSystem::BindingModifiersOverlap(const InputBinding& left, const InputBinding& right)
{
    for (u32 modifierMask = 0; modifierMask < 16; modifierMask++)
    {
        const InputModifier modifiers = static_cast<InputModifier>(modifierMask);
        if (left.Matches(left.control, modifiers) && right.Matches(right.control, modifiers))
            return true;
    }

    return false;
}

bool InputActionSystem::ContextDispatchesBefore(InputActionContextHandle left, InputActionContextHandle right) const
{
    const Context& leftContext = _contexts[left.index];
    const Context& rightContext = _contexts[right.index];
    if (leftContext.info.priority != rightContext.info.priority)
        return leftContext.info.priority > rightContext.info.priority;

    return leftContext.creationOrder > rightContext.creationOrder;
}

void InputActionSystem::RebuildSortedActions() const
{
    std::sort(_sortedActions.begin(), _sortedActions.end(), [this](InputActionHandle left, InputActionHandle right)
    {
        const InputActionInfo& leftInfo = _actions[left.index].info;
        const InputActionInfo& rightInfo = _actions[right.index].info;
        if (leftInfo.categorySortOrder != rightInfo.categorySortOrder)
            return leftInfo.categorySortOrder < rightInfo.categorySortOrder;
        if (leftInfo.category != rightInfo.category)
            return leftInfo.category < rightInfo.category;
        if (leftInfo.sortOrder != rightInfo.sortOrder)
            return leftInfo.sortOrder < rightInfo.sortOrder;
        if (leftInfo.displayName != rightInfo.displayName)
            return leftInfo.displayName < rightInfo.displayName;

        return leftInfo.name < rightInfo.name;
    });

    _sortedActionsDirty = false;
}

void InputActionSystem::RebuildSortedContexts() const
{
    std::sort(_sortedContexts.begin(), _sortedContexts.end(), [this](InputActionContextHandle left, InputActionContextHandle right)
    {
        const InputActionContextInfo& leftInfo = _contexts[left.index].info;
        const InputActionContextInfo& rightInfo = _contexts[right.index].info;

        if (leftInfo.sortOrder != rightInfo.sortOrder)
            return leftInfo.sortOrder < rightInfo.sortOrder;

        return leftInfo.name < rightInfo.name;
    });

    _sortedContextsDirty = false;
}

void InputActionSystem::AddBindingReference(InputActionHandle action, u32 bindingSlot, const InputBinding& binding)
{
    const u32 controlIndex = GetInputControlIndex(binding.control);
    if (controlIndex >= INPUT_CONTROL_COUNT)
        return;

    Context& context = _contexts[_actions[action.index].info.context.index];
    std::vector<BindingReference>& references = context.bindingsByControl[controlIndex];
    const BindingReference reference = { action.index, static_cast<u8>(bindingSlot) };
    const auto insertionPoint = std::lower_bound(references.begin(), references.end(), reference, [](const BindingReference& left, const BindingReference& right)
    {
        if (left.actionIndex != right.actionIndex)
            return left.actionIndex < right.actionIndex;

        return left.bindingSlot < right.bindingSlot;
    });

    references.insert(insertionPoint, reference);
}

void InputActionSystem::RemoveBindingReference(InputActionHandle action, u32 bindingSlot, const InputBinding& binding)
{
    const u32 controlIndex = GetInputControlIndex(binding.control);
    if (controlIndex >= INPUT_CONTROL_COUNT)
        return;

    Context& context = _contexts[_actions[action.index].info.context.index];
    std::vector<BindingReference>& references = context.bindingsByControl[controlIndex];
    auto reference = std::find_if(references.begin(), references.end(), [action, bindingSlot](const BindingReference& candidate)
    {
        return candidate.actionIndex == action.index && candidate.bindingSlot == bindingSlot;
    });

    if (reference != references.end())
        references.erase(reference);
}

void InputActionSystem::SetBindingDirect(InputActionHandle actionHandle, u32 bindingSlot, std::optional<InputBinding> binding, InputBindingChangeResult& result)
{
    Action& action = _actions[actionHandle.index];

    std::optional<InputBinding>& currentBinding = action.info.bindings[bindingSlot];
    if (currentBinding == binding)
        return;

    const std::optional<InputBinding> previousBinding = currentBinding;
    if (action.bindingDown[bindingSlot])
    {
        InputEvent canceledEvent;
        canceledEvent.type = InputEventType::Button;
        canceledEvent.control = currentBinding->control;
        canceledEvent.phase = InputPhase::Canceled;
        canceledEvent.modifiers = currentBinding->modifiers;

        _callbackDispatchDepth++;
        HandleActionEvent(action, actionHandle, bindingSlot, canceledEvent, 0.0f);
        _callbackDispatchDepth--;
    }

    if (currentBinding)
        RemoveBindingReference(actionHandle, bindingSlot, *currentBinding);

    currentBinding = binding;
    if (currentBinding)
        AddBindingReference(actionHandle, bindingSlot, *currentBinding);

    result.mutations.push_back({ actionHandle, bindingSlot, previousBinding, binding });
}

InputBindingChangeResult InputActionSystem::ApplyBindingChange(InputActionHandle actionHandle, u32 bindingSlot, std::optional<InputBinding> binding, InputBindingConflictPolicy policy)
{
    InputBindingChangeResult result;
    result.action = actionHandle;
    result.bindingSlot = bindingSlot;
    result.requestedPolicy = policy;
    result.appliedPolicy = policy;

    if (!IsActionValid(actionHandle))
        return result;

    if (bindingSlot >= InputActionDesc::MAX_BINDINGS)
    {
        result.status = InputBindingChangeStatus::InvalidSlot;
        return result;
    }

    Action& action = _actions[actionHandle.index];
    result.previousBinding = action.info.bindings[bindingSlot];
    if (!action.info.rebindable)
    {
        result.status = InputBindingChangeStatus::NotRebindable;
        return result;
    }

    if (binding && !IsBindingValid(*binding))
    {
        result.status = InputBindingChangeStatus::InvalidBinding;
        return result;
    }

    if (policy > InputBindingConflictPolicy::Allow)
    {
        result.status = InputBindingChangeStatus::InvalidPolicy;
        return result;
    }

    if (binding)
        result.conflicts = FindBindingConflicts(actionHandle, bindingSlot, *binding);

    if (action.info.bindings[bindingSlot] == binding)
    {
        result.status = result.conflicts.empty() ? InputBindingChangeStatus::Applied : InputBindingChangeStatus::AppliedWithConflicts;
        return result;
    }

    std::vector<InputBindingConflict> directConflicts;
    for (const InputBindingConflict& conflict : result.conflicts)
    {
        if (conflict.kind == InputBindingConflictKind::Direct)
            directConflicts.push_back(conflict);
    }

    if (!directConflicts.empty() && policy == InputBindingConflictPolicy::Reject)
    {
        result.status = InputBindingChangeStatus::RejectedConflict;
        return result;
    }

    if (policy == InputBindingConflictPolicy::Replace || (policy == InputBindingConflictPolicy::Swap && directConflicts.size() == 1))
    {
        for (const InputBindingConflict& conflict : directConflicts)
        {
            if (!_actions[conflict.action.index].info.rebindable)
            {
                result.status = InputBindingChangeStatus::NotRebindable;
                return result;
            }
        }
    }

    if (policy == InputBindingConflictPolicy::Replace)
    {
        for (const InputBindingConflict& conflict : directConflicts)
        {
            SetBindingDirect(conflict.action, conflict.bindingSlot, std::nullopt, result);
        }
    }
    else if (policy == InputBindingConflictPolicy::Swap && directConflicts.size() == 1)
    {
        const InputBindingConflict& conflict = directConflicts.front();
        const std::optional<InputBinding> previousRequestedBinding = action.info.bindings[bindingSlot];

        SetBindingDirect(actionHandle, bindingSlot, std::nullopt, result);
        SetBindingDirect(conflict.action, conflict.bindingSlot, previousRequestedBinding, result);
    }
    else if (policy == InputBindingConflictPolicy::Swap && directConflicts.size() > 1)
    {
        result.appliedPolicy = InputBindingConflictPolicy::Allow;
    }

    SetBindingDirect(actionHandle, bindingSlot, binding, result);
    result.status = result.conflicts.empty() ? InputBindingChangeStatus::Applied : InputBindingChangeStatus::AppliedWithConflicts;
    return result;
}

u32 InputActionSystem::NextDeferredMutationOrder()
{
    const u32 order = _nextDeferredMutationOrder++;
    if (_nextDeferredMutationOrder == 0)
        _nextDeferredMutationOrder = 1;

    return order;
}

void InputActionSystem::FlushDeferredMutations()
{
    if (_callbackDispatchDepth != 0 || _flushingDeferredMutations)
        return;

    enum class DeferredMutationType : u8
    {
        None,
        BindingReference,
        Connection,
        BindingChange,
        ContextState
    };

    _flushingDeferredMutations = true;
    while (!_pendingBindingReferences.empty() || !_pendingConnections.empty() || !_pendingBindingChanges.empty() || !_pendingContextStates.empty())
    {
        u32 nextOrder = std::numeric_limits<u32>::max();
        DeferredMutationType nextType = DeferredMutationType::None;

        if (!_pendingBindingReferences.empty() && _pendingBindingReferences.front().order < nextOrder)
        {
            nextOrder = _pendingBindingReferences.front().order;
            nextType = DeferredMutationType::BindingReference;
        }

        if (!_pendingConnections.empty() && _pendingConnections.front().order < nextOrder)
        {
            nextOrder = _pendingConnections.front().order;
            nextType = DeferredMutationType::Connection;
        }

        if (!_pendingBindingChanges.empty() && _pendingBindingChanges.front().order < nextOrder)
        {
            nextOrder = _pendingBindingChanges.front().order;
            nextType = DeferredMutationType::BindingChange;
        }

        if (!_pendingContextStates.empty() && _pendingContextStates.front().order < nextOrder)
        {
            nextOrder = _pendingContextStates.front().order;
            nextType = DeferredMutationType::ContextState;
        }

        if (nextType == DeferredMutationType::BindingReference)
        {
            const PendingBindingReference reference = std::move(_pendingBindingReferences.front());
            _pendingBindingReferences.erase(_pendingBindingReferences.begin());

            if (IsActionValid(reference.action))
                AddBindingReference(reference.action, reference.bindingSlot, reference.binding);
        }
        else if (nextType == DeferredMutationType::Connection)
        {
            PendingConnection connection = std::move(_pendingConnections.front());
            _pendingConnections.erase(_pendingConnections.begin());

            if (connection.active && IsActionValid(connection.action))
                _actions[connection.action.index].listeners.push_back({ std::move(connection.callback), connection.listenerID, true });
        }
        else if (nextType == DeferredMutationType::BindingChange)
        {
            PendingBindingChange change = std::move(_pendingBindingChanges.front());
            _pendingBindingChanges.erase(_pendingBindingChanges.begin());

            ApplyBindingChange(change.action, change.bindingSlot, std::move(change.binding), change.policy);
        }
        else if (nextType == DeferredMutationType::ContextState)
        {
            const PendingContextState state = _pendingContextStates.front();
            _pendingContextStates.erase(_pendingContextStates.begin());

            if (IsContextValid(state.context))
            {
                if (!state.active)
                    _contexts[state.context.index].scrollRemainder = vec2(0.0f);
                _inputSystem.SetContextActive(_contexts[state.context.index].inputContext, state.active);
            }
        }
    }

    if (_listenersNeedCleanup)
    {
        for (Action& action : _actions)
        {
            std::erase_if(action.listeners, [](const Listener& listener)
            {
                return !listener.active;
            });
        }

        _listenersNeedCleanup = false;
    }

    _flushingDeferredMutations = false;
}

void InputActionSystem::MarkTransient(u16 actionIndex)
{
    Action& action = _actions[actionIndex];
    if (action.transientMarked)
        return;

    action.transientMarked = true;
    _transientActions.push_back(actionIndex);
}

InputReply InputActionSystem::HandleBindingCaptureEvent(const InputEvent& event)
{
    if (_captureReleaseControl)
    {
        if (event.type == InputEventType::FocusChanged && !event.focused)
        {
            _captureReleaseControl.reset();
            _inputSystem.SetContextActive(_bindingCaptureContext, false);

            return InputReply::Consumed;
        }

        if (event.type == InputEventType::Button && event.control == *_captureReleaseControl)
        {
            if (event.phase == InputPhase::Released || event.phase == InputPhase::Canceled)
            {
                _captureReleaseControl.reset();
                _inputSystem.SetContextActive(_bindingCaptureContext, false);
            }

            return InputReply::Consumed;
        }
    }

    if (!IsBindingCaptureActive())
        return InputReply::Ignored;

    if (event.type == InputEventType::FocusChanged)
    {
        if (!event.focused)
            FinishBindingCapture(std::nullopt, true);

        return InputReply::Consumed;
    }

    if (event.type == InputEventType::Scroll)
    {
        if (event.delta.x == 0.0f && event.delta.y == 0.0f)
            return InputReply::Consumed;

        MouseWheelDirection direction;
        if (std::abs(event.delta.y) >= std::abs(event.delta.x))
        {
            direction = event.delta.y > 0.0f ? MouseWheelDirection::Up : MouseWheelDirection::Down;
        }
        else
        {
            direction = event.delta.x > 0.0f ? MouseWheelDirection::Right : MouseWheelDirection::Left;
        }

        FinishBindingCapture(InputBinding::MouseWheel(direction, event.modifiers), true);
        return InputReply::Consumed;
    }

    if (event.type != InputEventType::Button)
        return event.type == InputEventType::Text ? InputReply::Consumed : InputReply::Ignored;

    const u32 controlIndex = GetInputControlIndex(event.control);
    if (controlIndex >= INPUT_CONTROL_COUNT)
        return InputReply::Consumed;

    if (_captureIgnoredControls.test(controlIndex))
    {
        if (event.phase == InputPhase::Released || event.phase == InputPhase::Canceled)
            _captureIgnoredControls.reset(controlIndex);

        return InputReply::Consumed;
    }

    if (event.phase != InputPhase::Pressed)
        return InputReply::Consumed;

    if (event.control.device == InputDevice::Keyboard)
    {
        const Key key = static_cast<Key>(event.control.code);
        if (key == Key::Escape)
        {
            FinishBindingCapture(std::nullopt, true);
            return InputReply::Consumed;
        }

        if (IsModifierKey(key))
            return InputReply::Consumed;
    }

    if (event.control.device == InputDevice::Keyboard || event.control.device == InputDevice::Mouse)
        FinishBindingCapture(InputBinding{ event.control, event.modifiers, ModifierMatch::Exact }, true);

    return InputReply::Consumed;
}

void InputActionSystem::FinishBindingCapture(std::optional<InputBinding> binding, bool notifyCallback)
{
    InputBindingCaptureCallback callback = std::move(_bindingCaptureCallback);
    _bindingCaptureCallback = {};
    _captureIgnoredControls.reset();

    if (binding && (binding->control.device == InputDevice::Keyboard || binding->control.device == InputDevice::Mouse))
    {
        _captureReleaseControl = binding->control;
    }
    else
    {
        _inputSystem.SetContextActive(_bindingCaptureContext, false);
    }

    if (notifyCallback && callback)
        callback(binding);
}

void InputActionSystem::ApplyPersistedBindings(InputActionHandle actionHandle)
{
    Action& action = _actions[actionHandle.index];
    if (!action.info.rebindable)
        return;

    auto overrides = _persistedBindingOverrides.find(action.info.name);
    if (overrides == _persistedBindingOverrides.end())
        return;

    for (u32 bindingSlot = 0; bindingSlot < InputActionDesc::MAX_BINDINGS; bindingSlot++)
    {
        if (overrides->second[bindingSlot].present)
            action.info.bindings[bindingSlot] = overrides->second[bindingSlot].binding;
    }
}

InputReply InputActionSystem::HandleContextEvent(InputActionContextHandle contextHandle, const InputEvent& event)
{
    if (!IsContextValid(contextHandle))
        return InputReply::Ignored;

    _callbackDispatchDepth++;

    InputReply result = InputReply::Ignored;
    if (event.type == InputEventType::Button)
    {
        result = HandleButtonEvent(contextHandle, event);
    }
    else if (event.type == InputEventType::Scroll)
    {
        result = HandleScrollEvent(contextHandle, event);
    }

    _callbackDispatchDepth--;
    FlushDeferredMutations();

    return result;
}

InputReply InputActionSystem::HandleButtonEvent(InputActionContextHandle contextHandle, const InputEvent& event)
{
    _dispatchSerial++;
    if (_dispatchSerial == 0)
        _dispatchSerial = 1;

    const u32 controlIndex = GetInputControlIndex(event.control);
    if (controlIndex >= INPUT_CONTROL_COUNT)
        return InputReply::Ignored;

    Context& context = _contexts[contextHandle.index];
    InputReply result = InputReply::Ignored;
    for (const BindingReference& reference : context.bindingsByControl[controlIndex])
    {
        Action& action = _actions[reference.actionIndex];
        if (action.lastDispatchSerial == _dispatchSerial)
            continue;

        const std::optional<InputBinding>& binding = action.info.bindings[reference.bindingSlot];
        if (!binding)
            continue;

        const bool isRelease = event.phase == InputPhase::Released || event.phase == InputPhase::Canceled;
        if (!isRelease && !binding->Matches(event.control, event.modifiers))
            continue;

        if (isRelease && !action.bindingDown[reference.bindingSlot])
            continue;

        action.lastDispatchSerial = _dispatchSerial;
        const InputReply reply = HandleActionEvent(action, { reference.actionIndex }, reference.bindingSlot, event, event.phase == InputPhase::Released || event.phase == InputPhase::Canceled ? 0.0f : 1.0f);
        if (!isRelease && reply == InputReply::Consumed)
            return reply;

        result = MaxReply(result, reply);
    }

    return result;
}

InputReply InputActionSystem::HandleScrollEvent(InputActionContextHandle contextHandle, const InputEvent& event)
{
    Context& context = _contexts[contextHandle.index];
    f32 wholeX = 0.0f;
    f32 wholeY = 0.0f;
    const InputControl verticalControl = InputControl::MouseWheel(event.delta.y >= 0.0f ? MouseWheelDirection::Up : MouseWheelDirection::Down);
    const InputControl horizontalControl = InputControl::MouseWheel(event.delta.x >= 0.0f ? MouseWheelDirection::Right : MouseWheelDirection::Left);
    const bool handlesVertical = event.delta.y != 0.0f && HasMatchingBinding(context, verticalControl, event.modifiers);
    const bool handlesHorizontal = event.delta.x != 0.0f && HasMatchingBinding(context, horizontalControl, event.modifiers);

    if (handlesVertical)
    {
        context.scrollRemainder.y += event.delta.y;
        wholeY = std::trunc(context.scrollRemainder.y);
        context.scrollRemainder.y -= wholeY;
    }

    if (handlesHorizontal)
    {
        context.scrollRemainder.x += event.delta.x;
        wholeX = std::trunc(context.scrollRemainder.x);
        context.scrollRemainder.x -= wholeX;
    }

    InputReply result = InputReply::Ignored;
    if (handlesVertical && (event.delta.y != 0.0f || wholeY != 0.0f))
    {
        const f32 value = std::abs(wholeY);
        const InputReply reply = DispatchScrollDirection(context, verticalControl, event.modifiers, value, value >= 1.0f);
        if (reply == InputReply::Consumed)
            return reply;

        result = MaxReply(result, reply);
    }

    if (handlesHorizontal && (event.delta.x != 0.0f || wholeX != 0.0f))
    {
        const f32 value = std::abs(wholeX);
        const InputReply reply = DispatchScrollDirection(context, horizontalControl, event.modifiers, value, value >= 1.0f);
        if (reply == InputReply::Consumed)
            return reply;

        result = MaxReply(result, reply);
    }

    return result;
}

bool InputActionSystem::HasMatchingBinding(const Context& context, InputControl control, InputModifier modifiers) const
{
    const u32 controlIndex = GetInputControlIndex(control);
    if (controlIndex >= INPUT_CONTROL_COUNT)
        return false;

    for (const BindingReference& reference : context.bindingsByControl[controlIndex])
    {
        const std::optional<InputBinding>& binding = _actions[reference.actionIndex].info.bindings[reference.bindingSlot];
        if (binding && binding->Matches(control, modifiers))
            return true;
    }

    return false;
}

InputReply InputActionSystem::DispatchScrollDirection(Context& context, InputControl control, InputModifier modifiers, f32 value, bool triggered)
{
    const u32 controlIndex = GetInputControlIndex(control);
    if (controlIndex >= INPUT_CONTROL_COUNT)
        return InputReply::Ignored;

    _dispatchSerial++;
    if (_dispatchSerial == 0)
        _dispatchSerial = 1;

    InputReply result = InputReply::Ignored;
    for (const BindingReference& reference : context.bindingsByControl[controlIndex])
    {
        Action& action = _actions[reference.actionIndex];
        if (action.lastDispatchSerial == _dispatchSerial)
            continue;

        const std::optional<InputBinding>& binding = action.info.bindings[reference.bindingSlot];
        if (!binding || !binding->Matches(control, modifiers))
            continue;

        action.lastDispatchSerial = _dispatchSerial;
        if (!triggered)
        {
            if (action.info.defaultReply == InputReply::Consumed)
                return InputReply::Consumed;

            result = MaxReply(result, action.info.defaultReply);
            continue;
        }

        InputEvent actionSource = {};
        actionSource.type = InputEventType::Scroll;
        actionSource.control = control;
        actionSource.phase = InputPhase::Triggered;
        actionSource.modifiers = modifiers;

        const InputReply reply = HandleActionEvent(action, { reference.actionIndex }, reference.bindingSlot, actionSource, value);
        if (reply == InputReply::Consumed)
            return reply;

        result = MaxReply(result, reply);
    }

    return result;
}

InputReply InputActionSystem::HandleActionEvent(Action& action, InputActionHandle handle, u32 bindingSlot, const InputEvent& event, f32 value)
{
    if (event.phase == InputPhase::Pressed && !action.bindingDown[bindingSlot])
    {
        action.bindingDown[bindingSlot] = true;
        action.downBindingCount++;

        if (action.downBindingCount == 1)
        {
            action.pressed = true;
            MarkTransient(handle.index);
        }
    }
    else if ((event.phase == InputPhase::Released || event.phase == InputPhase::Canceled) && action.bindingDown[bindingSlot])
    {
        action.bindingDown[bindingSlot] = false;
        action.downBindingCount--;

        if (action.downBindingCount == 0)
        {
            action.released = true;
            MarkTransient(handle.index);
        }
    }

    InputActionEvent actionEvent;
    actionEvent.action = handle;
    actionEvent.control = event.control;
    actionEvent.phase = event.phase;
    actionEvent.modifiers = event.modifiers;
    actionEvent.value = value;

    InputReply result = action.info.defaultReply;
    if (_metricsEnabled)
        _frameMetrics.actionEvents++;

    _callbackDispatchDepth++;
    const u32 listenerCount = static_cast<u32>(action.listeners.size());
    for (u32 listenerIndex = 0; listenerIndex < listenerCount; listenerIndex++)
    {
        Action& currentAction = _actions[handle.index];
        Listener& listener = currentAction.listeners[listenerIndex];

        if (listener.active)
        {
            if (_metricsEnabled)
                _frameMetrics.listenerCallbacks++;

            result = MaxReply(result, listener.callback(actionEvent));
        }
    }
    _callbackDispatchDepth--;

    FlushDeferredMutations();

    return result;
}
