#pragma once
#include "Message.h"
#include "EnttRegistries.h"

#include <Base/Container/ConcurrentQueue.h>

#include <json/json.hpp>

namespace Editor
{
    class EditorHandler;
}
class InputManager;
class IOLoader;
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

class Application
{
public:
    Application();
    ~Application();

    void Start(bool startInSeparateThread);
    void Stop();

    void PassMessage(MessageInbound& message);
    bool TryGetMessageOutbound(MessageOutbound& message);

    bool IsRunning() { return _isRunning; }
    bool Tick(f32 deltaTime);

private:
    void Run();
    void IOLoadThread();

    bool Init();
    bool Render(f32 deltaTime, f32& timeSpentWaiting);

    void RefreshDatabases();
    void SaveCDB();

    void Cleanup();

private:
    bool _isRunning = false;

    InputManager* _inputManager = nullptr;
    GameRenderer* _gameRenderer = nullptr;

    Editor::EditorHandler* _editorHandler = nullptr;

    EnttRegistries _registries;
    enki::TaskScheduler* _taskScheduler = nullptr;

    ECS::Scheduler* _ecsScheduler = nullptr;
    Scripting::LuaManager* _luaManager = nullptr;

    nlohmann::json _cvarJson;

    moodycamel::ConcurrentQueue<MessageInbound> _messagesInbound;
    moodycamel::ConcurrentQueue<MessageOutbound> _messagesOutbound;
};