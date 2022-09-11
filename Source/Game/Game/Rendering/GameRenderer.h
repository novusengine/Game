#pragma once
#include "RenderResources.h"

#include <Base/Types.h>
#include <Base/Memory/StackAllocator.h>

namespace Renderer
{
	class Renderer;
}
struct GLFWwindow;
class Window;
class InputManager;
class TerrainRenderer;
class ModelRenderer;
class DebugRenderer;
class UIRenderer;

class GameRenderer
{
public:
	GameRenderer();
	~GameRenderer();

	bool UpdateWindow(f32 deltaTime);
	void UpdateRenderers(f32 deltaTime);
	void Render();

	void ReloadShaders(bool forceRecompileAll);

	InputManager* GetInputManager() { return _inputManager; }
	Renderer::Renderer* GetRenderer() { return _renderer; }
	ModelRenderer* GetModelRenderer() { return _modelRenderer; }
	TerrainRenderer* GetTerrainRenderer() { return _terrainRenderer; }

	RenderResources& GetRenderResources() { return _resources; }

	Window* GetWindow() { return _window; }

private:
	void CreatePermanentResources();

	void InitImgui();

private:
	Renderer::Renderer* _renderer = nullptr;
	Window* _window = nullptr;
	InputManager* _inputManager = nullptr;

	Memory::StackAllocator* _frameAllocator;

	u8 _frameIndex = 0;
	RenderResources _resources;

	// Sub Renderers
	TerrainRenderer* _terrainRenderer = nullptr;
	ModelRenderer* _modelRenderer = nullptr;
	DebugRenderer* _debugRenderer = nullptr;
	UIRenderer* _uiRenderer = nullptr;
};