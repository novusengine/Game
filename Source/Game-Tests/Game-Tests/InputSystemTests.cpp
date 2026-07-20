#include "Game-Lib/Input/InputActionSystem.h"

#include <Input/InputSystem.h>

#include <catch2/catch2.hpp>

#include <filesystem>
#include <fstream>
#include <vector>

TEST_CASE("Input actions expose crisp per-frame state and paired releases", "[Input]")
{
    InputSystem inputSystem;
    InputActionSystem inputActions(inputSystem);

    const InputActionContextHandle context = inputActions.CreateContext("Test", GameInputPriority::Gameplay);
    REQUIRE(inputActions.SetContextActive(context, true));

    std::vector<InputPhase> phases;
    const InputActionHandle action = inputActions.RegisterAction(
        context,
        "MoveForward",
        "Move Forward",
        "Movement",
        InputBinding::Keyboard(Key::W),
        [&phases](const InputActionEvent& event)
        {
            phases.push_back(event.phase);
            return InputReply::Consumed;
        });
    REQUIRE(action.IsValid());

    inputSystem.BeginFrame();
    inputActions.BeginFrame();
    inputSystem.QueueKeyboardEvent(Key::W, InputPhase::Pressed);
    inputSystem.ProcessEvents();

    CHECK(inputActions.IsDown(action));
    CHECK(inputActions.WasPressed(action));
    CHECK_FALSE(inputActions.WasReleased(action));
    REQUIRE(phases.size() == 1);
    CHECK(phases[0] == InputPhase::Pressed);

    inputSystem.BeginFrame();
    inputActions.BeginFrame();
    CHECK(inputActions.IsDown(action));
    CHECK_FALSE(inputActions.WasPressed(action));

    inputSystem.QueueKeyboardEvent(Key::W, InputPhase::Released);
    inputSystem.ProcessEvents();

    CHECK_FALSE(inputActions.IsDown(action));
    CHECK(inputActions.WasReleased(action));
    REQUIRE(phases.size() == 2);
    CHECK(phases[1] == InputPhase::Released);
}

TEST_CASE("Single-binding action registration accepts focused behavior options", "[Input]")
{
    InputSystem inputSystem;
    InputActionSystem inputActions(inputSystem);

    u32 lowerPriorityPresses = 0;
    const InputContextHandle lowerPriorityContext = inputSystem.CreateContext("LowerPriority", GameInputPriority::Debug, [&lowerPriorityPresses](const InputEvent& event)
    {
        if (event.phase == InputPhase::Pressed)
            lowerPriorityPresses++;
        return InputReply::Ignored;
    });
    REQUIRE(inputSystem.SetContextActive(lowerPriorityContext, true));

    const InputActionContextHandle context = inputActions.CreateContext("Test", GameInputPriority::Gameplay);
    REQUIRE(inputActions.SetContextActive(context, true));

    const InputActionHandle action = inputActions.RegisterAction(context, "FixedPassThrough", "Fixed Pass Through", "Test",
        InputBinding::Keyboard(Key::P), { .defaultReply = InputReply::Ignored, .rebindable = false }, [](const InputActionEvent&)
    {
        return InputReply::Ignored;
    });
    REQUIRE(action.IsValid());

    const InputActionInfo* info = inputActions.GetActionInfo(action);
    REQUIRE(info != nullptr);
    CHECK(info->defaultReply == InputReply::Ignored);
    CHECK_FALSE(info->rebindable);
    CHECK(info->defaultBindings[0] == InputBinding::Keyboard(Key::P));

    CHECK(InputActionSystem::TryCreateBinding(static_cast<u32>(InputDevice::Keyboard), static_cast<u32>(Key::P), static_cast<u32>(InputModifier::None), static_cast<u32>(ModifierMatch::Exact)) == InputBinding::Keyboard(Key::P));
    CHECK_FALSE(InputActionSystem::TryCreateBinding(static_cast<u32>(InputDevice::Keyboard), 33, static_cast<u32>(InputModifier::None), static_cast<u32>(ModifierMatch::Exact)));

    inputSystem.BeginFrame();
    inputActions.BeginFrame();
    inputSystem.QueueKeyboardEvent(Key::P, InputPhase::Pressed);
    inputSystem.ProcessEvents();

    CHECK(lowerPriorityPresses == 1);
}

