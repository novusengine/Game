#include "GameRenderer.h"
#include "Canvas/CanvasRenderer.h"
#include "UIRenderer.h"
#include "Debug/DebugRenderer.h"
#include "Debug/JoltDebugRenderer.h"
#include "Light/LightRenderer.h"
#include "Terrain/TerrainRenderer.h"
#include "Terrain/TerrainLoader.h"
#include "Terrain/TerrainManipulator.h"
#include "Texture/TextureRenderer.h"
#include "Model/ModelRenderer.h"
#include "Model/ModelLoader.h"
#include "Liquid/LiquidRenderer.h"
#include "Liquid/LiquidLoader.h"
#include "Material/MaterialRenderer.h"
#include "Skybox/SkyboxRenderer.h"
#include "Editor/EditorRenderer.h"
#include "Effect/EffectRenderer.h"
#include "Shadow/ShadowRenderer.h"
#include "PixelQuery.h"
#include "CullUtils.h"

#include "Game-Lib/Editor/EditorHandler.h"
#include "Game-Lib/Editor/Viewport.h"
#include "Game-Lib/Gameplay/MapLoader.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Base/CVarSystem/CVarSystem.h>
#include <Base/Memory/Bytebuffer.h>
#include <Base/Memory/FileReader.h>

#include <FileFormat/Novus/ShaderPack/ShaderPack.h>

#include <Input/InputManager.h>

#include <Renderer/Renderer.h>
#include <Renderer/RenderSettings.h>
#include <Renderer/RenderGraph.h>
#include <Renderer/Window.h>
#include <Renderer/Descriptors/RenderGraphDesc.h>
#include <Renderer/Renderers/Vulkan/RendererVK.h>
#include <Renderer/Renderers/Vulkan/Backend/stb_image.h>

#include <imgui/imgui.h>
#include <imgui/ImGuiNotify.hpp>
#include <imgui/implot.h>
#include <imgui/imnodes.h>
#include <imgui/ruda.h>
#include <imgui/backends/imgui_impl_glfw.h>

#include <gli/gli.hpp>
#include <GLFW/glfw3.h>
#include "RenderUtils.h"

AutoCVar_ShowFlag CVAR_StartWindowMaximized(CVarCategory::Client, "startWindowMaximized", "determines if the window should be maximized on launch", ShowFlag::ENABLED);

enum GlfwClientApi
{
    GlfwClientApi_Unknown,
    GlfwClientApi_OpenGL,
    GlfwClientApi_Vulkan
};
struct ImGui_ImplGlfw_Data
{
    GLFWwindow* Window;
    GlfwClientApi           ClientApi;
    double                  Time;
    GLFWwindow* MouseWindow;
    GLFWcursor* MouseCursors[ImGuiMouseCursor_COUNT];
    ImVec2                  LastValidMousePos;
    GLFWwindow* KeyOwnerWindows[GLFW_KEY_LAST];
    bool                    InstalledCallbacks;
    bool                    WantUpdateMonitors;

    // Chain GLFW callbacks: our callbacks will call the user's previously installed callbacks, if any.
    GLFWwindowfocusfun      PrevUserCallbackWindowFocus;
    GLFWcursorposfun        PrevUserCallbackCursorPos;
    GLFWcursorenterfun      PrevUserCallbackCursorEnter;
    GLFWmousebuttonfun      PrevUserCallbackMousebutton;
    GLFWscrollfun           PrevUserCallbackScroll;
    GLFWkeyfun              PrevUserCallbackKey;
    GLFWcharfun             PrevUserCallbackChar;
    GLFWmonitorfun          PrevUserCallbackMonitor;

    ImGui_ImplGlfw_Data() { memset((void*)this, 0, sizeof(*this)); }
};

void KeyCallback(GLFWwindow* window, i32 key, i32 scancode, i32 action, i32 modifiers)
{
    ServiceLocator::GetInputManager()->KeyboardInputHandler(key, scancode, action, modifiers);
}

void CharCallback(GLFWwindow* window, u32 unicodeKey)
{
    ServiceLocator::GetInputManager()->CharInputHandler(unicodeKey);
}

void MouseCallback(GLFWwindow* window, i32 button, i32 action, i32 modifiers)
{
    ServiceLocator::GetInputManager()->MouseInputHandler(button, action, modifiers);
}

void CursorPositionCallback(GLFWwindow* window, f64 x, f64 y)
{
    ServiceLocator::GetInputManager()->MousePositionHandler(static_cast<f32>(x), static_cast<f32>(y));
}

void ScrollCallback(GLFWwindow* window, f64 x, f64 y)
{
    ServiceLocator::GetInputManager()->MouseScrollHandler(static_cast<f32>(x), static_cast<f32>(y));
}

void WindowIconifyCallback(GLFWwindow* window, int iconified)
{
    Novus::Window* userWindow = reinterpret_cast<Novus::Window*>(glfwGetWindowUserPointer(window));
    userWindow->SetIsMinimized(iconified == 1);
}

GameRenderer::GameRenderer(InputManager* inputManager)
{
    ServiceLocator::SetGameRenderer(this);

    JPH::RegisterDefaultAllocator();

    _window = new Novus::Window();
    _window->Init(Renderer::Settings::SCREEN_WIDTH, Renderer::Settings::SCREEN_HEIGHT);
    glfwSetWindowAspectRatio(_window->GetWindow(), 16, 9);

    if (CVAR_StartWindowMaximized.Get() == ShowFlag::ENABLED)
        glfwMaximizeWindow(_window->GetWindow());

    KeybindGroup* debugKeybindGroup = inputManager->CreateKeybindGroup("Debug", 15);
    debugKeybindGroup->SetActive(true);

    glfwSetKeyCallback(_window->GetWindow(), KeyCallback);
    glfwSetCharCallback(_window->GetWindow(), CharCallback);
    glfwSetMouseButtonCallback(_window->GetWindow(), MouseCallback);
    glfwSetCursorPosCallback(_window->GetWindow(), CursorPositionCallback);
    glfwSetScrollCallback(_window->GetWindow(), ScrollCallback);
    glfwSetWindowIconifyCallback(_window->GetWindow(), WindowIconifyCallback);

    _renderer = new Renderer::RendererVK(_window);
    _renderer->SetGetShaderEntryCallback(std::bind_front(&GameRenderer::GetShaderEntry, this));
    _renderer->SetGetBlitPipelineCallback(std::bind_front(&GameRenderer::GetBlitPipeline, this));

    std::string shaderSourcePath = SHADER_SOURCE_DIR;
    _renderer->SetShaderSourceDirectory(shaderSourcePath);

    _renderer->InitWindow(_window);
    InitImgui();
    _renderer->InitDebug();

    CreatePermanentResources();

    RenderUtils::Init(_renderer, this);
    DepthPyramidUtils::Init(_renderer, this);

    _debugRenderer = new DebugRenderer(_renderer, this);
    _joltDebugRenderer = new JoltDebugRenderer(_renderer, this, _debugRenderer);

    _modelRenderer = new ModelRenderer(_renderer, this, _debugRenderer);
    _lightRenderer = new LightRenderer(_renderer, this, _debugRenderer, _modelRenderer);
    _modelLoader = new ModelLoader(_modelRenderer, _lightRenderer);
    _modelLoader->Init();

    _liquidRenderer = new LiquidRenderer(_renderer, this, _debugRenderer);
    _liquidLoader = new LiquidLoader(_liquidRenderer);

    _terrainRenderer = new TerrainRenderer(_renderer, this, _debugRenderer);
    _terrainLoader = new TerrainLoader(_terrainRenderer, _modelLoader, _liquidLoader);
    _terrainManipulator = new TerrainManipulator(*_terrainRenderer, *_debugRenderer);
    _textureRenderer = new TextureRenderer(_renderer, this, _debugRenderer);

    _modelLoader->SetTerrainLoader(_terrainLoader);

    _mapLoader = new MapLoader(_terrainLoader, _modelLoader, _liquidLoader);
    
    _materialRenderer = new MaterialRenderer(_renderer, this, _terrainRenderer, _modelRenderer, _lightRenderer);
    _skyboxRenderer = new SkyboxRenderer(_renderer, this, _debugRenderer);
    _editorRenderer = new EditorRenderer(_renderer, this, _debugRenderer);
    _canvasRenderer = new CanvasRenderer(_renderer, this, _debugRenderer);
    _uiRenderer = new UIRenderer(_renderer);
    _effectRenderer = new EffectRenderer(_renderer, this);
    _shadowRenderer = new ShadowRenderer(_renderer, this, _debugRenderer, _terrainRenderer, _modelRenderer, _resources);
    _pixelQuery = new PixelQuery(_renderer, this);

    _nameHashToCursor.reserve(128);
}

