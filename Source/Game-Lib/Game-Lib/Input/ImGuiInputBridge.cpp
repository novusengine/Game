#include "ImGuiInputBridge.h"

#include "InputActionSystem.h"

#include <Input/InputSystem.h>

#include <imgui/imgui.h>
#include <imgui/imguizmo/ImGuizmo.h>

ImGuiInputBridge::ImGuiInputBridge(InputSystem& inputSystem) : _inputSystem(inputSystem)
{
    _context = _inputSystem.CreateContext("ImGui", GameInputPriority::ImGui, [this](const InputEvent& event)
    {
        return HandleInput(event);
    });
    _inputSystem.SetContextActive(_context, true);
}

ImGuiInputBridge::~ImGuiInputBridge()
{
    _inputSystem.DestroyContext(_context);
}

InputReply ImGuiInputBridge::HandleInput(const InputEvent& event)
{
    const ImGuiIO& io = ImGui::GetIO();
    if (event.type == InputEventType::Text)
        return io.WantTextInput ? InputReply::Consumed : InputReply::Ignored;

    if (event.type == InputEventType::Button && event.control.device == InputDevice::Keyboard)
        return io.WantCaptureKeyboard ? InputReply::Consumed : InputReply::Ignored;

    const bool isMouseEvent = event.type == InputEventType::CursorMove || event.type == InputEventType::Scroll
        || (event.type == InputEventType::Button && event.control.device == InputDevice::Mouse);
    if (isMouseEvent && (io.WantCaptureMouse || ImGuizmo::IsOver() || ImGuizmo::IsUsing()))
        return InputReply::Consumed;

    return InputReply::Ignored;
}