TEST_CASE("Higher priority input consumers prevent lower contexts from observing presses", "[Input]")
{
    InputSystem inputSystem;
    u32 highPriorityPresses = 0;
    u32 lowPriorityPresses = 0;

    const InputContextHandle lowPriority = inputSystem.CreateContext("Low", 100, [&lowPriorityPresses](const InputEvent& event)
    {
        if (event.phase == InputPhase::Pressed)
            lowPriorityPresses++;
        return InputReply::Handled;
    });
    const InputContextHandle highPriority = inputSystem.CreateContext("High", 200, [&highPriorityPresses](const InputEvent& event)
    {
        if (event.phase == InputPhase::Pressed)
            highPriorityPresses++;
        return InputReply::Consumed;
    });
    REQUIRE(inputSystem.SetContextActive(lowPriority, true));
    REQUIRE(inputSystem.SetContextActive(highPriority, true));

    inputSystem.BeginFrame();
    inputSystem.QueueKeyboardEvent(Key::Space, InputPhase::Pressed);
    inputSystem.ProcessEvents();

    CHECK(highPriorityPresses == 1);
    CHECK(lowPriorityPresses == 0);
}

TEST_CASE("Deactivating an owning context cancels its active actions", "[Input]")
{
    InputSystem inputSystem;
    InputActionSystem inputActions(inputSystem);

    const InputActionContextHandle context = inputActions.CreateContext("Test", GameInputPriority::Gameplay);
    REQUIRE(inputActions.SetContextActive(context, true));

    InputPhase lastPhase = InputPhase::None;
    const InputActionHandle action = inputActions.RegisterAction(
        context,
        "Hold",
        "Hold",
        "Test",
        InputBinding::Mouse(MouseButton::Left),
        [&lastPhase](const InputActionEvent& event)
        {
            lastPhase = event.phase;
            return InputReply::Consumed;
        });

    inputSystem.BeginFrame();
    inputActions.BeginFrame();
    inputSystem.QueueMouseButtonEvent(MouseButton::Left, InputPhase::Pressed);
    inputSystem.ProcessEvents();
    REQUIRE(inputActions.IsDown(action));

    REQUIRE(inputActions.SetContextActive(context, false));
    CHECK_FALSE(inputActions.IsDown(action));
    CHECK(inputActions.WasReleased(action));
    CHECK(lastPhase == InputPhase::Canceled);
}

TEST_CASE("Direct binding conflicts reject by default and permissive conflicts dispatch one action", "[Input]")
{
    InputSystem inputSystem;
    InputActionSystem inputActions(inputSystem);

    const InputActionContextHandle context = inputActions.CreateContext("Gameplay", GameInputPriority::Gameplay);
    REQUIRE(inputActions.SetContextActive(context, true));

    const InputActionHandle jump = inputActions.RegisterAction(context, "Jump", "Jump", "Movement", InputBinding::Keyboard(Key::Space));
    const InputActionHandle autorun = inputActions.RegisterAction(context, "ToggleAutorun", "Toggle Autorun", "Movement", InputBinding::Mouse(MouseButton::Middle));

    InputBindingChangeResult rejected = inputActions.SetBinding(autorun, 0, InputBinding::Keyboard(Key::Space));
    CHECK(rejected.status == InputBindingChangeStatus::RejectedConflict);
    REQUIRE(rejected.conflicts.size() == 1);
    CHECK(rejected.conflicts[0].action == jump);
    CHECK(rejected.conflicts[0].kind == InputBindingConflictKind::Direct);
    CHECK(inputActions.GetActionInfo(autorun)->bindings[0] == InputBinding::Mouse(MouseButton::Middle));

    InputBindingChangeResult allowed = inputActions.SetBinding(autorun, 0, InputBinding::Keyboard(Key::Space), InputBindingConflictPolicy::Allow);
    CHECK(allowed.status == InputBindingChangeStatus::AppliedWithConflicts);

    inputSystem.BeginFrame();
    inputActions.BeginFrame();
    inputSystem.QueueKeyboardEvent(Key::Space, InputPhase::Pressed);
    inputSystem.ProcessEvents();

    CHECK(inputActions.IsDown(jump));
    CHECK_FALSE(inputActions.IsDown(autorun));
}

TEST_CASE("Cross-context overlaps report deterministic shadowing without blocking a bind", "[Input]")
{
    InputSystem inputSystem;
    InputActionSystem inputActions(inputSystem);

    const InputActionContextHandle globalContext = inputActions.CreateContext("Global", GameInputPriority::Global);
    const InputActionContextHandle gameplayContext = inputActions.CreateContext("Gameplay", GameInputPriority::Gameplay);
    const InputActionHandle toggleMap = inputActions.RegisterAction(globalContext, "ToggleMap", "Toggle Map", "Interface", InputBinding::Keyboard(Key::M));
    const InputActionHandle mount = inputActions.RegisterAction(gameplayContext, "Mount", "Mount", "Movement", InputBinding::Keyboard(Key::H));

    InputBindingChangeResult result = inputActions.SetBinding(mount, 0, InputBinding::Keyboard(Key::M));
    CHECK(result.status == InputBindingChangeStatus::AppliedWithConflicts);
    REQUIRE(result.conflicts.size() == 1);
    CHECK(result.conflicts[0].action == toggleMap);
    CHECK(result.conflicts[0].kind == InputBindingConflictKind::ShadowsRequested);
}

