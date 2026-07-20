#pragma once
#include "Message.h"
#include "EnttRegistries.h"

#include <Base/Container/ConcurrentQueue.h>

#include <json/json.hpp>

#include <atomic>


namespace Editor
{
    class EditorHandler;
}
class ImGuiInputBridge;
class InputActionSystem;
class InputPerformanceTest;
class InputSystem;
class GameRenderer;
class ModelLoader;

namespace enki
{
    class TaskScheduler;
}
namespace ECS
{
    class Scheduler;
}
namespace Scripting
{
    class LuaManager;
}
namespace Util
{
    class AssetWriter;
}

class Application
{
public:
    Application();
    ~Application();

    void Start(bool startInSeparateThread);
    void Stop();
    void RequestExit();

    void PassMessage(MessageInbound& message);
    bool TryGetMessageOutbound(MessageOutbound& message);

    bool IsRunning() { return _isRunning; }
    bool Tick(f32 deltaTime);

private:
    void Run();

    bool Init();
    bool Render(f32 deltaTime, f32& timeSpentWaiting);

    void DatabaseReload();
    void SaveCDB();

    void Cleanup();

private:
    std::atomic_bool _isRunning = false;
    std::atomic_bool _exitRequested = false;

    InputSystem* _inputSystem = nullptr;
    InputActionSystem* _inputActionSystem = nullptr;
    InputPerformanceTest* _inputPerformanceTest = nullptr;
    ImGuiInputBridge* _imguiInputBridge = nullptr;
    GameRenderer* _gameRenderer = nullptr;

    Editor::EditorHandler* _editorHandler = nullptr;

    EnttRegistries _registries;
    enki::TaskScheduler* _taskScheduler = nullptr;

    ECS::Scheduler* _ecsScheduler = nullptr;
    Scripting::LuaManager* _luaManager = nullptr;
    Util::AssetWriter* _assetWriter = nullptr;

    nlohmann::json _cvarJson;

    moodycamel::ConcurrentQueue<MessageInbound> _messagesInbound;
    moodycamel::ConcurrentQueue<MessageOutbound> _messagesOutbound;
};
