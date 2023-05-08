#pragma once
#include "RenderResources.h"

#include <Base/Types.h>
#include <Base/Memory/StackAllocator.h>

#include <robinhood/robinhood.h>

namespace Renderer
{
	class Renderer;
}
struct GLFWwindow;
struct GLFWimage;
struct GLFWcursor;
class Window;
class InputManager;
class TerrainRenderer;
class TerrainLoader;
class ModelRenderer;
class ModelLoader;
class MaterialRenderer;
class SkyboxRenderer;
class EditorRenderer;
class DebugRenderer;
class UIRenderer;
class PixelQuery;

class GameRenderer
{
public:
	GameRenderer();
	~GameRenderer();

	bool UpdateWindow(f32 deltaTime);
	void UpdateRenderers(f32 deltaTime);
	f32 Render();

	void ReloadShaders(bool forceRecompileAll);

	bool AddCursor(u32 nameHash, const std::string& path);
	bool SetCursor(u32 nameHash, u32 imguiMouseCursor = 0);

	InputManager* GetInputManager() { return _inputManager; }
	Renderer::Renderer* GetRenderer() { return _renderer; }

	TerrainRenderer* GetTerrainRenderer() { return _terrainRenderer; }
	ModelRenderer* GetModelRenderer() { return _modelRenderer; }
	DebugRenderer* GetDebugRenderer() { return _debugRenderer; }

	TerrainLoader* GetTerrainLoader() { return _terrainLoader; }
	ModelLoader* GetModelLoader() { return _modelLoader; }

	RenderResources& GetRenderResources() { return _resources; }
	PixelQuery* GetPixelQuery() { return _pixelQuery; }

	Window* GetWindow() { return _window; }
	const std::string& GetGPUName();

private:
	void CreatePermanentResources();

	void InitImgui();

private:
	struct Cursor
	{
		GLFWimage* image = nullptr;
		GLFWcursor* cursor = nullptr;
	};

	Renderer::Renderer* _renderer = nullptr;
	Window* _window = nullptr;
	InputManager* _inputManager = nullptr;
	PixelQuery* _pixelQuery = nullptr;

	Memory::StackAllocator* _frameAllocator[2];

	u8 _frameIndex = 0;
	RenderResources _resources;

	// Sub Renderers
	TerrainRenderer* _terrainRenderer = nullptr;
	TerrainLoader* _terrainLoader = nullptr;

	ModelRenderer* _modelRenderer = nullptr;
	ModelLoader* _modelLoader = nullptr;

	MaterialRenderer* _materialRenderer = nullptr;
	SkyboxRenderer* _skyboxRenderer = nullptr;
	DebugRenderer* _debugRenderer = nullptr;
	EditorRenderer* _editorRenderer = nullptr;
	UIRenderer* _uiRenderer = nullptr;

	robin_hood::unordered_map<u32, Cursor> _nameHashToCursor;
};