TEST_CASE("Modifier conflict detection tests behavioral overlap", "[Input]")
{
    InputSystem inputSystem;
    InputActionSystem inputActions(inputSystem);

    const InputActionContextHandle context = inputActions.CreateContext("Gameplay", GameInputPriority::Gameplay);
    inputActions.RegisterAction(context, "ShiftAction", "Shift Action", "Test", InputBinding::Keyboard(Key::K, InputModifier::Shift, ModifierMatch::AtLeast));
    const InputActionHandle exactControl = inputActions.RegisterAction(context, "ControlAction", "Control Action", "Test", InputBinding::Keyboard(Key::L));

    CHECK(inputActions.FindBindingConflicts(exactControl, 0, InputBinding::Keyboard(Key::K, InputModifier::Control, ModifierMatch::Exact)).empty());
    CHECK(inputActions.FindBindingConflicts(exactControl, 0, InputBinding::Keyboard(Key::K, InputModifier::Shift | InputModifier::Control, ModifierMatch::Exact)).size() == 1);
}

TEST_CASE("Replace, swap, and ambiguous swap policies report every mutation", "[Input]")
{
    InputSystem inputSystem;
    InputActionSystem inputActions(inputSystem);

    const InputActionContextHandle context = inputActions.CreateContext("Gameplay", GameInputPriority::Gameplay);
    const InputActionHandle first = inputActions.RegisterAction(context, "First", "First", "Test", InputBinding::Keyboard(Key::A));
    const InputActionHandle second = inputActions.RegisterAction(context, "Second", "Second", "Test", InputBinding::Keyboard(Key::B));
    const InputActionHandle third = inputActions.RegisterAction(context, "Third", "Third", "Test", InputBinding::Keyboard(Key::C));

    InputBindingChangeResult swapped = inputActions.SetBinding(second, 0, InputBinding::Keyboard(Key::A), InputBindingConflictPolicy::Swap);
    CHECK(swapped.Succeeded());
    CHECK(inputActions.GetActionInfo(first)->bindings[0] == InputBinding::Keyboard(Key::B));
    CHECK(inputActions.GetActionInfo(second)->bindings[0] == InputBinding::Keyboard(Key::A));

    REQUIRE(inputActions.SetBinding(first, 0, InputBinding::Keyboard(Key::A), InputBindingConflictPolicy::Allow).Succeeded());
    InputBindingChangeResult ambiguous = inputActions.SetBinding(third, 0, InputBinding::Keyboard(Key::A), InputBindingConflictPolicy::Swap);
    CHECK(ambiguous.status == InputBindingChangeStatus::AppliedWithConflicts);
    CHECK(ambiguous.requestedPolicy == InputBindingConflictPolicy::Swap);
    CHECK(ambiguous.appliedPolicy == InputBindingConflictPolicy::Allow);
    CHECK(ambiguous.conflicts.size() == 2);
    CHECK(inputActions.GetActionInfo(first)->bindings[0] == InputBinding::Keyboard(Key::A));
    CHECK(inputActions.GetActionInfo(second)->bindings[0] == InputBinding::Keyboard(Key::A));
    CHECK(inputActions.GetActionInfo(third)->bindings[0] == InputBinding::Keyboard(Key::A));

    InputBindingChangeResult replaced = inputActions.SetBinding(third, 0, InputBinding::Keyboard(Key::A), InputBindingConflictPolicy::Replace);
    CHECK(replaced.Succeeded());
    CHECK_FALSE(inputActions.GetActionInfo(first)->bindings[0].has_value());
    CHECK_FALSE(inputActions.GetActionInfo(second)->bindings[0].has_value());
}

