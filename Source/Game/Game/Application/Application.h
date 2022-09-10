#pragma once
#include "Message.h"

#include <Base/Container/ConcurrentQueue.h>

#include <json/json.hpp>

class GameRenderer;
class ModelLoader;
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

	nlohmann::json _cvarJson;

	moodycamel::ConcurrentQueue<MessageInbound> _messagesInbound;
	moodycamel::ConcurrentQueue<MessageOutbound> _messagesOutbound;
};