#pragma once

class InputSystem;
struct PenInputState;

namespace Novus
{
    class Window;
}

class PenInput
{
public:
    PenInput() = default;
    PenInput(const PenInput&) = delete;
    PenInput& operator=(const PenInput&) = delete;
    ~PenInput();

    void Initialize(Novus::Window& window, InputSystem& inputSystem);
    void Shutdown();

private:
    PenInputState* _state = nullptr;
};
