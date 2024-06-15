#pragma once
#include "RenderResources.h"

#include <Base/Types.h>
#include <Base/Memory/StackAllocator.h>

#include <robinhood/robinhood.h>

namespace Renderer
{
	class Renderer;
}

namespace Novus
{
    class Window;
}

struct GLFWwindow;
struct GLFWimage;
struct GLFWcursor;
class InputManager;
class TerrainRenderer;
class TerrainLoader;
class TerrainManipulator;
class ModelRenderer;
class ModelLoader;
class MapLoader;
class MaterialRenderer;
class SkyboxRenderer;
class EditorRenderer;
class DebugRenderer;
class JoltDebugRenderer;
class CanvasRenderer;
class LiquidLoader;
class LiquidRenderer;
class UIRenderer;
class PixelQuery;
class EffectRenderer;
class ShadowRenderer;

class GameRenderer
{
public:
	GameRenderer(InputManager* inputManager);
	~GameRenderer();

	bool UpdateWindow(f32 deltaTime);
	void UpdateRenderers(f32 deltaTime);
	f32 Render();

	void ReloadShaders(bool forceRecompileAll);

	bool AddCursor(u32 nameHash, const std::string& path);
	bool SetCursor(u32 nameHash, u32 imguiMouseCursor = 0);

	Renderer::Renderer* GetRenderer() { return _renderer; }

	TerrainRenderer* GetTerrainRenderer() { return _terrainRenderer; }
	ModelRenderer* GetModelRenderer() { return _modelRenderer; }
	LiquidRenderer* GetLiquidRenderer() { return _liquidRenderer; }
	DebugRenderer* GetDebugRenderer() { return _debugRenderer; }
	JoltDebugRenderer* GetJoltDebugRenderer() { return _joltDebugRenderer; }

	TerrainLoader* GetTerrainLoader() { return _terrainLoader; }
	ModelLoader* GetModelLoader() { return _modelLoader; }
	LiquidLoader* GetLiquidLoader() { return _liquidLoader; }
	MapLoader* GetMapLoader() { return _mapLoader; }

	TerrainManipulator* GetTerrainManipulator() { return _terrainManipulator; }

	RenderResources& GetRenderResources() { return _resources; }
	PixelQuery* GetPixelQuery() { return _pixelQuery; }

	Novus::Window* GetWindow() { return _window; }
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
    Novus::Window* _window = nullptr;
	PixelQuery* _pixelQuery = nullptr;

	Memory::StackAllocator* _frameAllocator[2];

	u8 _frameIndex = 0;
	vec2 _lastWindowSize = vec2(1, 1);
	RenderResources _resources;

	// Sub Renderers
	TerrainRenderer* _terrainRenderer = nullptr;
	TerrainLoader* _terrainLoader = nullptr;
	TerrainManipulator* _terrainManipulator = nullptr;

	ModelRenderer* _modelRenderer = nullptr;
	ModelLoader* _modelLoader = nullptr;

	LiquidRenderer* _liquidRenderer = nullptr;
	LiquidLoader* _liquidLoader = nullptr;

	MapLoader* _mapLoader = nullptr;

	MaterialRenderer* _materialRenderer = nullptr;
	SkyboxRenderer* _skyboxRenderer = nullptr;
	DebugRenderer* _debugRenderer = nullptr;
	JoltDebugRenderer* _joltDebugRenderer = nullptr;
	EditorRenderer* _editorRenderer = nullptr;
	CanvasRenderer* _canvasRenderer = nullptr;
	UIRenderer* _uiRenderer = nullptr;
	EffectRenderer* _effectRenderer = nullptr;
	ShadowRenderer* _shadowRenderer = nullptr;

	robin_hood::unordered_map<u32, Cursor> _nameHashToCursor;
};
