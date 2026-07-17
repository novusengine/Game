#include "ServiceLocator.h"

Application* ServiceLocator::_application = nullptr;
PACT::PactStorage* ServiceLocator::_pactStorage = nullptr;
Editor::EditorHandler* ServiceLocator::_editorHandler = nullptr;
InputSystem* ServiceLocator::_inputSystem = nullptr;
InputActionSystem* ServiceLocator::_inputActionSystem = nullptr;
GameRenderer* ServiceLocator::_gameRenderer = nullptr;
enki::TaskScheduler* ServiceLocator::_taskScheduler = nullptr;
EnttRegistries* ServiceLocator::_enttRegistries = nullptr;
GameConsole* ServiceLocator::_gameConsole = nullptr;
Scripting::LuaManager* ServiceLocator::_luaManager = nullptr;
Util::AssetWriter* ServiceLocator::_assetWriter = nullptr;

void ServiceLocator::SetApplication(Application* application)
{
    assert(_application == nullptr);
    _application = application;
}

void ServiceLocator::SetPactStorage(PACT::PactStorage* pactStorage)
{
    assert(_pactStorage == nullptr);
    _pactStorage = pactStorage;
}

void ServiceLocator::SetEditorHandler(Editor::EditorHandler* editorHandler)
{
    assert(_editorHandler == nullptr);
    _editorHandler = editorHandler;
}

void ServiceLocator::SetInputSystem(InputSystem* inputSystem)
{
    assert(_inputSystem == nullptr);
    _inputSystem = inputSystem;
}

void ServiceLocator::SetInputActionSystem(InputActionSystem* inputActionSystem)
{
    assert(_inputActionSystem == nullptr);
    _inputActionSystem = inputActionSystem;
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

void ServiceLocator::SetAssetWriter(Util::AssetWriter* assetWriter)
{
    assert(_assetWriter == nullptr);
    _assetWriter = assetWriter;
}