GameRenderer::~GameRenderer()
{
    delete _renderer;
}

bool GameRenderer::UpdateWindow(f32 deltaTime)
{
    ZoneScoped;
    return _window->Update(deltaTime);
}

void GameRenderer::UpdateRenderers(f32 deltaTime)
{
    ZoneScoped;

    // Reset the memory in the frameAllocator
    _frameAllocator[_frameIndex]->Reset();

    _textureRenderer->Update(deltaTime);
    _skyboxRenderer->Update(deltaTime);
    _mapLoader->Update(deltaTime);
    _terrainLoader->Update(deltaTime);
    _terrainRenderer->Update(deltaTime);
    _terrainManipulator->Update(deltaTime);
    _modelLoader->Update(deltaTime);
    _modelRenderer->Update(deltaTime);
    _liquidLoader->Update(deltaTime);
    _liquidRenderer->Update(deltaTime);
    _materialRenderer->Update(deltaTime);
    _joltDebugRenderer->Update(deltaTime);
    _debugRenderer->Update(deltaTime);
    _lightRenderer->Update(deltaTime);
    _pixelQuery->Update(deltaTime);
    _editorRenderer->Update(deltaTime);
    _canvasRenderer->Update(deltaTime);
    _uiRenderer->Update(deltaTime);
    _effectRenderer->Update(deltaTime);
    _shadowRenderer->Update(deltaTime, _resources);
}

