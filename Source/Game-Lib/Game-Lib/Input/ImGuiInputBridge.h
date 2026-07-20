#pragma once
#include <Input/InputTypes.h>

class InputSystem;

class ImGuiInputBridge
{
public:
    explicit ImGuiInputBridge(InputSystem& inputSystem);
    ~ImGuiInputBridge();

private:
    InputReply HandleInput(const InputEvent& event);

private:
    InputSystem& _inputSystem;
    InputContextHandle _context;
};