TEST_CASE("Action and context enumeration is stable and explicitly sorted", "[Input]")
{
    InputSystem inputSystem;
    InputActionSystem inputActions(inputSystem);

    const InputActionContextHandle laterContext = inputActions.CreateContext({ "Later", GameInputPriority::Gameplay, 20 });
    const InputActionContextHandle earlierContext = inputActions.CreateContext({ "Earlier", GameInputPriority::Gameplay, 10 });

    InputActionDesc laterAction;
    laterAction.name = "LaterAction";
    laterAction.displayName = "Zulu";
    laterAction.category = "Movement";
    laterAction.categorySortOrder = 20;
    laterAction.sortOrder = 20;
    const InputActionHandle later = inputActions.RegisterAction(laterContext, laterAction);

    InputActionDesc earlierAction;
    earlierAction.name = "EarlierAction";
    earlierAction.displayName = "Alpha";
    earlierAction.category = "Interface";
    earlierAction.categorySortOrder = 10;
    earlierAction.sortOrder = 10;
    const InputActionHandle earlier = inputActions.RegisterAction(earlierContext, earlierAction);

    REQUIRE(inputActions.GetContexts().size() == 2);
    CHECK(inputActions.GetContexts()[0] == earlierContext);
    CHECK(inputActions.GetContexts()[1] == laterContext);
    REQUIRE(inputActions.GetActions().size() == 2);
    CHECK(inputActions.GetActions()[0] == earlier);
    CHECK(inputActions.GetActions()[1] == later);
    CHECK_FALSE(inputActions.RegisterAction(earlierContext, "EarlierAction", "Duplicate", "Test", InputBinding::Keyboard(Key::A)).IsValid());
}

TEST_CASE("Callback mutations are deferred until the outer action dispatch completes", "[Input]")
{
    InputSystem inputSystem;
    InputActionSystem inputActions(inputSystem);

    const InputActionContextHandle context = inputActions.CreateContext("Gameplay", GameInputPriority::Gameplay);
    REQUIRE(inputActions.SetContextActive(context, true));
    const InputActionHandle action = inputActions.RegisterAction(context, "Mutable", "Mutable", "Test", InputBinding::Keyboard(Key::A));

    u32 primaryCalls = 0;
    u32 deferredCalls = 0;
    InputActionConnection primaryConnection;
    primaryConnection = inputActions.Connect(action, [&](const InputActionEvent& event)
    {
        if (event.phase != InputPhase::Pressed)
            return InputReply::Consumed;

        primaryCalls++;
        CHECK(inputActions.Disconnect(primaryConnection));
        REQUIRE(inputActions.Connect(action, [&deferredCalls](const InputActionEvent& deferredEvent)
        {
            if (deferredEvent.phase == InputPhase::Pressed)
                deferredCalls++;
            return InputReply::Consumed;
        }).IsValid());

        const InputBindingChangeResult result = inputActions.SetBinding(action, 0, InputBinding::Keyboard(Key::B));
        CHECK(result.status == InputBindingChangeStatus::Queued);
        return InputReply::Consumed;
    });

    inputSystem.BeginFrame();
    inputActions.BeginFrame();
    inputSystem.QueueKeyboardEvent(Key::A, InputPhase::Pressed);
    inputSystem.ProcessEvents();
    CHECK(primaryCalls == 1);
    CHECK(deferredCalls == 0);

    inputSystem.BeginFrame();
    inputActions.BeginFrame();
    inputSystem.QueueKeyboardEvent(Key::B, InputPhase::Pressed);
    inputSystem.ProcessEvents();
    CHECK(primaryCalls == 1);
    CHECK(deferredCalls == 1);
}

TEST_CASE("Context deactivation requested by a callback delivers cancellation after the callback", "[Input]")
{
    InputSystem inputSystem;
    InputActionSystem inputActions(inputSystem);

    const InputActionContextHandle context = inputActions.CreateContext("Gameplay", GameInputPriority::Gameplay);
    REQUIRE(inputActions.SetContextActive(context, true));

    std::vector<InputPhase> phases;
    inputActions.RegisterAction(context, "CloseContext", "Close Context", "Test", InputBinding::Keyboard(Key::Escape), [&](const InputActionEvent& event)
    {
        phases.push_back(event.phase);
        if (event.phase == InputPhase::Pressed)
            CHECK(inputActions.SetContextActive(context, false));
        return InputReply::Consumed;
    });

    inputSystem.BeginFrame();
    inputActions.BeginFrame();
    inputSystem.QueueKeyboardEvent(Key::Escape, InputPhase::Pressed);
    inputSystem.ProcessEvents();

    REQUIRE(phases.size() == 2);
    CHECK(phases[0] == InputPhase::Pressed);
    CHECK(phases[1] == InputPhase::Canceled);
    CHECK_FALSE(inputActions.IsContextActive(context));
}

