#include "InputPerformanceTest.h"
#include "InputActionSystem.h"

#include "Game-Lib/Gameplay/GameConsole/GameConsole.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Base/CVarSystem/CVarSystem.h>
#include <Base/Util/DebugHandler.h>

#include <algorithm>
#include <limits>
#include <numeric>
#include <string>
#include <string_view>

namespace
{
    AutoCVar_Int CVAR_InputBenchmarkSynthetic(CVarCategory::Client, "inputBenchmarkSynthetic", "Run the isolated synthetic input benchmark once", 0, CVarFlags::EditCheckbox | CVarFlags::DoNotSave);
    AutoCVar_Int CVAR_InputBenchmarkLive(CVarCategory::Client, "inputBenchmarkLive", "Capture live input dispatch performance once", 0, CVarFlags::EditCheckbox | CVarFlags::DoNotSave);
    AutoCVar_Float CVAR_InputBenchmarkDuration(CVarCategory::Client, "inputBenchmarkDuration", "Input benchmark duration in seconds", 10.0, CVarFlags::EditFloatDrag);
    AutoCVar_Int CVAR_InputBenchmarkActions(CVarCategory::Client, "inputBenchmarkActions", "Actions registered by the synthetic input benchmark", 1000, CVarFlags::Advanced);
    AutoCVar_Int CVAR_InputBenchmarkContexts(CVarCategory::Client, "inputBenchmarkContexts", "Active contexts created by the synthetic input benchmark", 12, CVarFlags::Advanced);
    AutoCVar_Int CVAR_InputBenchmarkEventsPerFrame(CVarCategory::Client, "inputBenchmarkEventsPerFrame", "Raw button events dispatched per synthetic benchmark frame", 256, CVarFlags::Advanced);

    f64 ToMicroseconds(std::chrono::steady_clock::duration duration)
    {
        return std::chrono::duration<f64, std::micro>(duration).count();
    }
}

InputPerformanceTest::InputPerformanceTest(InputSystem& inputSystem, InputActionSystem& inputActions)
    : _inputSystem(inputSystem), _inputActions(inputActions)
{
}

void InputPerformanceTest::BeginFrame()
{
    if (CVAR_InputBenchmarkLive.Get() != 0 && !_liveCapture.active)
    {
        CVAR_InputBenchmarkLive.Set(0);
        StartLiveCapture();
    }

    if (CVAR_InputBenchmarkSynthetic.Get() != 0 && !_syntheticCapture.active)
    {
        CVAR_InputBenchmarkSynthetic.Set(0);
        StartSyntheticCapture();
    }
}

void InputPerformanceTest::BeginLiveDispatch()
{
    if (_liveCapture.active)
        _liveDispatchStart = std::chrono::steady_clock::now();
}

void InputPerformanceTest::EndLiveDispatch()
{
    if (!_liveCapture.active)
        return;

    _liveCapture.dispatchMicroseconds.push_back(ToMicroseconds(std::chrono::steady_clock::now() - _liveDispatchStart));

    const InputSystemFrameMetrics& inputMetrics = _inputSystem.GetFrameMetrics();
    const InputActionFrameMetrics& actionMetrics = _inputActions.GetFrameMetrics();

    _liveCapture.routedEvents += inputMetrics.queuedEvents;
    _liveCapture.contextCallbacks += inputMetrics.contextCallbacks;
    _liveCapture.actionEvents += actionMetrics.actionEvents;
    _liveCapture.listenerCallbacks += actionMetrics.listenerCallbacks;
}

void InputPerformanceTest::Update(f32 deltaTime)
{
    if (_syntheticCapture.active)
        RunSyntheticFrame();

    const f64 duration = std::max(CVAR_InputBenchmarkDuration.GetFloat(), 0.1f);
    if (_liveCapture.active)
    {
        _liveCapture.elapsedSeconds += deltaTime;

        if (_liveCapture.elapsedSeconds >= duration)
            FinishCapture(_liveCapture, "live");
    }

    if (_syntheticCapture.active)
    {
        _syntheticCapture.elapsedSeconds += deltaTime;

        if (_syntheticCapture.elapsedSeconds >= duration)
            FinishCapture(_syntheticCapture, "synthetic");
    }
}

