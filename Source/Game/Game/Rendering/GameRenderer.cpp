#include "GameRenderer.h"
#include "Canvas/CanvasRenderer.h"
#include "UIRenderer.h"
#include "Debug/DebugRenderer.h"
#include "Debug/JoltDebugRenderer.h"
#include "Terrain/TerrainRenderer.h"
#include "Terrain/TerrainLoader.h"
#include "Terrain/TerrainManipulator.h"
#include "Model/ModelRenderer.h"
#include "Model/ModelLoader.h"
#include "Water/WaterRenderer.h"
#include "Water/WaterLoader.h"
#include "Material/MaterialRenderer.h"
#include "Skybox/SkyboxRenderer.h"
#include "Editor/EditorRenderer.h"
#include "PixelQuery.h"
#include "CullUtils.h"

#include "Game/Editor/EditorHandler.h"
#include "Game/Editor/Viewport.h"
#include "Game/Gameplay/MapLoader.h"
#include "Game/Util/ServiceLocator.h"

#include <Input/InputManager.h>
#include <Renderer/Renderer.h>
#include <Renderer/RenderSettings.h>
#include <Renderer/RenderGraph.h>
#include <Renderer/Window.h>
#include <Renderer/Descriptors/RenderGraphDesc.h>
#include <Renderer/Renderers/Vulkan/RendererVK.h>
#include <Renderer/Renderers/Vulkan/Backend/stb_image.h>

#include <imgui/imgui.h>
#include <imgui/imgui_notify.h>
#include <imgui/implot.h>
#include <imgui/imnodes.h>
#include <imgui/ruda.h>
#include <imgui/backends/imgui_impl_glfw.h>

#include <gli/gli.hpp>
#include <GLFW/glfw3.h>

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

    KeybindGroup* debugKeybindGroup = inputManager->CreateKeybindGroup("Debug", 15);
    debugKeybindGroup->SetActive(true);

    glfwSetKeyCallback(_window->GetWindow(), KeyCallback);
    glfwSetCharCallback(_window->GetWindow(), CharCallback);
    glfwSetMouseButtonCallback(_window->GetWindow(), MouseCallback);
    glfwSetCursorPosCallback(_window->GetWindow(), CursorPositionCallback);
    glfwSetScrollCallback(_window->GetWindow(), ScrollCallback);
    glfwSetWindowIconifyCallback(_window->GetWindow(), WindowIconifyCallback);

	_renderer = new Renderer::RendererVK(_window);

    std::string shaderSourcePath = SHADER_SOURCE_DIR;
    _renderer->SetShaderSourceDirectory(shaderSourcePath);

    InitImgui();
	_renderer->InitDebug();
	_renderer->InitWindow(_window);

    _debugRenderer = new DebugRenderer(_renderer);
    _joltDebugRenderer = new JoltDebugRenderer(_renderer, _debugRenderer);

    _modelRenderer = new ModelRenderer(_renderer, _debugRenderer);
    _modelLoader = new ModelLoader(_modelRenderer);
    _modelLoader->Init();

    _waterRenderer = new WaterRenderer(_renderer, _debugRenderer);
    _waterLoader = new WaterLoader(_waterRenderer);

    _terrainRenderer = new TerrainRenderer(_renderer, _debugRenderer);
    _terrainLoader = new TerrainLoader(_terrainRenderer, _modelLoader, _waterLoader);
    _terrainManipulator = new TerrainManipulator(*_terrainRenderer, *_debugRenderer);

    _mapLoader = new MapLoader(_terrainLoader, _modelLoader, _waterLoader);

    _materialRenderer = new MaterialRenderer(_renderer, _terrainRenderer, _modelRenderer);
    _skyboxRenderer = new SkyboxRenderer(_renderer, _debugRenderer);
    _editorRenderer = new EditorRenderer(_renderer, _debugRenderer);
    _canvasRenderer = new CanvasRenderer(_renderer);
    _uiRenderer = new UIRenderer(_renderer);
    _pixelQuery = new PixelQuery(_renderer);

    CreatePermanentResources();

    DepthPyramidUtils::InitBuffers(_renderer);

    _nameHashToCursor.reserve(128);
}