f32 GameRenderer::Render()
{
    // If the window is minimized we want to pause rendering
    if (_window->IsMinimized())
    {
        ImGui::End();
        ImGui::Render();
        
        return 0.0f;
    }

    Editor::EditorHandler* editorHandler = ServiceLocator::GetEditorHandler();
    bool isEditorMode = editorHandler->GetViewport()->IsEditorMode();

    vec2 windowSize = _renderer->GetWindowSize();

    if (!isEditorMode)
    {
        if (windowSize.x != _lastWindowSize.x || windowSize.y != _lastWindowSize.y)
        {
            _renderer->SetRenderSize(windowSize);
            _lastWindowSize = windowSize;
        }
    }
    else
    {
        vec2 viewportSize = editorHandler->GetViewport()->GetViewportSize();
        viewportSize.x = glm::clamp(viewportSize.x, 1.0f, windowSize.x);
        viewportSize.y = glm::clamp(viewportSize.y, 1.0f, windowSize.y);

        if (viewportSize.x != _lastWindowSize.x || viewportSize.y != _lastWindowSize.y)
        {
            _renderer->SetRenderSize(viewportSize);
            _lastWindowSize = viewportSize;
        }
    }

    if (_resources.cameras.SyncToGPU(_renderer))
    {
        _resources.globalDescriptorSet.Bind("_cameras", _resources.cameras.GetBuffer());
    }

    // Create rendergraph
    Renderer::RenderGraphDesc renderGraphDesc;
    renderGraphDesc.allocator = _frameAllocator[_frameIndex]; // We need to give our rendergraph an allocator to use
    Renderer::RenderGraph& renderGraph = _renderer->CreateRenderGraph(renderGraphDesc);

    f32 timeWaited = _renderer->FlipFrame(_frameIndex);

    editorHandler->DrawImGui();
    editorHandler->EndEditor();
    editorHandler->EndImGui();
    ImGui::Render();

    _renderer->ResetTimeQueries(_frameIndex);

    // StartFrame Pass
    {
        struct StartFramePassData
        {
            Renderer::ImageMutableResource sceneColor;

            Renderer::DescriptorSetResource debugSet;
            Renderer::DescriptorSetResource globalSet;
            Renderer::DescriptorSetResource lightSet;
            Renderer::DescriptorSetResource modelSet;
            Renderer::DescriptorSetResource terrainSet;
        };

        renderGraph.AddPass<StartFramePassData>("StartFramePass",
            [this](StartFramePassData& data, Renderer::RenderGraphBuilder& builder) // Setup
            {
                data.sceneColor = builder.Write(_resources.sceneColor, Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::CLEAR);

                builder.Write(_resources.visibilityBuffer, Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::CLEAR);
                builder.Write(_resources.skyboxColor, Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::CLEAR);
                builder.Write(_resources.finalColor, Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::CLEAR);
                builder.Write(_resources.transparency, Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::CLEAR);
                builder.Write(_resources.transparencyWeights, Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::CLEAR);
                builder.Write(_resources.depth, Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::CLEAR);
                builder.Write(_resources.depthColorCopy, Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::CLEAR);
                builder.Write(_resources.skyboxDepth, Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::CLEAR);
                builder.Write(_resources.packedNormals, Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::CLEAR);
                builder.Write(_resources.ssaoTarget, Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::CLEAR);

                data.debugSet = builder.Use(_resources.debugDescriptorSet);
                data.globalSet = builder.Use(_resources.globalDescriptorSet);
                data.lightSet = builder.Use(_resources.lightDescriptorSet);
                data.modelSet = builder.Use(_resources.modelDescriptorSet);
                data.terrainSet = builder.Use(_resources.terrainDescriptorSet);

                return true; // Return true from setup to enable this pass, return false to disable it
            },
            [this](StartFramePassData& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList) // Execute
            {
                GPU_SCOPED_PROFILER_ZONE(commandList, StartFramePass);
                commandList.MarkFrameStart(_frameIndex);

                // Set viewport
                vec2 renderSize = graphResources.GetImageDimensions(data.sceneColor);

                commandList.SetViewport(0, 0, renderSize.x, renderSize.y, 0.0f, 1.0f);
                commandList.SetScissorRect(0, static_cast<u32>(renderSize.x), 0, static_cast<u32>(renderSize.y));
            });
    }
    _debugRenderer->AddStartFramePass(&renderGraph, _resources, _frameIndex);

    _textureRenderer->AddTexturePass(&renderGraph, _resources, _frameIndex);

    _skyboxRenderer->AddSkyboxPass(&renderGraph, _resources, _frameIndex);
    _modelRenderer->AddSkyboxPass(&renderGraph, _resources, _frameIndex);

    _shadowRenderer->AddShadowPass(&renderGraph, _resources, _frameIndex);

    // Occluder passes
    _terrainRenderer->AddOccluderPass(&renderGraph, _resources, _frameIndex);
    _modelRenderer->AddOccluderPass(&renderGraph, _resources, _frameIndex);
    _joltDebugRenderer->AddOccluderPass(&renderGraph, _resources, _frameIndex);

    // Depth Pyramid Pass
    struct PyramidPassData
    {
        Renderer::DepthImageResource depth;
        Renderer::ImageMutableResource depthPyramid;

        Renderer::DescriptorSetResource copyDescriptorSet;
        Renderer::DescriptorSetResource pyramidDescriptorSet;
    };

    renderGraph.AddPass<PyramidPassData>("PyramidPass",
        [this](PyramidPassData& data, Renderer::RenderGraphBuilder& builder) // Setup
        {
            using BufferUsage = Renderer::BufferPassUsage;

            data.depth = builder.Read(_resources.depth, Renderer::PipelineType::GRAPHICS);
            data.depthPyramid = builder.Write(_resources.depthPyramid, Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::LOAD);

            builder.Write(DepthPyramidUtils::_atomicBuffer, BufferUsage::COMPUTE);

            data.copyDescriptorSet = builder.Use(DepthPyramidUtils::_copyDescriptorSet);
            data.pyramidDescriptorSet = builder.Use(DepthPyramidUtils::_pyramidDescriptorSet);

            return true; // Return true from setup to enable this pass, return false to disable it
        },
        [this](PyramidPassData& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList) // Execute
        {
            GPU_SCOPED_PROFILER_ZONE(commandList, BuildPyramid);

            DepthPyramidUtils::BuildPyramidParams params;
            params.graphResources = &graphResources;
            params.commandList = &commandList;
            params.resources = &_resources;
            params.frameIndex = _frameIndex;

            params.pyramidSize = graphResources.GetImageDimensions(data.depthPyramid, 0);
            params.depth = data.depth;
            params.depthPyramid = data.depthPyramid;

            params.copyDescriptorSet = data.copyDescriptorSet;
            params.pyramidDescriptorSet = data.pyramidDescriptorSet;

            DepthPyramidUtils::BuildPyramid(params);
        });

    _terrainRenderer->AddCullingPass(&renderGraph, _resources, _frameIndex);
    _terrainRenderer->AddGeometryPass(&renderGraph, _resources, _frameIndex);
    
    _modelRenderer->AddCullingPass(&renderGraph, _resources, _frameIndex);
    _modelRenderer->AddGeometryPass(&renderGraph, _resources, _frameIndex);

    _joltDebugRenderer->AddCullingPass(&renderGraph, _resources, _frameIndex);
    _joltDebugRenderer->AddGeometryPass(&renderGraph, _resources, _frameIndex);

    _modelRenderer->AddTransparencyCullingPass(&renderGraph, _resources, _frameIndex);
    _modelRenderer->AddTransparencyGeometryPass(&renderGraph, _resources, _frameIndex);

    _liquidRenderer->AddCopyDepthPass(&renderGraph, _resources, _frameIndex);
    _liquidRenderer->AddCullingPass(&renderGraph, _resources, _frameIndex);
    _liquidRenderer->AddGeometryPass(&renderGraph, _resources, _frameIndex);

    _lightRenderer->AddClassificationPass(&renderGraph, _resources, _frameIndex);

    _materialRenderer->AddPreEffectsPass(&renderGraph, _resources, _frameIndex);
    _effectRenderer->AddSSAOPass(&renderGraph, _resources, _frameIndex);

    _materialRenderer->AddMaterialPass(&renderGraph, _resources, _frameIndex);

    _pixelQuery->AddPixelQueryPass(&renderGraph, _resources, _frameIndex);

    _editorRenderer->AddWorldGridPass(&renderGraph, _resources, _frameIndex);
    _debugRenderer->Add3DPass(&renderGraph, _resources, _frameIndex);

    _canvasRenderer->AddCanvasPass(&renderGraph, _resources, _frameIndex);
    _debugRenderer->Add2DPass(&renderGraph, _resources, _frameIndex);

    _lightRenderer->AddDebugPass(&renderGraph, _resources, _frameIndex);

    Renderer::ImageID finalTarget = isEditorMode ? _resources.finalColor : _resources.sceneColor;
    _uiRenderer->AddImguiPass(&renderGraph, _resources, _frameIndex, finalTarget);

    renderGraph.AddSignalSemaphore(_resources.sceneRenderedSemaphore); // Signal that we are ready to present
    renderGraph.AddSignalSemaphore(_resources.frameSyncSemaphores.Get(_frameIndex)); // Signal that this frame has finished, for next frames sake

    static bool firstFrame = true;
    if (firstFrame)
    {
        firstFrame = false;
    }
    else
    {
        renderGraph.AddWaitSemaphore(_resources.frameSyncSemaphores.Get(!_frameIndex)); // Wait for previous frame to finish
    }

    if (_renderer->ShouldWaitForUpload())
    {
        renderGraph.AddWaitSemaphore(_renderer->GetUploadFinishedSemaphore());
        _renderer->SetHasWaitedForUpload();
    }

    renderGraph.Setup();
    renderGraph.Execute();

    _renderer->Present(_window, finalTarget, _resources.sceneRenderedSemaphore);

    // Flip the frameIndex between 0 and 1
    _frameIndex = !_frameIndex;
    return timeWaited;
}

void GameRenderer::ReloadShaders(bool forceRecompileAll)
{
    _renderer->ReloadShaders(forceRecompileAll);
}

bool GameRenderer::AddCursor(u32 nameHash, const std::string& path)
{
    if (_nameHashToCursor.contains(nameHash))
        return false;

    gli::texture texture = gli::load(path);

    if (texture.empty())
        return false;

    Cursor cursor;
    cursor.image = new GLFWimage();
    cursor.image->width = texture.extent().x;
    cursor.image->height = texture.extent().y;
    cursor.image->pixels = (unsigned char*)texture.data();
    cursor.cursor = glfwCreateCursor(cursor.image, 0, 0);

    _nameHashToCursor[nameHash] = cursor;

    return true;
}

bool GameRenderer::SetCursor(u32 nameHash, u32 imguiMouseCursor /*= 0*/)
{
    if (!_nameHashToCursor.contains(nameHash))
        return false;

    const Cursor& cursor = _nameHashToCursor[nameHash];
    if (cursor.cursor == nullptr)
        return false;

    ImGuiIO& io = ImGui::GetIO();
    ImGui_ImplGlfw_Data* glfwImguiData = reinterpret_cast<ImGui_ImplGlfw_Data*>(io.BackendPlatformUserData);
    glfwImguiData->MouseCursors[imguiMouseCursor] = cursor.cursor;
    return true;
}