TEST_CASE("Wheel actions coalesce whole steps and keep fractional remainders context-local", "[Input]")
{
    InputSystem inputSystem;
    InputActionSystem inputActions(inputSystem);

    const InputActionContextHandle uiContext = inputActions.CreateContext("UI", GameInputPriority::UI);
    const InputActionContextHandle cameraContext = inputActions.CreateContext("Camera", GameInputPriority::Gameplay);
    REQUIRE(inputActions.SetContextActive(uiContext, true));
    REQUIRE(inputActions.SetContextActive(cameraContext, true));

    f32 uiValue = 0.0f;
    f32 cameraValue = 0.0f;
    inputActions.RegisterAction(uiContext, "UIScroll", "UI Scroll", "Interface", InputBinding::MouseWheel(MouseWheelDirection::Up), [&uiValue](const InputActionEvent& event)
    {
        CHECK(event.phase == InputPhase::Triggered);
        uiValue += event.value;
        return InputReply::Consumed;
    });
    inputActions.RegisterAction(cameraContext, "CameraZoom", "Camera Zoom", "Camera", InputBinding::MouseWheel(MouseWheelDirection::Up), [&cameraValue](const InputActionEvent& event)
    {
        cameraValue += event.value;
        return InputReply::Consumed;
    });

    inputSystem.BeginFrame();
    inputActions.BeginFrame();
    inputSystem.QueueScrollEvent(0.0f, 0.4f);
    inputSystem.ProcessEvents();
    CHECK(uiValue == 0.0f);
    CHECK(cameraValue == 0.0f);

    inputSystem.BeginFrame();
    inputActions.BeginFrame();
    inputSystem.QueueScrollEvent(0.0f, 2.7f);
    inputSystem.QueueScrollEvent(0.0f, 1.0f);
    inputSystem.ProcessEvents();
    CHECK(uiValue == 4.0f);
    CHECK(cameraValue == 0.0f);

    REQUIRE(inputActions.SetContextActive(uiContext, false));
    inputSystem.BeginFrame();
    inputActions.BeginFrame();
    inputSystem.QueueScrollEvent(0.0f, 0.7f);
    inputSystem.ProcessEvents();
    CHECK(cameraValue == 0.0f);

    inputSystem.BeginFrame();
    inputActions.BeginFrame();
    inputSystem.QueueScrollEvent(0.0f, 0.4f);
    inputSystem.ProcessEvents();
    CHECK(cameraValue == 1.0f);
}

TEST_CASE("Wheel contexts accumulate only directions they bind", "[Input]")
{
    InputSystem inputSystem;
    InputActionSystem inputActions(inputSystem);

    const InputActionContextHandle uiContext = inputActions.CreateContext("UI", GameInputPriority::UI);
    const InputActionContextHandle cameraContext = inputActions.CreateContext("Camera", GameInputPriority::Gameplay);
    REQUIRE(inputActions.SetContextActive(uiContext, true));
    REQUIRE(inputActions.SetContextActive(cameraContext, true));

    f32 uiValue = 0.0f;
    f32 cameraValue = 0.0f;
    inputActions.RegisterAction(uiContext, "UIScrollUp", "UI Scroll Up", "Interface", InputBinding::MouseWheel(MouseWheelDirection::Up), [&uiValue](const InputActionEvent& event)
    {
        uiValue += event.value;
        return InputReply::Consumed;
    });
    inputActions.RegisterAction(cameraContext, "CameraZoomOut", "Camera Zoom Out", "Camera", InputBinding::MouseWheel(MouseWheelDirection::Down), [&cameraValue](const InputActionEvent& event)
    {
        cameraValue += event.value;
        return InputReply::Consumed;
    });

    auto scroll = [&](f32 delta)
    {
        inputSystem.BeginFrame();
        inputActions.BeginFrame();
        inputSystem.QueueScrollEvent(0.0f, delta);
        inputSystem.ProcessEvents();
    };

    for (u32 i = 0; i < 3; i++)
    {
        scroll(-0.4f);
    }
    CHECK(uiValue == 0.0f);
    CHECK(cameraValue == 1.0f);

    for (u32 i = 0; i < 2; i++)
    {
        scroll(0.5f);
    }
    CHECK(uiValue == 1.0f);
    CHECK(cameraValue == 1.0f);
}

TEST_CASE("Optional input metrics count raw routing and action callbacks", "[Input]")
{
    InputSystem inputSystem;
    InputActionSystem inputActions(inputSystem);
    inputSystem.SetMetricsEnabled(true);
    inputActions.SetMetricsEnabled(true);

    const InputActionContextHandle context = inputActions.CreateContext("Gameplay", GameInputPriority::Gameplay);
    REQUIRE(inputActions.SetContextActive(context, true));
    inputActions.RegisterAction(context, "Measured", "Measured", "Test", InputBinding::Keyboard(Key::A), [](const InputActionEvent&)
    {
        return InputReply::Consumed;
    });

    inputSystem.BeginFrame();
    inputActions.BeginFrame();
    inputSystem.QueueKeyboardEvent(Key::A, InputPhase::Pressed);
    inputSystem.ProcessEvents();

    CHECK(inputSystem.GetFrameMetrics().queuedEvents == 1);
    CHECK(inputSystem.GetFrameMetrics().contextCallbacks == 1);
    CHECK(inputActions.GetFrameMetrics().actionEvents == 1);
    CHECK(inputActions.GetFrameMetrics().listenerCallbacks == 1);
}

