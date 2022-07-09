#pragma once
#include "RenderResources.h"

#include <Base/Types.h>
#include <Base/Memory/StackAllocator.h>

namespace Renderer
{
	class Renderer;
}
class Window;
class UIRenderer;

class GameRenderer
{
public:
	GameRenderer();
	~GameRenderer();

	bool UpdateWindow(f32 deltaTime);
	void UpdateRenderers(f32 deltaTime);
	void Render();

private:
	void CreatePermanentResources();

	void InitImgui();

private:
	Renderer::Renderer* _renderer = nullptr;
	Window* _window = nullptr;
	Memory::StackAllocator* _frameAllocator;

	u8 _frameIndex = 0;
	RenderResources _resources;

	// Sub Renderers
	UIRenderer* _uiRenderer = nullptr;
};