void InputPerformanceTest::StartLiveCapture()
{
    _liveCapture = {};
    _liveCapture.active = true;
    _liveCapture.dispatchMicroseconds.reserve(4096);
    _inputSystem.SetMetricsEnabled(true);
    _inputActions.SetMetricsEnabled(true);

    NC_LOG_INFO("Input performance: started live capture for {:.2f} seconds", std::max(CVAR_InputBenchmarkDuration.GetFloat(), 0.1f));
}

void InputPerformanceTest::StartSyntheticCapture()
{
    _syntheticInputSystem = std::make_unique<InputSystem>();
    _syntheticInputActions = std::make_unique<InputActionSystem>(*_syntheticInputSystem);

    const u32 contextCount = static_cast<u32>(std::clamp(CVAR_InputBenchmarkContexts.Get(), 1, static_cast<i32>(InputActionSystem::MAX_CONTEXTS)));
    const u32 actionCount = static_cast<u32>(std::clamp(CVAR_InputBenchmarkActions.Get(), 1, static_cast<i32>(std::numeric_limits<u16>::max() - 1)));

    std::vector<InputActionContextHandle> contexts;
    contexts.reserve(contextCount);
    for (u32 contextIndex = 0; contextIndex < contextCount; contextIndex++)
    {
        InputActionContextHandle context = _syntheticInputActions->CreateContext("InputBenchmarkContext" + std::to_string(contextIndex), GameInputPriority::Gameplay + static_cast<i32>(contextIndex));
        _syntheticInputActions->SetContextActive(context, true);
        contexts.push_back(context);
    }

    for (u32 actionIndex = 0; actionIndex < actionCount; actionIndex++)
    {
        const std::string actionName = "InputBenchmarkAction" + std::to_string(actionIndex);
        _syntheticInputActions->RegisterAction(contexts[actionIndex % contextCount], actionName, actionName, "Benchmark",
            InputBinding::Keyboard(static_cast<Key>(static_cast<u16>(Key::A) + (actionIndex % 26))),
            { .defaultReply = InputReply::Handled, .rebindable = false }, [](const InputActionEvent&)
        {
            return InputReply::Handled;
        });
    }

    _syntheticInputSystem->SetMetricsEnabled(true);
    _syntheticInputActions->SetMetricsEnabled(true);
    _syntheticCapture = {};
    _syntheticCapture.active = true;
    _syntheticCapture.dispatchMicroseconds.reserve(4096);
    _syntheticEventCursor = 0;

    NC_LOG_INFO("Input performance: started synthetic capture with {} actions, {} contexts, and {} events/frame for {:.2f} seconds", actionCount, contextCount, std::clamp(CVAR_InputBenchmarkEventsPerFrame.Get(), 2, static_cast<i32>(InputSystem::MAX_EVENTS_PER_FRAME)), std::max(CVAR_InputBenchmarkDuration.GetFloat(), 0.1f));
}

void InputPerformanceTest::RunSyntheticFrame()
{
    _syntheticInputSystem->BeginFrame();
    _syntheticInputActions->BeginFrame();

    i32 eventCount = std::clamp(CVAR_InputBenchmarkEventsPerFrame.Get(), 2, static_cast<i32>(InputSystem::MAX_EVENTS_PER_FRAME));
    eventCount -= eventCount % 2;
    for (i32 eventIndex = 0; eventIndex < eventCount; eventIndex += 2)
    {
        const Key key = static_cast<Key>(static_cast<u16>(Key::A) + (_syntheticEventCursor++ % 26));
        _syntheticInputSystem->QueueKeyboardEvent(key, InputPhase::Pressed);
        _syntheticInputSystem->QueueKeyboardEvent(key, InputPhase::Released);
    }

    const auto dispatchStart = std::chrono::steady_clock::now();
    _syntheticInputSystem->ProcessEvents();
    _syntheticCapture.dispatchMicroseconds.push_back(ToMicroseconds(std::chrono::steady_clock::now() - dispatchStart));

    const InputSystemFrameMetrics& inputMetrics = _syntheticInputSystem->GetFrameMetrics();
    const InputActionFrameMetrics& actionMetrics = _syntheticInputActions->GetFrameMetrics();
    _syntheticCapture.routedEvents += inputMetrics.queuedEvents;
    _syntheticCapture.contextCallbacks += inputMetrics.contextCallbacks;
    _syntheticCapture.actionEvents += actionMetrics.actionEvents;
    _syntheticCapture.listenerCallbacks += actionMetrics.listenerCallbacks;
}