TEST_CASE("Dropped button events do not diverge snapshots from routed state", "[Input]")
{
    InputSystem inputSystem;
    inputSystem.QueueKeyboardEvent(Key::A, InputPhase::Pressed);
    inputSystem.ProcessEvents();
    REQUIRE(inputSystem.IsDown(InputControl::Keyboard(Key::A)));

    inputSystem.BeginFrame();
    for (u32 eventIndex = 0; eventIndex < InputSystem::MAX_EVENTS_PER_FRAME; eventIndex++)
    {
        inputSystem.QueueTextEvent('a');
    }
    inputSystem.QueueKeyboardEvent(Key::A, InputPhase::Released);

    CHECK(inputSystem.IsDown(InputControl::Keyboard(Key::A)));
    CHECK_FALSE(inputSystem.WasReleased(InputControl::Keyboard(Key::A)));
}

TEST_CASE("Connection ownership disconnects a group without retaining individual handles", "[Input]")
{
    InputSystem inputSystem;
    InputActionSystem inputActions(inputSystem);

    const InputActionContextHandle context = inputActions.CreateContext("Gameplay", GameInputPriority::Gameplay);
    REQUIRE(inputActions.SetContextActive(context, true));
    const InputActionHandle action = inputActions.RegisterAction(context, "Owned", "Owned", "Test", InputBinding::Keyboard(Key::A));

    u32 calls = 0;
    {
        InputActionConnections connections(inputActions);
        REQUIRE(connections.Connect(action, [&calls](const InputActionEvent&)
        {
            calls++;
            return InputReply::Consumed;
        }).IsValid());
    }

    inputSystem.BeginFrame();
    inputActions.BeginFrame();
    inputSystem.QueueKeyboardEvent(Key::A, InputPhase::Pressed);
    inputSystem.ProcessEvents();
    CHECK(calls == 0);
}

TEST_CASE("Action contexts reserve the raw context used by binding capture", "[Input]")
{
    InputSystem inputSystem;
    InputActionSystem inputActions(inputSystem);

    for (u32 contextIndex = 0; contextIndex < InputActionSystem::MAX_CONTEXTS; contextIndex++)
    {
        const InputActionContextHandle context = inputActions.CreateContext("Context" + std::to_string(contextIndex), GameInputPriority::Gameplay);
        REQUIRE(context.IsValid());
    }

    CHECK_FALSE(inputActions.CreateContext("OverCapacity", GameInputPriority::Gameplay).IsValid());
}

TEST_CASE("Destroying the action system does not dispatch callbacks into torn-down owners", "[Input]")
{
    InputSystem inputSystem;
    u32 calls = 0;
    {
        InputActionSystem inputActions(inputSystem);
        const InputActionContextHandle context = inputActions.CreateContext("Gameplay", GameInputPriority::Gameplay);
        REQUIRE(inputActions.SetContextActive(context, true));
        inputActions.RegisterAction(context, "Held", "Held", "Test", InputBinding::Keyboard(Key::A), [&calls](const InputActionEvent&)
        {
            calls++;
            return InputReply::Consumed;
        });

        inputSystem.QueueKeyboardEvent(Key::A, InputPhase::Pressed);
        inputSystem.ProcessEvents();
        REQUIRE(calls == 1);
    }

    CHECK(calls == 1);
}

TEST_CASE("Raw contexts can destroy themselves without mutating an executing callback", "[Input]")
{
    InputSystem inputSystem;
    std::vector<InputPhase> phases;
    InputContextHandle context;
    context = inputSystem.CreateContext("SelfDestroying", GameInputPriority::Gameplay, [&](const InputEvent& event)
    {
        phases.push_back(event.phase);
        if (event.phase == InputPhase::Pressed)
            CHECK(inputSystem.DestroyContext(context));
        return InputReply::Consumed;
    });
    REQUIRE(inputSystem.SetContextActive(context, true));

    inputSystem.BeginFrame();
    inputSystem.QueueKeyboardEvent(Key::A, InputPhase::Pressed);
    inputSystem.ProcessEvents();

    REQUIRE(phases.size() == 2);
    CHECK(phases[0] == InputPhase::Pressed);
    CHECK(phases[1] == InputPhase::Canceled);
    CHECK_FALSE(inputSystem.IsContextActive(context));
}

