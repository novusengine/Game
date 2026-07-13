#pragma once
#include <cassert>

#include <Game-Lib/Application/EnttRegistries.h>

namespace PACT
{
    class PactStorage;
}

namespace Editor
{
    class EditorHandler;
}
class InputManager;
class GameRenderer;

namespace enki
{
    class TaskScheduler;
}

struct EnttRegistries;
class Application;
class GameConsole;

namespace Scripting
{
    class LuaManager;
}

namespace Util
{
    class AssetWriter;
}

class ServiceLocator
{
public:
    static Application* GetApplication()
    {
        assert(_application != nullptr);
        return _application;
    }
    static void SetApplication(Application* application);

    static PACT::PactStorage* GetPactStorage()
    {
        assert(_pactStorage != nullptr);
        return _pactStorage;
    }
    static void SetPactStorage(PACT::PactStorage* pactStorage);

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

    static Util::AssetWriter* GetAssetWriter()
    {
        assert(_assetWriter != nullptr);
        return _assetWriter;
    }
    static void SetAssetWriter(Util::AssetWriter* assetWriter);

private:
    ServiceLocator() { }
    static Application* _application;
    static PACT::PactStorage* _pactStorage;
    static Editor::EditorHandler* _editorHandler;
    static InputManager* _inputManager;
    static GameRenderer* _gameRenderer;
    static enki::TaskScheduler* _taskScheduler;
    static EnttRegistries* _enttRegistries;
    static GameConsole* _gameConsole;
    static Scripting::LuaManager* _luaManager;
    static Util::AssetWriter* _assetWriter;
};