void InputPerformanceTest::FinishCapture(Capture& capture, const char* name)
{
    capture.active = false;
    if (std::string_view(name) == "live")
    {
        _inputSystem.SetMetricsEnabled(false);
        _inputActions.SetMetricsEnabled(false);
    }

    if (capture.dispatchMicroseconds.empty())
    {
        NC_LOG_WARNING("Input performance: {} capture ended without samples", name);
        return;
    }

    std::vector<f64> sortedSamples = capture.dispatchMicroseconds;
    std::sort(sortedSamples.begin(), sortedSamples.end());
    const f64 totalMicroseconds = std::accumulate(sortedSamples.begin(), sortedSamples.end(), 0.0);
    const f64 average = totalMicroseconds / static_cast<f64>(sortedSamples.size());
    const f64 measuredSeconds = std::max(capture.elapsedSeconds, 0.000001);
    const f64 dispatchSeconds = std::max(totalMicroseconds / 1'000'000.0, 0.000001);
    const f64 inputRate = static_cast<f64>(capture.routedEvents) / measuredSeconds;
    const f64 routedEventCapacity = static_cast<f64>(capture.routedEvents) / dispatchSeconds;
    const f64 actionEventCapacity = static_cast<f64>(capture.actionEvents) / dispatchSeconds;
    const f64 p50 = Percentile(sortedSamples, 0.50);
    const f64 p95 = Percentile(sortedSamples, 0.95);
    const f64 p99 = Percentile(sortedSamples, 0.99);

    if (GameConsole* console = ServiceLocator::GetGameConsole())
    {
        console->Print("Input performance [%s]: %zu frames, %llu routed events, %llu context callbacks, %llu action events, %llu listener callbacks", name, sortedSamples.size(), capture.routedEvents, capture.contextCallbacks, capture.actionEvents, capture.listenerCallbacks);
        console->Print("Input performance [%s]: dispatch us avg=%.3f p50=%.3f p95=%.3f p99=%.3f max=%.3f", name, average, p50, p95, p99, sortedSamples.back());
        console->Print("Input performance [%s]: input rate=%.0f routed events/s", name, inputRate);
        
        if (std::string_view(name) == "synthetic")
            console->Print("Input performance [%s]: measured dispatch capacity=%.0f routed events/s, action throughput=%.0f action events/s", name, routedEventCapacity, actionEventCapacity);
    }
    else
    {
        NC_LOG_INFO("Input performance [{}]: {} frames, {} routed events, {} context callbacks, {} action events, {} listener callbacks", name, sortedSamples.size(), capture.routedEvents, capture.contextCallbacks, capture.actionEvents, capture.listenerCallbacks);
        NC_LOG_INFO("Input performance [{}]: dispatch us avg={:.3f} p50={:.3f} p95={:.3f} p99={:.3f} max={:.3f}", name, average, p50, p95, p99, sortedSamples.back());
        NC_LOG_INFO("Input performance [{}]: input rate={:.0f} routed events/s", name, inputRate);
        
        if (std::string_view(name) == "synthetic")
            NC_LOG_INFO("Input performance [{}]: measured dispatch capacity={:.0f} routed events/s, action throughput={:.0f} action events/s", name, routedEventCapacity, actionEventCapacity);
    }

    if (std::string_view(name) == "synthetic")
    {
        _syntheticInputActions.reset();
        _syntheticInputSystem.reset();
    }
}

f64 InputPerformanceTest::Percentile(const std::vector<f64>& sortedSamples, f64 percentile)
{
    const size_t index = static_cast<size_t>(percentile * static_cast<f64>(sortedSamples.size() - 1));
    return sortedSamples[index];
}