GameRenderer::~GameRenderer()
{
	delete _renderer;
}

bool GameRenderer::UpdateWindow(f32 deltaTime)
{
    return _window->Update(deltaTime);
}

void GameRenderer::UpdateRenderers(f32 deltaTime)
{
    // Reset the memory in the frameAllocator
    _frameAllocator[_frameIndex]->Reset();

    _skyboxRenderer->Update(deltaTime);
    _mapLoader->Update(deltaTime);
    _terrainLoader->Update(deltaTime);
    _terrainRenderer->Update(deltaTime);
    _terrainManipulator->Update(deltaTime);
    _modelLoader->Update(deltaTime);
    _modelRenderer->Update(deltaTime);
    _waterLoader->Update(deltaTime);
    _waterRenderer->Update(deltaTime);
    _materialRenderer->Update(deltaTime);
    _joltDebugRenderer->Update(deltaTime);
    _debugRenderer->Update(deltaTime);
    _pixelQuery->Update(deltaTime);
    _editorRenderer->Update(deltaTime);
    _canvasRenderer->Update(deltaTime);
    _uiRenderer->Update(deltaTime);
}

f32 GameRenderer::Render()
{
    // If the window is minimized we want to pause rendering
    if (_window->IsMinimized())
        return 0.0f;

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
            Renderer::ImageMutableResource visibilityBuffer;
            Renderer::ImageMutableResource sceneColor;
            Renderer::ImageMutableResource finalColor;
            Renderer::ImageMutableResource transparency;
            Renderer::ImageMutableResource transparencyWeights;
            Renderer::DepthImageMutableResource depth;
        };

        renderGraph.AddPass<StartFramePassData>("StartFramePass",
            [=](StartFramePassData& data, Renderer::RenderGraphBuilder& builder) // Setup
            {
                data.visibilityBuffer = builder.Write(_resources.visibilityBuffer, Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::CLEAR);
                data.sceneColor = builder.Write(_resources.sceneColor, Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::CLEAR);
                data.finalColor = builder.Write(_resources.finalColor, Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::CLEAR);
                data.transparency = builder.Write(_resources.transparency, Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::CLEAR);
                data.transparencyWeights = builder.Write(_resources.transparencyWeights, Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::CLEAR);
                data.depth = builder.Write(_resources.depth, Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::CLEAR);
                
                return true; // Return true from setup to enable this pass, return false to disable it
            },
            [&](StartFramePassData& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList) // Execute
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
        [=](PyramidPassData& data, Renderer::RenderGraphBuilder& builder) // Setup
        {
            using BufferUsage = Renderer::BufferPassUsage;

            data.depth = builder.Read(_resources.depth, Renderer::PipelineType::GRAPHICS);
            data.depthPyramid = builder.Write(_resources.depthPyramid, Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::LOAD);

            builder.Write(DepthPyramidUtils::_atomicBuffer, BufferUsage::COMPUTE);

            data.copyDescriptorSet = builder.Use(DepthPyramidUtils::_copyDescriptorSet);
            data.pyramidDescriptorSet = builder.Use(DepthPyramidUtils::_pyramidDescriptorSet);

            return true; // Return true from setup to enable this pass, return false to disable it
        },
        [=](PyramidPassData& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList) // Execute
        {
            GPU_SCOPED_PROFILER_ZONE(commandList, BuildPyramid);

            DepthPyramidUtils::BuildPyramidParams params;
            params.renderer = _renderer;
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

    _skyboxRenderer->AddSkyboxPass(&renderGraph, _resources, _frameIndex);

    _modelRenderer->AddTransparencyCullingPass(&renderGraph, _resources, _frameIndex);
    _modelRenderer->AddTransparencyGeometryPass(&renderGraph, _resources, _frameIndex);

    _waterRenderer->AddCopyDepthPass(&renderGraph, _resources, _frameIndex);
    _waterRenderer->AddCullingPass(&renderGraph, _resources, _frameIndex);
    _waterRenderer->AddGeometryPass(&renderGraph, _resources, _frameIndex);

    _materialRenderer->AddMaterialPass(&renderGraph, _resources, _frameIndex);

    _pixelQuery->AddPixelQueryPass(&renderGraph, _resources, _frameIndex);

    _editorRenderer->AddWorldGridPass(&renderGraph, _resources, _frameIndex);
    _debugRenderer->Add3DPass(&renderGraph, _resources, _frameIndex);
    _debugRenderer->Add2DPass(&renderGraph, _resources, _frameIndex);

    _canvasRenderer->AddCanvasPass(&renderGraph, _resources, _frameIndex);

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

const std::string& GameRenderer::GetGPUName()
{
    return _renderer->GetGPUName();
}

void GameRenderer::CreatePermanentResources()
{
    // Visibility Buffer rendertarget
    Renderer::ImageDesc visibilityBufferDesc;
    visibilityBufferDesc.debugName = "VisibilityBuffer";
    visibilityBufferDesc.dimensions = vec2(1.0f, 1.0f);
    visibilityBufferDesc.dimensionType = Renderer::ImageDimensionType::DIMENSION_SCALE_RENDERSIZE;
    visibilityBufferDesc.format = Renderer::ImageFormat::R32G32B32A32_UINT;
    visibilityBufferDesc.sampleCount = Renderer::SampleCount::SAMPLE_COUNT_1;
    visibilityBufferDesc.clearUInts = uvec4(0, 0, 0, 0);

    _resources.visibilityBuffer = _renderer->CreateImage(visibilityBufferDesc);

    // Scene color rendertarget
    Renderer::ImageDesc sceneColorDesc;
    sceneColorDesc.debugName = "SceneColor";
    sceneColorDesc.dimensions = vec2(1.0f, 1.0f);
    sceneColorDesc.dimensionType = Renderer::ImageDimensionType::DIMENSION_SCALE_RENDERSIZE;
    sceneColorDesc.format = Renderer::ImageFormat::R16G16B16A16_FLOAT;
    sceneColorDesc.sampleCount = Renderer::SampleCount::SAMPLE_COUNT_1;
    sceneColorDesc.clearColor = Color(0.52f, 0.80f, 0.92f, 1.0f); // Sky blue

    _resources.sceneColor = _renderer->CreateImage(sceneColorDesc);

    sceneColorDesc.debugName = "FinalColor";
    sceneColorDesc.dimensionType = Renderer::ImageDimensionType::DIMENSION_SCALE_WINDOW;
    sceneColorDesc.clearColor = Color(0.43f, 0.50f, 0.56f, 1.0f); // Slate gray
    _resources.finalColor = _renderer->CreateImage(sceneColorDesc);

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
    _resources.cameras.Get().push_back(Camera());
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

    // Apply theme
    {
        ImGuiStyle& style = ImGui::GetStyle();

        style.FrameRounding = 2.0f;
        style.GrabRounding = 2.0f;
        style.WindowMinSize = vec2(100, 100);

        ImVec4* colors = style.Colors;
        colors[ImGuiCol_Text] = ImVec4(0.95f, 0.96f, 0.98f, 1.00f);
        colors[ImGuiCol_TextDisabled] = ImVec4(0.36f, 0.42f, 0.47f, 1.00f);
        colors[ImGuiCol_WindowBg] = ImVec4(0.11f, 0.15f, 0.17f, 1.00f);
        colors[ImGuiCol_ChildBg] = ImVec4(0.15f, 0.18f, 0.22f, 1.00f);
        colors[ImGuiCol_PopupBg] = ImVec4(0.08f, 0.08f, 0.08f, 0.94f);
        colors[ImGuiCol_Border] = ImVec4(0.08f, 0.10f, 0.12f, 1.00f);
        colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        colors[ImGuiCol_FrameBg] = ImVec4(0.20f, 0.25f, 0.29f, 1.00f);
        colors[ImGuiCol_FrameBgHovered] = ImVec4(0.12f, 0.20f, 0.28f, 1.00f);
        colors[ImGuiCol_FrameBgActive] = ImVec4(0.09f, 0.12f, 0.14f, 1.00f);
        colors[ImGuiCol_TitleBg] = ImVec4(0.09f, 0.12f, 0.14f, 0.65f);
        colors[ImGuiCol_TitleBgActive] = ImVec4(0.08f, 0.10f, 0.12f, 1.00f);
        colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.00f, 0.00f, 0.00f, 0.51f);
        colors[ImGuiCol_MenuBarBg] = ImVec4(0.15f, 0.18f, 0.22f, 1.00f);
        colors[ImGuiCol_ScrollbarBg] = ImVec4(0.02f, 0.02f, 0.02f, 0.39f);
        colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.20f, 0.25f, 0.29f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.18f, 0.22f, 0.25f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.09f, 0.21f, 0.31f, 1.00f);
        colors[ImGuiCol_CheckMark] = ImVec4(0.28f, 0.56f, 1.00f, 1.00f);
        colors[ImGuiCol_SliderGrab] = ImVec4(0.28f, 0.56f, 1.00f, 1.00f);
        colors[ImGuiCol_SliderGrabActive] = ImVec4(0.37f, 0.61f, 1.00f, 1.00f);
        colors[ImGuiCol_Button] = ImVec4(0.20f, 0.25f, 0.29f, 1.00f);
        colors[ImGuiCol_ButtonHovered] = ImVec4(0.28f, 0.56f, 1.00f, 1.00f);
        colors[ImGuiCol_ButtonActive] = ImVec4(0.06f, 0.53f, 0.98f, 1.00f);
        colors[ImGuiCol_Header] = ImVec4(0.20f, 0.25f, 0.29f, 0.55f);
        colors[ImGuiCol_HeaderHovered] = ImVec4(0.26f, 0.59f, 0.98f, 0.80f);
        colors[ImGuiCol_HeaderActive] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
        colors[ImGuiCol_Separator] = ImVec4(0.20f, 0.25f, 0.29f, 1.00f);
        colors[ImGuiCol_SeparatorHovered] = ImVec4(0.10f, 0.40f, 0.75f, 0.78f);
        colors[ImGuiCol_SeparatorActive] = ImVec4(0.10f, 0.40f, 0.75f, 1.00f);
        colors[ImGuiCol_ResizeGrip] = ImVec4(0.26f, 0.59f, 0.98f, 0.25f);
        colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.26f, 0.59f, 0.98f, 0.67f);
        colors[ImGuiCol_ResizeGripActive] = ImVec4(0.26f, 0.59f, 0.98f, 0.95f);
        colors[ImGuiCol_Tab] = ImVec4(0.11f, 0.15f, 0.17f, 1.00f);
        colors[ImGuiCol_TabHovered] = ImVec4(0.26f, 0.59f, 0.98f, 0.80f);
        colors[ImGuiCol_TabActive] = ImVec4(0.20f, 0.25f, 0.29f, 1.00f);
        colors[ImGuiCol_TabUnfocused] = ImVec4(0.11f, 0.15f, 0.17f, 1.00f);
        colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.11f, 0.15f, 0.17f, 1.00f);
        colors[ImGuiCol_PlotLines] = ImVec4(0.61f, 0.61f, 0.61f, 1.00f);
        colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
        colors[ImGuiCol_PlotHistogram] = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
        colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
        colors[ImGuiCol_TextSelectedBg] = ImVec4(0.26f, 0.59f, 0.98f, 0.35f);
        colors[ImGuiCol_DragDropTarget] = ImVec4(1.00f, 1.00f, 0.00f, 0.90f);
        colors[ImGuiCol_NavHighlight] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
        colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
        colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
        colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.35f);
    }

    ImPlot::CreateContext();
    ImGui_ImplGlfw_InitForVulkan(_window->GetWindow(), true);

    _renderer->InitImgui();
}