const Renderer::ShaderEntry* GameRenderer::GetShaderEntry(u32 shaderNameHash, const std::string& debugName)
{
    if (_shaderNameHashToShaderEntry.contains(shaderNameHash) == false)
    {
        NC_LOG_CRITICAL("GameRenderer::GetShaderEntry Tried to get ShaderEntry for unknown shader name hash: {}", shaderNameHash);
    }

    Renderer::ShaderEntry& entry = _shaderNameHashToShaderEntry[shaderNameHash];
    if (entry.debugName.empty())
    {
        entry.debugName = debugName;
    }

    return &entry;
}

Renderer::GraphicsPipelineID GameRenderer::GetBlitPipeline(u32 shaderNameHash)
{
    if (_blitPipelines.contains(shaderNameHash) == false)
    {
        NC_LOG_CRITICAL("GameRenderer::GetBlitPipeline Tried to get Blit Pipeline for unknown shader name hash: {}", shaderNameHash);
    }

    return _blitPipelines[shaderNameHash];
}

Renderer::GraphicsPipelineID GameRenderer::GetOverlayPipeline(u32 shaderNameHash)
{
    if (_overlayPipelines.contains(shaderNameHash) == false)
    {
        NC_LOG_CRITICAL("GameRenderer::GetOverlayPipeline Tried to get Overlay Pipeline for unknown shader name hash: {}", shaderNameHash);
    }

    return _overlayPipelines[shaderNameHash];
}

bool GameRenderer::SetImguiTheme(u32 themeNameHash)
{
    if (!_themeNameHashToIndex.contains(themeNameHash))
        return false;

    if (IsCurrentTheme(themeNameHash))
        return true;

    u32 themeIndex = _themeNameHashToIndex[themeNameHash];
    const auto& theme = _imguiThemes[themeIndex];
    ImGui::GetStyle() = *theme.style;

    _currentThemeHash = themeNameHash;
    return true;
}

const std::string& GameRenderer::GetGPUName()
{
    return _renderer->GetGPUName();
}

void GameRenderer::CreatePermanentResources()
{
    CreateRenderTargets();
    LoadShaderPacks();
    InitDescriptorSets();
    CreateBlitPipelines();

    // Frame allocator, this is a fast allocator for data that is only needed this frame
    {
        const size_t FRAME_ALLOCATOR_SIZE = 16 * 1024 * 1024; // 16 MB

        for (u32 i = 0; i < 2; i++)
        {
            _frameAllocator[i] = new Memory::StackAllocator();
            _frameAllocator[i]->Init(FRAME_ALLOCATOR_SIZE);
        }
    }

    _resources.sceneRenderedSemaphore = _renderer->CreateNSemaphore();
    for (u32 i = 0; i < _resources.frameSyncSemaphores.Num; i++)
    {
        _resources.frameSyncSemaphores.Get(i) = _renderer->CreateNSemaphore();
    }

    _resources.cameras.SetDebugName("Cameras");
    _resources.cameras.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);
    _resources.cameras.Add(Camera());
}

void GameRenderer::CreateRenderTargets()
{
    // Visibility Buffer rendertarget
    Renderer::ImageDesc visibilityBufferDesc;
    visibilityBufferDesc.debugName = "VisibilityBuffer";
    visibilityBufferDesc.dimensions = vec2(1.0f, 1.0f);
    visibilityBufferDesc.dimensionType = Renderer::ImageDimensionType::DIMENSION_SCALE_RENDERSIZE;
    visibilityBufferDesc.format = Renderer::ImageFormat::R32G32_UINT;
    visibilityBufferDesc.sampleCount = Renderer::SampleCount::SAMPLE_COUNT_1;
    visibilityBufferDesc.clearUInts = uvec4(0, 0, 0, 0);

    _resources.visibilityBuffer = _renderer->CreateImage(visibilityBufferDesc);

    // Normals rendertarget
    Renderer::ImageDesc packedNormalsDesc;
    packedNormalsDesc.debugName = "PackedNormals";
    packedNormalsDesc.dimensions = vec2(1.0f, 1.0f);
    packedNormalsDesc.dimensionType = Renderer::ImageDimensionType::DIMENSION_SCALE_RENDERSIZE;
    packedNormalsDesc.format = Renderer::ImageFormat::R11G11B10_UFLOAT;
    packedNormalsDesc.sampleCount = Renderer::SampleCount::SAMPLE_COUNT_1;
    packedNormalsDesc.clearColor = Color(0.0f, 0.0f, 0.0f, 0.0f);

    _resources.packedNormals = _renderer->CreateImage(packedNormalsDesc);

    // Scene color rendertarget
    Renderer::ImageDesc sceneColorDesc;
    sceneColorDesc.debugName = "SceneColor";
    sceneColorDesc.dimensions = vec2(1.0f, 1.0f);
    sceneColorDesc.dimensionType = Renderer::ImageDimensionType::DIMENSION_SCALE_RENDERSIZE;
    sceneColorDesc.format = _renderer->GetSwapChainImageFormat();
    sceneColorDesc.sampleCount = Renderer::SampleCount::SAMPLE_COUNT_1;
    sceneColorDesc.clearColor = Color(0.52f, 0.80f, 0.92f, 1.0f); // Sky blue

    _resources.sceneColor = _renderer->CreateImage(sceneColorDesc);
    _resources.skyboxColor = _renderer->CreateImage(sceneColorDesc);

    sceneColorDesc.debugName = "FinalColor";
    sceneColorDesc.dimensionType = Renderer::ImageDimensionType::DIMENSION_SCALE_WINDOW;
    sceneColorDesc.clearColor = Color(0.43f, 0.50f, 0.56f, 1.0f); // Slate gray
    _resources.finalColor = _renderer->CreateImage(sceneColorDesc);

    // Debug rendertarget
    Renderer::ImageDesc debugColorDesc;
    debugColorDesc.debugName = "DebugColor";
    debugColorDesc.dimensions = vec2(1.0f, 1.0f);
    debugColorDesc.dimensionType = Renderer::ImageDimensionType::DIMENSION_SCALE_RENDERSIZE;
    debugColorDesc.format = _renderer->GetSwapChainImageFormat();
    debugColorDesc.sampleCount = Renderer::SampleCount::SAMPLE_COUNT_1;
    debugColorDesc.clearColor = Color::Clear;

    _resources.debugColor = _renderer->CreateImage(debugColorDesc);

    // SSAO
    Renderer::ImageDesc ssaoTargetDesc;
    ssaoTargetDesc.debugName = "SSAOTarget";
    ssaoTargetDesc.dimensions = vec2(1.0f, 1.0f);
    ssaoTargetDesc.dimensionType = Renderer::ImageDimensionType::DIMENSION_SCALE_RENDERSIZE;
    ssaoTargetDesc.format = Renderer::ImageFormat::R8_UNORM;
    ssaoTargetDesc.sampleCount = Renderer::SampleCount::SAMPLE_COUNT_1;
    ssaoTargetDesc.clearColor = Color(1.0f, 1.0f, 1.0f, 1.0f);

    _resources.ssaoTarget = _renderer->CreateImage(ssaoTargetDesc);

    // Transparency rendertarget
    Renderer::ImageDesc transparencyDesc;
    transparencyDesc.debugName = "Transparency";
    transparencyDesc.dimensions = vec2(1.0f, 1.0f);
    transparencyDesc.dimensionType = Renderer::ImageDimensionType::DIMENSION_SCALE_RENDERSIZE;
    transparencyDesc.format = Renderer::ImageFormat::R16G16B16A16_FLOAT;
    transparencyDesc.sampleCount = Renderer::SampleCount::SAMPLE_COUNT_1;
    transparencyDesc.clearColor = Color::Clear;

    _resources.transparency = _renderer->CreateImage(transparencyDesc);

    // Transparency weights rendertarget
    Renderer::ImageDesc transparencyWeightsDesc;
    transparencyWeightsDesc.debugName = "TransparencyWeights";
    transparencyWeightsDesc.dimensions = vec2(1.0f, 1.0f);
    transparencyWeightsDesc.dimensionType = Renderer::ImageDimensionType::DIMENSION_SCALE_RENDERSIZE;
    transparencyWeightsDesc.format = Renderer::ImageFormat::R16_FLOAT;
    transparencyWeightsDesc.sampleCount = Renderer::SampleCount::SAMPLE_COUNT_1;
    transparencyWeightsDesc.clearColor = Color::Red;

    _resources.transparencyWeights = _renderer->CreateImage(transparencyWeightsDesc);

    // depth pyramid ID rendertarget
    Renderer::ImageDesc pyramidDesc;
    pyramidDesc.debugName = "DepthPyramid";
    pyramidDesc.dimensions = vec2(1.0f, 1.0f);
    pyramidDesc.dimensionType = Renderer::ImageDimensionType::DIMENSION_PYRAMID_RENDERSIZE;
    pyramidDesc.format = Renderer::ImageFormat::R32_FLOAT;
    pyramidDesc.sampleCount = Renderer::SampleCount::SAMPLE_COUNT_1;
    _resources.depthPyramid = _renderer->CreateImage(pyramidDesc);

    // Main depth rendertarget
    Renderer::DepthImageDesc mainDepthDesc;
    mainDepthDesc.debugName = "MainDepth";
    mainDepthDesc.dimensions = vec2(1.0f, 1.0f);
    mainDepthDesc.dimensionType = Renderer::ImageDimensionType::DIMENSION_SCALE_RENDERSIZE;
    mainDepthDesc.format = Renderer::DepthImageFormat::D32_FLOAT;
    mainDepthDesc.sampleCount = Renderer::SampleCount::SAMPLE_COUNT_1;
    mainDepthDesc.depthClearValue = 0.0f;

    _resources.depth = _renderer->CreateDepthImage(mainDepthDesc);
    _resources.skyboxDepth = _renderer->CreateDepthImage(mainDepthDesc);
    _resources.debugRendererDepth = _renderer->CreateDepthImage(mainDepthDesc);

    // Copy of the depth, as a color rendertarget
    Renderer::ImageDesc depthColorCopyDesc;
    depthColorCopyDesc.debugName = "DepthColorCopy";
    depthColorCopyDesc.dimensions = vec2(1.0f, 1.0f);
    depthColorCopyDesc.dimensionType = Renderer::ImageDimensionType::DIMENSION_SCALE_RENDERSIZE;
    depthColorCopyDesc.format = Renderer::ImageFormat::R32_FLOAT;
    depthColorCopyDesc.sampleCount = Renderer::SampleCount::SAMPLE_COUNT_1;
    depthColorCopyDesc.clearColor = Color::Clear;

    _resources.depthColorCopy = _renderer->CreateImage(depthColorCopyDesc);
}

