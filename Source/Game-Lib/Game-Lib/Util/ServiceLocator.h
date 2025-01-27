#pragma once
#include <cassert>

#include <Game-Lib/Application/EnttRegistries.h>

namespace Editor
{
    class EditorHandler;
}
class InputManager;
class IOLoader;
class GameRenderer;

namespace enki
{
    class TaskScheduler;
}

struct EnttRegistries;
class GameConsole;

namespace Scripting
{
    class LuaManager;
}

class ServiceLocator
{
public:
    static Editor::EditorHandler* GetEditorHandler()
    {
        assert(_editorHandler != nullptr);
        return _editorHandler;
    }
    static void SetEditorHandler(Editor::EditorHandler* editorHandler);

    static InputManager* GetInputManager()
    {
        assert(_inputManager != nullptr);
        return _inputManager;
    }
    static void SetInputManager(InputManager* inputManager);

    static IOLoader* GetIOLoader()
    {
        assert(_ioLoader != nullptr);
        return _ioLoader;
    }
    static void SetIOLoader(IOLoader* ioLoader);

    static GameRenderer* GetGameRenderer()
    {
        assert(_gameRenderer != nullptr);
        return _gameRenderer;
    }
    static void SetGameRenderer(GameRenderer* gameRenderer);

    static enki::TaskScheduler* GetTaskScheduler()
    {
        assert(_taskScheduler != nullptr);
        return _taskScheduler;
    }
    static void SetTaskScheduler(enki::TaskScheduler* taskScheduler);

    static EnttRegistries* GetEnttRegistries()
    {
        assert(_enttRegistries != nullptr);
        return _enttRegistries;
    }
    static void SetEnttRegistries(EnttRegistries* enttRegistries);

    static GameConsole* GetGameConsole()
    {
        assert(_gameConsole != nullptr);
        return _gameConsole;
    }
    static void SetGameConsole(GameConsole* gameConsole);

    static Scripting::LuaManager* GetLuaManager()
    {
        assert(_luaManager != nullptr);
        return _luaManager;
    }
    static void SetLuaManager(Scripting::LuaManager* luaManager);

private:
    ServiceLocator() { }
    static Editor::EditorHandler* _editorHandler;
    static InputManager* _inputManager;
    static IOLoader* _ioLoader;
    static GameRenderer* _gameRenderer;
    static enki::TaskScheduler* _taskScheduler;
    static EnttRegistries* _enttRegistries;
    static GameConsole* _gameConsole;
    static Scripting::LuaManager* _luaManager;
};