TEST_CASE("Rebinding an active action pairs its press with cancellation", "[Input]")
{
    InputSystem inputSystem;
    InputActionSystem inputActions(inputSystem);

    const InputActionContextHandle context = inputActions.CreateContext("Gameplay", GameInputPriority::Gameplay);
    REQUIRE(inputActions.SetContextActive(context, true));

    std::vector<InputPhase> phases;
    const InputActionHandle action = inputActions.RegisterAction(context, "RebindActive", "Rebind Active", "Test", InputBinding::Keyboard(Key::A), [&phases](const InputActionEvent& event)
    {
        phases.push_back(event.phase);
        return InputReply::Consumed;
    });

    inputSystem.BeginFrame();
    inputActions.BeginFrame();
    inputSystem.QueueKeyboardEvent(Key::A, InputPhase::Pressed);
    inputSystem.ProcessEvents();
    REQUIRE(inputActions.IsDown(action));

    REQUIRE(inputActions.SetBinding(action, 0, InputBinding::Keyboard(Key::B)).Succeeded());
    REQUIRE(phases.size() == 2);
    CHECK(phases[0] == InputPhase::Pressed);
    CHECK(phases[1] == InputPhase::Canceled);
    CHECK_FALSE(inputActions.IsDown(action));
    CHECK(inputActions.WasReleased(action));
}

TEST_CASE("Binding capture consumes keyboard mouse and wheel input before gameplay", "[Input]")
{
    InputSystem inputSystem;
    InputActionSystem inputActions(inputSystem);
    u32 gameplayEvents = 0;
    const InputContextHandle gameplay = inputSystem.CreateContext("GameplayRaw", GameInputPriority::Gameplay, [&gameplayEvents](const InputEvent&)
    {
        gameplayEvents++;
        return InputReply::Handled;
    });
    REQUIRE(inputSystem.SetContextActive(gameplay, true));

    std::optional<InputBinding> captured;
    REQUIRE(inputActions.BeginBindingCapture([&captured](std::optional<InputBinding> binding) { captured = binding; }));
    inputSystem.QueueKeyboardEvent(Key::K, InputPhase::Pressed, InputModifier::Control | InputModifier::Shift);
    inputSystem.ProcessEvents();
    REQUIRE(captured);
    CHECK(*captured == InputBinding::Keyboard(Key::K, InputModifier::Control | InputModifier::Shift));
    CHECK(gameplayEvents == 0);

    inputSystem.BeginFrame();
    inputSystem.QueueKeyboardEvent(Key::K, InputPhase::Released);
    inputSystem.ProcessEvents();
    CHECK(gameplayEvents == 0);

    captured.reset();
    inputSystem.BeginFrame();
    REQUIRE(inputActions.BeginBindingCapture([&captured](std::optional<InputBinding> binding) { captured = binding; }));
    inputSystem.QueueMouseButtonEvent(MouseButton::Middle, InputPhase::Pressed);
    inputSystem.ProcessEvents();
    REQUIRE(captured);
    CHECK(*captured == InputBinding::Mouse(MouseButton::Middle));
    CHECK(gameplayEvents == 0);

    inputSystem.BeginFrame();
    inputSystem.QueueMouseButtonEvent(MouseButton::Middle, InputPhase::Released);
    inputSystem.ProcessEvents();
    CHECK(gameplayEvents == 0);

    captured.reset();
    inputSystem.BeginFrame();
    REQUIRE(inputActions.BeginBindingCapture([&captured](std::optional<InputBinding> binding) { captured = binding; }));
    inputSystem.QueueScrollEvent(0.0f, -1.0f);
    inputSystem.ProcessEvents();
    REQUIRE(captured);
    CHECK(*captured == InputBinding::MouseWheel(MouseWheelDirection::Down));
    CHECK(gameplayEvents == 0);
}

TEST_CASE("Binding capture cancels on Escape and focus loss while ignoring modifier-only presses", "[Input]")
{
    InputSystem inputSystem;
    InputActionSystem inputActions(inputSystem);
    u32 cancellations = 0;
    REQUIRE(inputActions.BeginBindingCapture([&cancellations](std::optional<InputBinding> binding)
    {
        if (!binding)
            cancellations++;
    }));

    inputSystem.QueueKeyboardEvent(Key::LeftShift, InputPhase::Pressed, InputModifier::Shift);
    inputSystem.ProcessEvents();
    CHECK(inputActions.IsBindingCaptureActive());

    inputSystem.BeginFrame();
    inputSystem.QueueKeyboardEvent(Key::Escape, InputPhase::Pressed);
    inputSystem.ProcessEvents();
    CHECK_FALSE(inputActions.IsBindingCaptureActive());
    CHECK(cancellations == 1);

    REQUIRE(inputActions.BeginBindingCapture([&cancellations](std::optional<InputBinding> binding)
    {
        if (!binding)
            cancellations++;
    }));
    inputSystem.BeginFrame();
    inputSystem.QueueFocusEvent(false);
    inputSystem.ProcessEvents();
    CHECK_FALSE(inputActions.IsBindingCaptureActive());
    CHECK(cancellations == 2);
}