void GameRenderer::InitDescriptorSets()
{
    // Load dummy shader containing includes of all descriptor sets
    Renderer::ComputeShaderDesc shaderDesc;
    shaderDesc.shaderEntry = GetShaderEntry("DescriptorSet/_IncludeAll.cs"_h, "DescriptorSet/_IncludeAll.cs");
    Renderer::ComputeShaderID computeShader = _renderer->LoadShader(shaderDesc);

    // Create a dummy pipeline from it
    Renderer::ComputePipelineDesc pipelineDesc;
    pipelineDesc.computeShader = computeShader;
    _allDescriptorSetComputePipeline = _renderer->CreatePipeline(pipelineDesc);

   Renderer::DescriptorSet* descriptorSets[] =
   {
       &_resources.debugDescriptorSet,
       &_resources.globalDescriptorSet,
       &_resources.lightDescriptorSet,
       &_resources.terrainDescriptorSet,
       &_resources.modelDescriptorSet
   };

   for(Renderer::DescriptorSet* descriptorSet : descriptorSets)
   {
       descriptorSet->RegisterPipeline(_renderer, _allDescriptorSetComputePipeline);
       descriptorSet->Init(_renderer);
   }

   // Create a dummy graphics pipeline
   Renderer::VertexShaderDesc vertexShaderDesc;
   vertexShaderDesc.shaderEntry = GetShaderEntry("DescriptorSet/_IncludeAll.vs"_h, "DescriptorSet/_IncludeAll.vs");
   Renderer::VertexShaderID vertexShader = _renderer->LoadShader(vertexShaderDesc);

   Renderer::GraphicsPipelineDesc graphicsPipelineDesc;
   graphicsPipelineDesc.states.vertexShader = vertexShader;

   _allDescriptorSetGraphicsPipeline = _renderer->CreatePipeline(graphicsPipelineDesc);
}

void GameRenderer::LoadShaderPacks()
{
    // Load all shader packs into memory
    std::filesystem::path binPath = std::filesystem::path("Data/Shaders/");

    // Load all .shaderpack files
    for(const auto& entry : std::filesystem::recursive_directory_iterator(binPath))
    {
        if (entry.path().extension() != ".shaderpack")
            continue;

        FileReader fileReader(entry.path().string());
        if (!fileReader.Open())
        {
            NC_LOG_ERROR("Failed to open shader pack: {}", entry.path().string());
            continue;
        }

        u64 bufferSize = fileReader.Length();
        std::shared_ptr<Bytebuffer> buffer = Bytebuffer::BorrowRuntime(bufferSize);
        fileReader.Read(buffer.get(), bufferSize);

        FileFormat::ShaderPack shaderPack;
        if (!FileFormat::ShaderPack::Read(buffer, shaderPack))
        {
            fileReader.Close();
            NC_LOG_ERROR("Failed to read shader pack: {}", entry.path().string());
            continue;
        }

        LoadShaderPack(buffer, shaderPack);

        std::filesystem::path relativePath = std::filesystem::relative(entry.path(), binPath);
        u32 shaderPackNameHash = StringUtils::fnv1a_32(relativePath.string().c_str(), relativePath.string().length());
        _shaderPackBuffers[shaderPackNameHash] = std::move(buffer);
    }
}

void GameRenderer::LoadShaderPack(std::shared_ptr<Bytebuffer> buffer, FileFormat::ShaderPack& shaderPack)
{
    for (u32 i = 0; i < shaderPack.GetNumShaders(); i++)
    {
        FileFormat::ShaderRef* shaderRef = shaderPack.GetShaderRef(buffer, i);

        Renderer::ShaderEntry shaderEntry =
        {
            .permutationNameHash = shaderRef->permutationNameHash,
            .shaderData = buffer->GetDataPointer() + shaderRef->dataOffset,
            .shaderSize = shaderRef->dataSize,
        };
        shaderPack.GetShaderReflection(buffer, i, shaderEntry.reflection);

        _shaderNameHashToShaderEntry[shaderRef->permutationNameHash] = shaderEntry;
    }
}

