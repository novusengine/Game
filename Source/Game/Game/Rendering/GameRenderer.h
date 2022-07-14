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
	ModelRenderer* GetModelRenderer() { return _modelRenderer; }

private:
	void CreatePermanentResources();

	void InitImgui();

	void CapturedMouseMoved(vec2 pos);

private:
	Renderer::Renderer* _renderer = nullptr;
	Window* _window = nullptr;
	InputManager* _inputManager = nullptr;

	bool _captureMouse = false;
	bool _captureMouseHasMoved = false;
	f32 _mouseSensitivity = 0.05f;
	f32 _cameraSpeed = 20.0f;

	vec2 _prevMousePosition = vec2(0, 0);

	Memory::StackAllocator* _frameAllocator;

	u8 _frameIndex = 0;
	RenderResources _resources;

	// Sub Renderers
	TerrainRenderer* _terrainRenderer = nullptr;
	ModelRenderer* _modelRenderer = nullptr;
	DebugRenderer* _debugRenderer = nullptr;
	UIRenderer* _uiRenderer = nullptr;
};