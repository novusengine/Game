#pragma once
#include <Base/Types.h>

#include <Input/InputSystem.h>

#include <chrono>
#include <memory>
#include <vector>

class InputActionSystem;

class InputPerformanceTest
{
public:
    InputPerformanceTest(InputSystem& inputSystem, InputActionSystem& inputActions);

    void BeginFrame();
    void BeginLiveDispatch();
    void EndLiveDispatch();
    void Update(f32 deltaTime);

private:
    struct Capture
    {
    public:
        std::vector<f64> dispatchMicroseconds;
        f64 elapsedSeconds = 0.0;
        u64 routedEvents = 0;
        u64 contextCallbacks = 0;
        u64 actionEvents = 0;
        u64 listenerCallbacks = 0;
        bool active = false;
    };

    void StartLiveCapture();
    void StartSyntheticCapture();
    void RunSyntheticFrame();
    void FinishCapture(Capture& capture, const char* name);
    static f64 Percentile(const std::vector<f64>& sortedSamples, f64 percentile);

private:
    InputSystem& _inputSystem;
    InputActionSystem& _inputActions;
    std::unique_ptr<InputSystem> _syntheticInputSystem;
    std::unique_ptr<InputActionSystem> _syntheticInputActions;
    Capture _liveCapture;
    Capture _syntheticCapture;
    std::chrono::steady_clock::time_point _liveDispatchStart;
    u32 _syntheticEventCursor = 0;
};