void GameRenderer::CreateBlitPipelines()
{
    Renderer::VertexShaderDesc vertexShaderDesc;
    vertexShaderDesc.shaderEntry = GetShaderEntry("Blitting/Blit.vs"_h, "Blitting/Blit.vs");
    Renderer::VertexShaderID vertexShader = _renderer->LoadShader(vertexShaderDesc);

    const char* textureTypes[] =
    {
        "float",
        "int",
        "uint"
    };

    Renderer::GraphicsPipelineDesc pipelineDesc;
    pipelineDesc.states.vertexShader = vertexShader;
    pipelineDesc.states.rasterizerState.cullMode = Renderer::CullMode::BACK;
    pipelineDesc.states.rasterizerState.frontFaceMode = Renderer::FrontFaceState::COUNTERCLOCKWISE;
    pipelineDesc.states.renderTargetFormats[0] = _renderer->GetSwapChainImageFormat();

    // Create blit pipelines for all texture types and component counts
    for (u32 i = 0; i < 3; i++)
    {
        const char* textureType = textureTypes[i];

        for (u32 j = 1; j <= 4; j++)
        {
            std::string componentTypeName = textureType;
            if (j > 1)
            {
                componentTypeName.append(std::to_string(j));
            }

            std::vector<Renderer::PermutationField> permutationFields =
            {
                { "TEX_TYPE", componentTypeName }
            };
            u32 shaderEntryNameHash = Renderer::GetShaderEntryNameHash("Blitting/Blit.ps", permutationFields);

            Renderer::PixelShaderDesc pixelShaderDesc;
            pixelShaderDesc.shaderEntry = GetShaderEntry(shaderEntryNameHash, "Blitting/Blit.ps");
            pipelineDesc.debugName = "Blit_" + componentTypeName;
            
            pipelineDesc.states.pixelShader = _renderer->LoadShader(pixelShaderDesc);

            u32 componentTypeNameHash = StringUtils::fnv1a_32(componentTypeName.c_str(), componentTypeName.length());
            _blitPipelines[componentTypeNameHash] = _renderer->CreatePipeline(pipelineDesc);
        }
    }

    // Set additive blending for overlay pipelines
    pipelineDesc.states.blendState.renderTargets[0].blendEnable = true;
    pipelineDesc.states.blendState.renderTargets[0].blendOp = Renderer::BlendOp::ADD;
    pipelineDesc.states.blendState.renderTargets[0].srcBlend = Renderer::BlendMode::SRC_ALPHA;
    pipelineDesc.states.blendState.renderTargets[0].destBlend = Renderer::BlendMode::ONE;

    // Create overlay pipelines for all texture types and component counts
    for (u32 i = 0; i < 3; i++)
    {
        const char* textureType = textureTypes[i];

        for (u32 j = 1; j <= 4; j++)
        {
            std::string componentTypeName = textureType;
            if (j > 1)
            {
                componentTypeName.append(std::to_string(j));
            }

            std::vector<Renderer::PermutationField> permutationFields =
            {
                { "TEX_TYPE", componentTypeName }
            };
            u32 shaderEntryNameHash = Renderer::GetShaderEntryNameHash("Blitting/Blit.ps", permutationFields);

            Renderer::PixelShaderDesc pixelShaderDesc;
            pixelShaderDesc.shaderEntry = GetShaderEntry(shaderEntryNameHash, "Blitting/Blit.ps");
            pipelineDesc.debugName = "Blit_" + componentTypeName;

            pipelineDesc.states.pixelShader = _renderer->LoadShader(pixelShaderDesc);

            u32 componentTypeNameHash = StringUtils::fnv1a_32(componentTypeName.c_str(), componentTypeName.length());
            _overlayPipelines[componentTypeNameHash] = _renderer->CreatePipeline(pipelineDesc);
        }
    }
}