TEST_CASE("Binding capture ignores the button held when capture begins", "[Input]")
{
    InputSystem inputSystem;
    InputActionSystem inputActions(inputSystem);

    inputSystem.QueueMouseButtonEvent(MouseButton::Left, InputPhase::Pressed);
    inputSystem.ProcessEvents();

    std::optional<InputBinding> captured;
    REQUIRE(inputActions.BeginBindingCapture([&captured](std::optional<InputBinding> binding) { captured = binding; }));

    inputSystem.BeginFrame();
    inputSystem.QueueMouseButtonEvent(MouseButton::Left, InputPhase::Released);
    inputSystem.ProcessEvents();
    CHECK_FALSE(captured);
    CHECK(inputActions.IsBindingCaptureActive());

    inputSystem.BeginFrame();
    inputSystem.QueueKeyboardEvent(Key::K, InputPhase::Pressed);
    inputSystem.ProcessEvents();
    REQUIRE(captured);
    CHECK(*captured == InputBinding::Keyboard(Key::K));
}

TEST_CASE("Binding persistence round-trips overrides and applies them to late registrations", "[Input]")
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "NovusInputBindingsTest.json";
    std::error_code error;
    std::filesystem::remove(path, error);

    {
        InputSystem inputSystem;
        InputActionSystem inputActions(inputSystem, path);
        const InputActionContextHandle context = inputActions.CreateContext("Gameplay", GameInputPriority::Gameplay);
        const InputActionHandle action = inputActions.RegisterAction(context, "Jump", "Jump", "Movement", InputBinding::Keyboard(Key::Space));
        REQUIRE(inputActions.SetBinding(action, 0, InputBinding::Mouse(MouseButton::Middle, InputModifier::Alt)).Succeeded());
        REQUIRE(inputActions.SaveBindings());
    }

    {
        InputSystem inputSystem;
        InputActionSystem inputActions(inputSystem, path);
        const InputActionContextHandle context = inputActions.CreateContext("Gameplay", GameInputPriority::Gameplay);
        const InputActionHandle action = inputActions.RegisterAction(context, "Jump", "Jump", "Movement", InputBinding::Keyboard(Key::Space));
        REQUIRE(inputActions.GetActionInfo(action)->bindings[0]);
        CHECK(*inputActions.GetActionInfo(action)->bindings[0] == InputBinding::Mouse(MouseButton::Middle, InputModifier::Alt));
    }

    std::filesystem::remove(path, error);
}

TEST_CASE("Invalid persisted bindings are ignored and non-rebindable actions keep authored defaults", "[Input]")
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "NovusInvalidInputBindingsTest.json";
    {
        std::ofstream stream(path);
        stream << R"({"version":1,"actions":{"Invalid":{"1":{"device":99,"code":999,"modifiers":64,"modifierMatch":9}},"Truncated":{"1":{"device":1,"code":65601,"modifiers":0,"modifierMatch":0}},"Locked":{"1":{"device":1,"code":66,"modifiers":0,"modifierMatch":0}}}})";
    }

    InputSystem inputSystem;
    InputActionSystem inputActions(inputSystem, path);
    const InputActionContextHandle context = inputActions.CreateContext("Gameplay", GameInputPriority::Gameplay);
    const InputActionHandle invalid = inputActions.RegisterAction(context, "Invalid", "Invalid", "Test", InputBinding::Keyboard(Key::A));
    const InputActionHandle truncated = inputActions.RegisterAction(context, "Truncated", "Truncated", "Test", InputBinding::Keyboard(Key::B));

    InputActionDesc lockedDesc;
    lockedDesc.name = "Locked";
    lockedDesc.defaultBindings[0] = InputBinding::Keyboard(Key::A);
    lockedDesc.defaultBindings[1] = InputBinding::Mouse(MouseButton::Middle);
    lockedDesc.rebindable = false;
    const InputActionHandle locked = inputActions.RegisterAction(context, lockedDesc);

    CHECK(inputActions.GetActionInfo(invalid)->bindings[0] == InputBinding::Keyboard(Key::A));
    CHECK(inputActions.GetActionInfo(truncated)->bindings[0] == InputBinding::Keyboard(Key::B));
    CHECK(inputActions.GetActionInfo(locked)->bindings == lockedDesc.defaultBindings);

    std::error_code error;
    std::filesystem::remove(path, error);
}
