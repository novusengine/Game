#pragma once
#include "RenderResources.h"

#include <Base/Types.h>
#include <Base/Memory/StackAllocator.h>

#include <FileFormat/Novus/ShaderPack/ShaderPack.h>

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
struct ImGuiStyle;
class InputManager;
class TerrainRenderer;
class TerrainLoader;
class TerrainManipulator;
class TextureRenderer;
class ModelRenderer;
class ModelLoader;
class MapLoader;
class MaterialRenderer;
class SkyboxRenderer;
class EditorRenderer;
class DebugRenderer;
class LightRenderer;
class JoltDebugRenderer;
class CanvasRenderer;
class LiquidLoader;
class LiquidRenderer;
class UIRenderer;
class PixelQuery;
class EffectRenderer;
class ShadowRenderer;

struct ImGuiTheme
{
public:
    std::string name;
    ImGuiStyle* style = nullptr;
};

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

    CanvasRenderer* GetCanvasRenderer() { return _canvasRenderer; }
    DebugRenderer* GetDebugRenderer() { return _debugRenderer; }
    JoltDebugRenderer* GetJoltDebugRenderer() { return _joltDebugRenderer; }
    LiquidRenderer* GetLiquidRenderer() { return _liquidRenderer; }
    MaterialRenderer* GetMaterialRenderer() { return _materialRenderer; }
    ModelRenderer* GetModelRenderer() { return _modelRenderer; }
    TerrainRenderer* GetTerrainRenderer() { return _terrainRenderer; }
    TextureRenderer* GetTextureRenderer() { return _textureRenderer; }
    SkyboxRenderer* GetSkyboxRenderer() { return _skyboxRenderer; }

    LiquidLoader* GetLiquidLoader() { return _liquidLoader; }
    MapLoader* GetMapLoader() { return _mapLoader; }
    ModelLoader* GetModelLoader() { return _modelLoader; }
    TerrainLoader* GetTerrainLoader() { return _terrainLoader; }

    TerrainManipulator* GetTerrainManipulator() { return _terrainManipulator; }

    RenderResources& GetRenderResources() { return _resources; }
    PixelQuery* GetPixelQuery() { return _pixelQuery; }

    const Renderer::ShaderEntry* GetShaderEntry(u32 shaderNameHash, const std::string& debugName);
    Renderer::GraphicsPipelineID GetBlitPipeline(u32 shaderNameHash);
    Renderer::GraphicsPipelineID GetOverlayPipeline(u32 shaderNameHash);

    const std::vector<ImGuiTheme>& GetImguiThemes() { return _imguiThemes; }
    bool IsCurrentTheme(u32 themeNameHash) { return _currentThemeHash == themeNameHash; }
    bool SetImguiTheme(u32 themeNameHash);

    Novus::Window* GetWindow() { return _window; }
    const std::string& GetGPUName();

private:
    void CreatePermanentResources();
    void InitDescriptorSets();

    void CreateRenderTargets();
    void LoadShaderPacks();
    void LoadShaderPack(std::shared_ptr<Bytebuffer> buffer, FileFormat::ShaderPack& shaderPack);
    void CreateBlitPipelines();

    void CreateImguiThemes();
    void InitImgui();

private:
    struct Cursor
    {
    public:
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

    Renderer::ComputePipelineID _allDescriptorSetComputePipeline;
    Renderer::GraphicsPipelineID _allDescriptorSetGraphicsPipeline;

    // Sub Renderers
    TerrainRenderer* _terrainRenderer = nullptr;
    TerrainLoader* _terrainLoader = nullptr;
    TerrainManipulator* _terrainManipulator = nullptr;

    TextureRenderer* _textureRenderer = nullptr;

    ModelRenderer* _modelRenderer = nullptr;
    ModelLoader* _modelLoader = nullptr;

    LiquidRenderer* _liquidRenderer = nullptr;
    LiquidLoader* _liquidLoader = nullptr;

    MapLoader* _mapLoader = nullptr;

    MaterialRenderer* _materialRenderer = nullptr;
    SkyboxRenderer* _skyboxRenderer = nullptr;
    DebugRenderer* _debugRenderer = nullptr;
    LightRenderer* _lightRenderer = nullptr;
    JoltDebugRenderer* _joltDebugRenderer = nullptr;
    EditorRenderer* _editorRenderer = nullptr;
    CanvasRenderer* _canvasRenderer = nullptr;
    UIRenderer* _uiRenderer = nullptr;
    EffectRenderer* _effectRenderer = nullptr;
    ShadowRenderer* _shadowRenderer = nullptr;

    u32 _currentThemeHash = std::numeric_limits<u32>().max();
    std::vector<ImGuiTheme> _imguiThemes;
    robin_hood::unordered_map<u32, u32> _themeNameHashToIndex;

    robin_hood::unordered_map<u32, Cursor> _nameHashToCursor;

    robin_hood::unordered_map<u32, std::shared_ptr<Bytebuffer>> _shaderPackBuffers;
    robin_hood::unordered_map<u32, Renderer::ShaderEntry> _shaderNameHashToShaderEntry;

    robin_hood::unordered_map<u32, Renderer::GraphicsPipelineID> _blitPipelines;
    robin_hood::unordered_map<u32, Renderer::GraphicsPipelineID> _overlayPipelines;
};