void GameRenderer::CreateImguiThemes()
{
    for (auto& theme : _imguiThemes)
    {
        if (theme.style == nullptr)
            continue;

        delete theme.style;
    }
    _imguiThemes.clear();
    _themeNameHashToIndex.clear();

    // Blue Teal
    {
        u32 themeIndex = static_cast<u32>(_imguiThemes.size());

        ImGuiTheme& theme = _imguiThemes.emplace_back();
        theme.name = "Blue Teal";
        theme.style = new ImGuiStyle();

        u32 themeNameHash = StringUtils::fnv1a_32(theme.name.c_str(), theme.name.length());
        _themeNameHashToIndex[themeNameHash] = themeIndex;

        theme.style->FrameRounding = 2.0f;
        theme.style->GrabRounding = 2.0f;
        theme.style->WindowMinSize = vec2(100, 100);

        theme.style->Colors[ImGuiCol_Text] = ImVec4(0.95f, 0.96f, 0.98f, 1.00f);
        theme.style->Colors[ImGuiCol_TextDisabled] = ImVec4(0.36f, 0.42f, 0.47f, 1.00f);
        theme.style->Colors[ImGuiCol_WindowBg] = ImVec4(0.11f, 0.15f, 0.17f, 1.00f);
        theme.style->Colors[ImGuiCol_ChildBg] = ImVec4(0.15f, 0.18f, 0.22f, 1.00f);
        theme.style->Colors[ImGuiCol_PopupBg] = ImVec4(0.08f, 0.08f, 0.08f, 0.94f);
        theme.style->Colors[ImGuiCol_Border] = ImVec4(0.08f, 0.10f, 0.12f, 1.00f);
        theme.style->Colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        theme.style->Colors[ImGuiCol_FrameBg] = ImVec4(0.20f, 0.25f, 0.29f, 1.00f);
        theme.style->Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.12f, 0.20f, 0.28f, 1.00f);
        theme.style->Colors[ImGuiCol_FrameBgActive] = ImVec4(0.09f, 0.12f, 0.14f, 1.00f);
        theme.style->Colors[ImGuiCol_TitleBg] = ImVec4(0.09f, 0.12f, 0.14f, 0.65f);
        theme.style->Colors[ImGuiCol_TitleBgActive] = ImVec4(0.08f, 0.10f, 0.12f, 1.00f);
        theme.style->Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.00f, 0.00f, 0.00f, 0.51f);
        theme.style->Colors[ImGuiCol_MenuBarBg] = ImVec4(0.15f, 0.18f, 0.22f, 1.00f);
        theme.style->Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.02f, 0.02f, 0.02f, 0.39f);
        theme.style->Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.20f, 0.25f, 0.29f, 1.00f);
        theme.style->Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.18f, 0.22f, 0.25f, 1.00f);
        theme.style->Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.09f, 0.21f, 0.31f, 1.00f);
        theme.style->Colors[ImGuiCol_CheckMark] = ImVec4(0.28f, 0.56f, 1.00f, 1.00f);
        theme.style->Colors[ImGuiCol_SliderGrab] = ImVec4(0.28f, 0.56f, 1.00f, 1.00f);
        theme.style->Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.37f, 0.61f, 1.00f, 1.00f);
        theme.style->Colors[ImGuiCol_Button] = ImVec4(0.20f, 0.25f, 0.29f, 1.00f);
        theme.style->Colors[ImGuiCol_ButtonHovered] = ImVec4(0.28f, 0.56f, 1.00f, 1.00f);
        theme.style->Colors[ImGuiCol_ButtonActive] = ImVec4(0.06f, 0.53f, 0.98f, 1.00f);
        theme.style->Colors[ImGuiCol_Header] = ImVec4(0.20f, 0.25f, 0.29f, 0.55f);
        theme.style->Colors[ImGuiCol_HeaderHovered] = ImVec4(0.26f, 0.59f, 0.98f, 0.80f);
        theme.style->Colors[ImGuiCol_HeaderActive] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
        theme.style->Colors[ImGuiCol_Separator] = ImVec4(0.20f, 0.25f, 0.29f, 1.00f);
        theme.style->Colors[ImGuiCol_SeparatorHovered] = ImVec4(0.10f, 0.40f, 0.75f, 0.78f);
        theme.style->Colors[ImGuiCol_SeparatorActive] = ImVec4(0.10f, 0.40f, 0.75f, 1.00f);
        theme.style->Colors[ImGuiCol_ResizeGrip] = ImVec4(0.26f, 0.59f, 0.98f, 0.25f);
        theme.style->Colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.26f, 0.59f, 0.98f, 0.67f);
        theme.style->Colors[ImGuiCol_ResizeGripActive] = ImVec4(0.26f, 0.59f, 0.98f, 0.95f);
        theme.style->Colors[ImGuiCol_Tab] = ImVec4(0.11f, 0.15f, 0.17f, 1.00f);
        theme.style->Colors[ImGuiCol_TabHovered] = ImVec4(0.26f, 0.59f, 0.98f, 0.80f);
        theme.style->Colors[ImGuiCol_TabActive] = ImVec4(0.20f, 0.25f, 0.29f, 1.00f);
        theme.style->Colors[ImGuiCol_TabUnfocused] = ImVec4(0.11f, 0.15f, 0.17f, 1.00f);
        theme.style->Colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.11f, 0.15f, 0.17f, 1.00f);
        theme.style->Colors[ImGuiCol_PlotLines] = ImVec4(0.61f, 0.61f, 0.61f, 1.00f);
        theme.style->Colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
        theme.style->Colors[ImGuiCol_PlotHistogram] = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
        theme.style->Colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
        theme.style->Colors[ImGuiCol_TextSelectedBg] = ImVec4(0.26f, 0.59f, 0.98f, 0.35f);
        theme.style->Colors[ImGuiCol_DragDropTarget] = ImVec4(1.00f, 1.00f, 0.00f, 0.90f);
        theme.style->Colors[ImGuiCol_NavHighlight] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
        theme.style->Colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
        theme.style->Colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
        theme.style->Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.35f);
    }

    // Dark Grey
    {
        u32 themeIndex = static_cast<u32>(_imguiThemes.size());

        ImGuiTheme& theme = _imguiThemes.emplace_back();
        theme.name = "Dark Grey";
        theme.style = new ImGuiStyle();

        u32 themeNameHash = StringUtils::fnv1a_32(theme.name.c_str(), theme.name.length());
        _themeNameHashToIndex[themeNameHash] = themeIndex;

        theme.style->Alpha = 1.0f;
        theme.style->DisabledAlpha = 0.6000000238418579f;
        theme.style->WindowPadding = ImVec2(8.0f, 8.0f);
        theme.style->WindowRounding = 4.0f;
        theme.style->WindowBorderSize = 1.0f;
        theme.style->WindowMinSize = ImVec2(32.0f, 32.0f);
        theme.style->WindowTitleAlign = ImVec2(0.0f, 0.5f);
        theme.style->WindowMenuButtonPosition = ImGuiDir_Left;
        theme.style->ChildRounding = 4.0f;
        theme.style->ChildBorderSize = 1.0f;
        theme.style->PopupRounding = 2.0f;
        theme.style->PopupBorderSize = 1.0f;
        theme.style->FramePadding = ImVec2(4.0f, 3.0f);
        theme.style->FrameRounding = 2.0f;
        theme.style->FrameBorderSize = 1.0f;
        theme.style->ItemSpacing = ImVec2(8.0f, 4.0f);
        theme.style->ItemInnerSpacing = ImVec2(4.0f, 4.0f);
        theme.style->CellPadding = ImVec2(4.0f, 2.0f);
        theme.style->IndentSpacing = 21.0f;
        theme.style->ColumnsMinSpacing = 6.0f;
        theme.style->ScrollbarSize = 13.0f;
        theme.style->ScrollbarRounding = 12.0f;
        theme.style->GrabMinSize = 7.0f;
        theme.style->GrabRounding = 0.0f;
        theme.style->TabRounding = 0.0f;
        theme.style->TabBorderSize = 1.0f;
        theme.style->TabCloseButtonMinWidthUnselected = 0.0f;
        theme.style->ColorButtonPosition = ImGuiDir_Right;
        theme.style->ButtonTextAlign = ImVec2(0.5f, 0.5f);
        theme.style->SelectableTextAlign = ImVec2(0.0f, 0.0f);

        theme.style->Colors[ImGuiCol_Text] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
        theme.style->Colors[ImGuiCol_TextDisabled] = ImVec4(0.4980392158031464f, 0.4980392158031464f, 0.4980392158031464f, 1.0f);
        theme.style->Colors[ImGuiCol_WindowBg] = ImVec4(0.1764705926179886f, 0.1764705926179886f, 0.1764705926179886f, 1.0f);
        theme.style->Colors[ImGuiCol_ChildBg] = ImVec4(0.2784313857555389f, 0.2784313857555389f, 0.2784313857555389f, 0.0f);
        theme.style->Colors[ImGuiCol_PopupBg] = ImVec4(0.3098039329051971f, 0.3098039329051971f, 0.3098039329051971f, 1.0f);
        theme.style->Colors[ImGuiCol_Border] = ImVec4(0.3627451121807098f, 0.3627451121807098f, 0.3627451121807098f, 0.6f);
        theme.style->Colors[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
        theme.style->Colors[ImGuiCol_FrameBg] = ImVec4(0.1568627506494522f * 1.5f, 0.1568627506494522f * 1.5f, 0.1568627506494522f * 1.5f, 1.0f);
        theme.style->Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.1568627506494522f * 1.6f, 0.1568627506494522f * 1.6f, 0.1568627506494522f * 1.6f, 1.0f);
        theme.style->Colors[ImGuiCol_FrameBgActive] = ImVec4(0.2784313857555389f, 0.2784313857555389f, 0.2784313857555389f, 1.0f);
        theme.style->Colors[ImGuiCol_TitleBg] = ImVec4(0.1450980454683304f, 0.1450980454683304f, 0.1450980454683304f, 1.0f);
        theme.style->Colors[ImGuiCol_TitleBgActive] = ImVec4(0.1450980454683304f, 0.1450980454683304f, 0.1450980454683304f, 1.0f);
        theme.style->Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.1450980454683304f, 0.1450980454683304f, 0.1450980454683304f, 1.0f);
        theme.style->Colors[ImGuiCol_MenuBarBg] = ImVec4(0.1921568661928177f, 0.1921568661928177f, 0.1921568661928177f, 1.0f);
        theme.style->Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.1568627506494522f, 0.1568627506494522f, 0.1568627506494522f, 1.0f);
        theme.style->Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.2745098173618317f, 0.2745098173618317f, 0.2745098173618317f, 1.0f);
        theme.style->Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.2980392277240753f, 0.2980392277240753f, 0.2980392277240753f, 1.0f);
        theme.style->Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(1.0f, 0.3882353007793427f, 0.0f, 1.0f);
        theme.style->Colors[ImGuiCol_CheckMark] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
        theme.style->Colors[ImGuiCol_SliderGrab] = ImVec4(0.3882353007793427f, 0.3882353007793427f, 0.3882353007793427f, 1.0f);
        theme.style->Colors[ImGuiCol_SliderGrabActive] = ImVec4(1.0f, 0.3882353007793427f, 0.0f, 1.0f);
        theme.style->Colors[ImGuiCol_Button] = ImVec4(1.0f, 1.0f, 1.0f, 30.f / 255.f);
        theme.style->Colors[ImGuiCol_ButtonHovered] = ImVec4(1.0f, 1.0f, 1.0f, 0.1560000032186508f);
        theme.style->Colors[ImGuiCol_ButtonActive] = ImVec4(1.0f, 1.0f, 1.0f, 0.3910000026226044f);
        theme.style->Colors[ImGuiCol_Header] = ImVec4(0.3098039329051971f, 0.3098039329051971f, 0.3098039329051971f, 1.0f);
        theme.style->Colors[ImGuiCol_HeaderHovered] = ImVec4(0.4666666686534882f, 0.4666666686534882f, 0.4666666686534882f, 1.0f);
        theme.style->Colors[ImGuiCol_HeaderActive] = ImVec4(0.4666666686534882f, 0.4666666686534882f, 0.4666666686534882f, 1.0f);
        theme.style->Colors[ImGuiCol_Separator] = ImVec4(0.2627451121807098f, 0.2627451121807098f, 0.2627451121807098f, 1.0f);
        theme.style->Colors[ImGuiCol_SeparatorHovered] = ImVec4(0.3882353007793427f, 0.3882353007793427f, 0.3882353007793427f, 1.0f);
        theme.style->Colors[ImGuiCol_SeparatorActive] = ImVec4(1.0f, 0.3882353007793427f, 0.0f, 1.0f);
        theme.style->Colors[ImGuiCol_ResizeGrip] = ImVec4(1.0f, 1.0f, 1.0f, 0.25f);
        theme.style->Colors[ImGuiCol_ResizeGripHovered] = ImVec4(1.0f, 1.0f, 1.0f, 0.6700000166893005f);
        theme.style->Colors[ImGuiCol_ResizeGripActive] = ImVec4(1.0f, 0.3882353007793427f, 0.0f, 1.0f);
        theme.style->Colors[ImGuiCol_Tab] = ImVec4(0.09411764889955521f, 0.09411764889955521f, 0.09411764889955521f, 1.0f);
        theme.style->Colors[ImGuiCol_TabHovered] = ImVec4(0.3490196168422699f, 0.3490196168422699f, 0.3490196168422699f, 1.0f);
        theme.style->Colors[ImGuiCol_TabActive] = ImVec4(0.1921568661928177f, 0.1921568661928177f, 0.1921568661928177f, 1.0f);
        theme.style->Colors[ImGuiCol_TabUnfocused] = ImVec4(0.09411764889955521f, 0.09411764889955521f, 0.09411764889955521f, 1.0f);
        theme.style->Colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.1921568661928177f, 0.1921568661928177f, 0.1921568661928177f, 1.0f);
        theme.style->Colors[ImGuiCol_PlotLines] = ImVec4(0.4666666686534882f, 0.4666666686534882f, 0.4666666686534882f, 1.0f);
        theme.style->Colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.0f, 0.3882353007793427f, 0.0f, 1.0f);
        theme.style->Colors[ImGuiCol_PlotHistogram] = ImVec4(0.5843137502670288f, 0.5843137502670288f, 0.5843137502670288f, 1.0f);
        theme.style->Colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.0f, 0.3882353007793427f, 0.0f, 1.0f);
        theme.style->Colors[ImGuiCol_TableHeaderBg] = ImVec4(0.1882352977991104f, 0.1882352977991104f, 0.2000000029802322f, 1.0f);
        theme.style->Colors[ImGuiCol_TableBorderStrong] = ImVec4(0.3098039329051971f, 0.3098039329051971f, 0.3490196168422699f, 1.0f);
        theme.style->Colors[ImGuiCol_TableBorderLight] = ImVec4(0.2274509817361832f, 0.2274509817361832f, 0.2470588237047195f, 1.0f);
        theme.style->Colors[ImGuiCol_TableRowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
        theme.style->Colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.0f, 1.0f, 1.0f, 0.05999999865889549f);
        theme.style->Colors[ImGuiCol_TextSelectedBg] = ImVec4(1.0f, 1.0f, 1.0f, 0.1560000032186508f);
        theme.style->Colors[ImGuiCol_DragDropTarget] = ImVec4(1.0f, 0.3882353007793427f, 0.0f, 1.0f);
        theme.style->Colors[ImGuiCol_NavHighlight] = ImVec4(1.0f, 0.3882353007793427f, 0.0f, 1.0f);
        theme.style->Colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.0f, 0.3882353007793427f, 0.0f, 1.0f);
        theme.style->Colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.5860000252723694f);
        theme.style->Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.5860000252723694f);

        theme.style->FrameBorderSize = 1.f;
        theme.style->FrameRounding = 1.f;
    }
}

