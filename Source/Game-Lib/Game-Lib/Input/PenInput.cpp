#include "PenInput.h"

#include <Input/InputSystem.h>

#include <Renderer/Window.h>

#if defined(_WIN32)
#include <Base/Util/DebugHandler.h>

#include <GLFW/glfw3.h>
#if !defined(GLFW_EXPOSE_NATIVE_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#endif
#include <GLFW/glfw3native.h>

#include <CommCtrl.h>

#include <limits>

#pragma comment(lib, "Comctl32.lib")

struct PenInputState
{
public:
    static constexpr u32 INVALID_POINTER_ID = std::numeric_limits<u32>::max();

    InputSystem* inputSystem = nullptr;
    HWND windowHandle = nullptr;
    u32 activePointerID = INVALID_POINTER_ID;
};

namespace
{
    void ResetPenState(PenInputState& state)
    {
        state.activePointerID = PenInputState::INVALID_POINTER_ID;
        state.inputSystem->SetPenState(0.0f, false, false, vec2(0.0f));
    }

    bool UpdatePenState(PenInputState& state, WPARAM wParam)
    {
        const u32 pointerID = GET_POINTERID_WPARAM(wParam);
        POINTER_INPUT_TYPE pointerType = PT_POINTER;
        if (!GetPointerType(pointerID, &pointerType) || pointerType != PT_PEN)
            return false;

        POINTER_PEN_INFO penInfo = {};
        if (!GetPointerPenInfo(pointerID, &penInfo))
            return false;

        const bool isPrimary = (penInfo.pointerInfo.pointerFlags & POINTER_FLAG_PRIMARY) != 0;
        if (state.activePointerID != PenInputState::INVALID_POINTER_ID && state.activePointerID != pointerID && !isPrimary)
            return false;

        state.activePointerID = pointerID;

        const bool inContact = (penInfo.pointerInfo.pointerFlags & POINTER_FLAG_INCONTACT) != 0;
        const bool inRange = (penInfo.pointerInfo.pointerFlags & POINTER_FLAG_INRANGE) != 0;
        f32 pressure = 0.0f;
        if (inContact)
            pressure = (penInfo.penMask & PEN_MASK_PRESSURE) != 0 ? static_cast<f32>(penInfo.pressure) / 1024.0f : 1.0f;

        const vec2 position(static_cast<f32>(penInfo.pointerInfo.ptPixelLocation.x), static_cast<f32>(penInfo.pointerInfo.ptPixelLocation.y));
        state.inputSystem->SetPenState(pressure, inRange, inContact, position);
        return true;
    }

    LRESULT CALLBACK PenInputWindowProcedure(HWND windowHandle, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR subclassID, DWORD_PTR referenceData)
    {
        PenInputState& state = *reinterpret_cast<PenInputState*>(referenceData);

        switch (message)
        {
            case WM_POINTERENTER:
            case WM_POINTERDOWN:
            case WM_POINTERUPDATE:
            case WM_POINTERUP:
                if (!UpdatePenState(state, wParam) && message == WM_POINTERUP && state.activePointerID == GET_POINTERID_WPARAM(wParam))
                    ResetPenState(state);
                break;
            case WM_POINTERLEAVE:
            case WM_POINTERCAPTURECHANGED:
            {
                const u32 pointerID = GET_POINTERID_WPARAM(wParam);
                if (state.activePointerID == pointerID)
                    ResetPenState(state);
                break;
            }
            case WM_KILLFOCUS:
            case WM_CANCELMODE:
                ResetPenState(state);
                break;
            case WM_NCDESTROY:
                ResetPenState(state);
                RemoveWindowSubclass(windowHandle, PenInputWindowProcedure, subclassID);
                state.windowHandle = nullptr;
                break;
        }

        return DefSubclassProc(windowHandle, message, wParam, lParam);
    }
}

#else

struct PenInputState {};

#endif

PenInput::~PenInput()
{
    Shutdown();
}

void PenInput::Initialize(Novus::Window& window, InputSystem& inputSystem)
{
    Shutdown();

#if defined(_WIN32)
    HWND windowHandle = glfwGetWin32Window(window.GetWindow());
    if (!windowHandle)
    {
        NC_LOG_WARNING("PenInput: Failed to retrieve the Win32 window handle");
        return;
    }

    PenInputState* state = new PenInputState();
    state->inputSystem = &inputSystem;
    state->windowHandle = windowHandle;

    const UINT_PTR subclassID = reinterpret_cast<UINT_PTR>(state);
    if (!SetWindowSubclass(windowHandle, PenInputWindowProcedure, subclassID, reinterpret_cast<DWORD_PTR>(state)))
    {
        NC_LOG_WARNING("PenInput: Failed to install the Win32 pointer message handler");
        delete state;
        return;
    }

    _state = state;
#else
    (void)window;
    (void)inputSystem;
#endif
}

void PenInput::Shutdown()
{
    if (!_state)
        return;

#if defined(_WIN32)
    _state->inputSystem->SetPenState(0.0f, false, false, vec2(0.0f));
    if (_state->windowHandle)
        RemoveWindowSubclass(_state->windowHandle, PenInputWindowProcedure, reinterpret_cast<UINT_PTR>(_state));
#endif

    delete _state;
    _state = nullptr;
}
