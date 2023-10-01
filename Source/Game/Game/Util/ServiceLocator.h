#pragma once
#include <cassert>

#include <Game/Application/EnttRegistries.h>

namespace Editor
{
    class EditorHandler;
}
class InputManager;
class GameRenderer;
namespace ECS { namespace Components { struct DirtyTransformQueue; } }

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

namespace Animation
{
    class AnimationSystem;
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

    static Animation::AnimationSystem* GetAnimationSystem()
    {
        assert(_animationSystem != nullptr);
        return _animationSystem;
    }
    static void SetAnimationSystem(Animation::AnimationSystem* animationSystem);

    static ECS::Components::DirtyTransformQueue* GetTransformQueue()
    {
        assert(_dirtyTransformQueue != nullptr);
        return _dirtyTransformQueue;
    }
    static void SetTransformQueue(ECS::Components::DirtyTransformQueue* _dirtyTransformQueue);

private:
    ServiceLocator() { }
    static Editor::EditorHandler* _editorHandler;
    static InputManager* _inputManager;
    static GameRenderer* _gameRenderer;
    static enki::TaskScheduler* _taskScheduler;
    static EnttRegistries* _enttRegistries;
    static GameConsole* _gameConsole;
    static Scripting::LuaManager* _luaManager;
    static Animation::AnimationSystem* _animationSystem;
    static ECS::Components::DirtyTransformQueue* _dirtyTransformQueue;
};