void GameRenderer::InitImgui()
{
    ImGui::CreateContext();
    ImNodes::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigWindowsMoveFromTitleBarOnly = true;
    //io.ConfigViewportsNoAutoMerge = true;

    ImFontConfig font_cfg;
    font_cfg.FontDataOwnedByAtlas = false;
    io.Fonts->AddFontFromMemoryTTF((void*)ruda, sizeof(ruda), 17.f, &font_cfg);

    // Initialize notify
    //ImGui::MergeIconsWithLatestFont(16.f, false); // I don't think this is needed anymore, do we need something else?

    // Create Themes
    {
        CreateImguiThemes();
    }

    // Apply Theme
    {
        const char* theme = CVarSystem::Get()->GetStringCVar(CVarCategory::Client, "imguiTheme");
        u32 themeHash = StringUtils::fnv1a_32(theme, strlen(theme));

        if (!_themeNameHashToIndex.contains(themeHash))
        {
            static std::string defaultTheme = "Blue Teal";
            CVarSystem::Get()->SetStringCVar(CVarCategory::Client, "imguiTheme", defaultTheme.c_str());
            themeHash = StringUtils::fnv1a_32(defaultTheme.c_str(), defaultTheme.length());
        }

        SetImguiTheme(themeHash);
    }

    ImPlot::CreateContext();
    ImGui_ImplGlfw_InitForVulkan(_window->GetWindow(), true);

    _renderer->InitImgui();
}
