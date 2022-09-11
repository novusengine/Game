#pragma once
#include "Message.h"
#include "EnttRegistries.h"

#include <Base/Container/ConcurrentQueue.h>

#include <json/json.hpp>

namespace Editor
{
	class EditorHandler;
}
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

class Application
{
public:
	Application();
	~Application();

	void Start();
	void Stop();

	void PassMessage(MessageInbound& message);
	bool TryGetMessageOutbound(MessageOutbound& message);

private:
	void Run();
	bool Init();
	bool Tick(f32 deltaTime);
	bool Render(f32 deltaTime);

	void Cleanup();

private:
	bool _isRunning = false;

	GameRenderer* _gameRenderer = nullptr;
	ModelLoader*  _modelLoader = nullptr;

	Editor::EditorHandler* _editorHandler = nullptr;

	EnttRegistries _registries;
	enki::TaskScheduler* _taskScheduler = nullptr;

	ECS::Scheduler* _ecsScheduler = nullptr;

	nlohmann::json _cvarJson;

	moodycamel::ConcurrentQueue<MessageInbound> _messagesInbound;
	moodycamel::ConcurrentQueue<MessageOutbound> _messagesOutbound;
};