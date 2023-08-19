#include "ServiceLocator.h"

Editor::EditorHandler* ServiceLocator::_editorHandler = nullptr;
InputManager* ServiceLocator::_inputManager = nullptr;
GameRenderer* ServiceLocator::_gameRenderer = nullptr;
enki::TaskScheduler* ServiceLocator::_taskScheduler = nullptr;
EnttRegistries* ServiceLocator::_enttRegistries = nullptr;
GameConsole* ServiceLocator::_gameConsole = nullptr;
Scripting::LuaManager* ServiceLocator::_luaManager = nullptr;
Animation::AnimationSystem* ServiceLocator::_animationSystem = nullptr;

void ServiceLocator::SetEditorHandler(Editor::EditorHandler* editorHandler)
{
    assert(_editorHandler == nullptr);
    _editorHandler = editorHandler;
}

void ServiceLocator::SetInputManager(InputManager* inputManager)
{
    assert(_inputManager == nullptr);
    _inputManager = inputManager;
}

void ServiceLocator::SetGameRenderer(GameRenderer* gameRenderer)
{
    assert(_gameRenderer == nullptr);
    _gameRenderer = gameRenderer;
}

void ServiceLocator::SetTaskScheduler(enki::TaskScheduler* taskScheduler)
{
    assert(_taskScheduler == nullptr);
    _taskScheduler = taskScheduler;
}

void ServiceLocator::SetEnttRegistries(EnttRegistries* enttRegistries)
{
    assert(_enttRegistries == nullptr);
    _enttRegistries = enttRegistries;
}

void ServiceLocator::SetGameConsole(GameConsole* gameConsole)
{
    assert(_gameConsole == nullptr);
    _gameConsole = gameConsole;
}

void ServiceLocator::SetLuaManager(Scripting::LuaManager* luaManager)
{
    assert(_luaManager == nullptr);
    _luaManager = luaManager;
}

void ServiceLocator::SetAnimationSystem(Animation::AnimationSystem* animationSystem)
{
    assert(_animationSystem == nullptr);
    _animationSystem = animationSystem;
}
