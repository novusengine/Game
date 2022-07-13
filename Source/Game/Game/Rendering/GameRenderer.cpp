#include "GameRenderer.h"
#include "UIRenderer.h"
#include "Debug/DebugRenderer.h"
#include "Terrain/TerrainRenderer.h"
#include "Model/ModelRenderer.h"
#include "Game/Util/ServiceLocator.h"

#include <Input/InputManager.h>
#include <Renderer/Renderer.h>
#include <Renderer/RenderSettings.h>
#include <Renderer/RenderGraph.h>
#include <Renderer/Window.h>
#include <Renderer/Descriptors/RenderGraphDesc.h>
#include <Renderer/Renderers/Vulkan/RendererVK.h>

#include <imgui/imgui.h>
#include <imgui/imgui_notify.h>
#include <imgui/implot.h>
#include <imgui/ruda.h>
#include <imgui/backends/imgui_impl_glfw.h>

#include <windows.h> // Get rid of this when we have our input manager :(

void KeyCallback(GLFWwindow* window, i32 key, i32 scancode, i32 action, i32 modifiers)
{
    ServiceLocator::GetGameRenderer()->GetInputManager()->KeyboardInputHandler(key, scancode, action, modifiers);
}

void CharCallback(GLFWwindow* window, u32 unicodeKey)
{
    ServiceLocator::GetGameRenderer()->GetInputManager()->CharInputHandler(unicodeKey);
}

void MouseCallback(GLFWwindow* window, i32 button, i32 action, i32 modifiers)
{
    ServiceLocator::GetGameRenderer()->GetInputManager()->MouseInputHandler(button, action, modifiers);
}

void CursorPositionCallback(GLFWwindow* window, f64 x, f64 y)
{
    ServiceLocator::GetGameRenderer()->GetInputManager()->MousePositionHandler(static_cast<f32>(x), static_cast<f32>(y));
}

void ScrollCallback(GLFWwindow* window, f64 x, f64 y)
{
    ServiceLocator::GetGameRenderer()->GetInputManager()->MouseScrollHandler(static_cast<f32>(x), static_cast<f32>(y));
}

void WindowIconifyCallback(GLFWwindow* window, int iconified)
{
    Window* userWindow = reinterpret_cast<Window*>(glfwGetWindowUserPointer(window));
    userWindow->SetIsMinimized(iconified == 1);
}

GameRenderer::GameRenderer()
{
	_window = new Window();
	_window->Init(Renderer::Settings::SCREEN_WIDTH, Renderer::Settings::SCREEN_HEIGHT);

    _inputManager = new InputManager();
    KeybindGroup* keybindGroup = _inputManager->CreateKeybindGroup("Camera", 10);
    keybindGroup->SetActive(true);

    glfwSetKeyCallback(_window->GetWindow(), KeyCallback);
    glfwSetCharCallback(_window->GetWindow(), CharCallback);
    glfwSetMouseButtonCallback(_window->GetWindow(), MouseCallback);
    glfwSetCursorPosCallback(_window->GetWindow(), CursorPositionCallback);
    glfwSetScrollCallback(_window->GetWindow(), ScrollCallback);
    glfwSetWindowIconifyCallback(_window->GetWindow(), WindowIconifyCallback);

    keybindGroup->AddKeyboardCallback("Forward", GLFW_KEY_W, KeybindAction::Press, KeybindModifier::Any, nullptr);
    keybindGroup->AddKeyboardCallback("Backward", GLFW_KEY_S, KeybindAction::Press, KeybindModifier::Any, nullptr);
    keybindGroup->AddKeyboardCallback("Left", GLFW_KEY_A, KeybindAction::Press, KeybindModifier::Any, nullptr);
    keybindGroup->AddKeyboardCallback("Right", GLFW_KEY_D, KeybindAction::Press, KeybindModifier::Any, nullptr);
    keybindGroup->AddKeyboardCallback("Upwards", GLFW_KEY_E, KeybindAction::Press, KeybindModifier::Any, nullptr);
    keybindGroup->AddKeyboardCallback("Downwards", GLFW_KEY_Q, KeybindAction::Press, KeybindModifier::Any, nullptr);
    keybindGroup->AddKeyboardCallback("ToggleMouseCapture", GLFW_KEY_ESCAPE, KeybindAction::Press, KeybindModifier::Any, [this](i32 key, KeybindAction action, KeybindModifier modifier)
    {
        _captureMouse = !_captureMouse;
        if (_captureMouse)
        {
            ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouse;
            glfwSetInputMode(_window->GetWindow(), GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            DebugHandler::Print("Mouse captured!");
        }
        else
        {
            ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
            glfwSetInputMode(_window->GetWindow(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            DebugHandler::Print("Mouse released!");
        }

        return true;
    });
    keybindGroup->AddKeyboardCallback("Right Mouseclick", GLFW_MOUSE_BUTTON_RIGHT, KeybindAction::Click, KeybindModifier::Any, [this](i32 key, KeybindAction action, KeybindModifier modifier)
    {
        if (!_captureMouse)
        {
            _captureMouse = true;

            _prevMousePosition = vec2(_inputManager->GetMousePositionX(), _inputManager->GetMousePositionY());
            glfwSetInputMode(_window->GetWindow(), GLFW_CURSOR, GLFW_CURSOR_DISABLED);

            ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouse;
            DebugHandler::Print("Mouse captured because of mouseclick!");
        }
        return true;
    });
    keybindGroup->AddMousePositionCallback([this](f32 xPos, f32 yPos)
    {
        if (_captureMouse)
        {
            CapturedMouseMoved(vec2(xPos, yPos));
        }

        return _captureMouse;
    });

	_renderer = new Renderer::RendererVK();

    std::string shaderSourcePath = SHADER_SOURCE_DIR;
    _renderer->SetShaderSourceDirectory(shaderSourcePath);

    InitImgui();
	_renderer->InitDebug();
	_renderer->InitWindow(_window);

    _terrainRenderer = new TerrainRenderer(_renderer);
    _modelRenderer = new ModelRenderer(_renderer);
    _debugRenderer = new DebugRenderer(_renderer);
    _uiRenderer = new UIRenderer(_renderer);

    // Set camera position
    _resources.cameraComponent.position = vec3(0, 10, -10);
    _resources.cameraComponent.pitch = 30.0f;

    CreatePermanentResources();
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
    // Update camera
    {
        KeybindGroup* keybindGroup = _inputManager->GetKeybindGroupByHash("Camera"_h);

        // Input
        if (keybindGroup->IsKeybindPressed("Forward"_h))
        {
            _resources.cameraComponent.position += _resources.cameraComponent.forward * _cameraSpeed * deltaTime;
        }
        if (keybindGroup->IsKeybindPressed("Backward"_h))
        {
            _resources.cameraComponent.position += -_resources.cameraComponent.forward * _cameraSpeed * deltaTime;
        }
        if (keybindGroup->IsKeybindPressed("Left"_h))
        {
            _resources.cameraComponent.position += -_resources.cameraComponent.right * _cameraSpeed * deltaTime;
        }
        if (keybindGroup->IsKeybindPressed("Right"_h))
        {
            _resources.cameraComponent.position += _resources.cameraComponent.right * _cameraSpeed * deltaTime;
        }
        if (keybindGroup->IsKeybindPressed("Upwards"_h))
        {
            _resources.cameraComponent.position += vec3(0.0f, 1.0f, 0.0f) * _cameraSpeed * deltaTime;
        }
        if (keybindGroup->IsKeybindPressed("Downwards"_h))
        {
            _resources.cameraComponent.position += vec3(0.0f, -1.0f, 0.0f) * _cameraSpeed * deltaTime;
        }

        // Compute matrices
        SafeVectorScopedWriteLock camerasLock(_resources.cameras);
        std::vector<Camera>& cameras = camerasLock.Get();
        Camera& camera = cameras[_resources.cameraComponent.cameraIndex];
        
        // TODO: Figure the order of this one out
        quat rotQuat = quat(vec3(glm::radians(_resources.cameraComponent.pitch), glm::radians(_resources.cameraComponent.yaw), glm::radians(_resources.cameraComponent.roll)));
        mat4x4 rotationMatrix = glm::mat4_cast(rotQuat);

        mat4x4 cameraMatrix = glm::translate(mat4x4(1.0f), _resources.cameraComponent.position) * rotationMatrix;
        camera.worldToView = glm::inverse(cameraMatrix);

        f32 aspectRatioWH = static_cast<f32>(Renderer::Settings::SCREEN_WIDTH) / static_cast<f32>(Renderer::Settings::SCREEN_HEIGHT);

        camera.viewToClip = glm::perspective(glm::radians(75.0f), aspectRatioWH, 1000.0f, 0.5f);
        camera.worldToClip = camera.viewToClip * camera.worldToView;

        // Update camera vectors
        _resources.cameraComponent.forward = rotationMatrix[2];
        _resources.cameraComponent.right = rotationMatrix[0];
        _resources.cameraComponent.up = rotationMatrix[1];

        _resources.cameras.SetDirtyElement(_resources.cameraComponent.cameraIndex);

        // Print position
        if (ImGui::Begin("Camera"))
        {
            ImGui::Text("Pos: (%.2f, %.2f, %.2f)", _resources.cameraComponent.position.x, _resources.cameraComponent.position.y, _resources.cameraComponent.position.z);
            
            ImGui::Separator();
            
            ImGui::Text("Pitch: %.2f", _resources.cameraComponent.pitch);
            ImGui::Text("Yaw: %.2f", _resources.cameraComponent.yaw);
            ImGui::Text("Roll: %.2f", _resources.cameraComponent.roll);

            vec3 eulerAngles = glm::eulerAngles(rotQuat);
            ImGui::Text("Euler: (%.2f, %.2f, %.2f)", eulerAngles.x, eulerAngles.y, eulerAngles.z);

            ImGui::Separator();

            ImGui::Text("Forward: (%.2f, %.2f, %.2f)", _resources.cameraComponent.forward.x, _resources.cameraComponent.forward.y, _resources.cameraComponent.forward.z);
            ImGui::Text("Right: (%.2f, %.2f, %.2f)", _resources.cameraComponent.right.x, _resources.cameraComponent.right.y, _resources.cameraComponent.right.z);
            ImGui::Text("Up: (%.2f, %.2f, %.2f)", _resources.cameraComponent.up.x, _resources.cameraComponent.up.y, _resources.cameraComponent.up.z);
        }
        ImGui::End();
    }

    // Reset the memory in the frameAllocator
    _frameAllocator->Reset();

    //_terrainRenderer->Update(deltaTime);
    _modelRenderer->Update(deltaTime);
    _debugRenderer->Update(deltaTime);
    _uiRenderer->Update(deltaTime);
}

void GameRenderer::Render()
{
    // If the window is minimized we want to pause rendering
    if (_window->IsMinimized())
        return;

    if (_resources.cameras.SyncToGPU(_renderer))
    {
        _resources.globalDescriptorSet.Bind("_cameras", _resources.cameras.GetBuffer());
    }

    // Create rendergraph
    Renderer::RenderGraphDesc renderGraphDesc;
    renderGraphDesc.allocator = _frameAllocator; // We need to give our rendergraph an allocator to use
    Renderer::RenderGraph renderGraph = _renderer->CreateRenderGraph(renderGraphDesc);

    _renderer->FlipFrame(_frameIndex);

    ImGui::Render();

    // StartFrame Pass
    {
        struct StartFramePassData
        {
            Renderer::RenderPassMutableResource finalColor;
            Renderer::RenderPassMutableResource depth;
        };

        renderGraph.AddPass<StartFramePassData>("StartFramePass",
            [=](StartFramePassData& data, Renderer::RenderGraphBuilder& builder) // Setup
            {
                data.finalColor = builder.Write(_resources.finalColor, Renderer::RenderGraphBuilder::WriteMode::RENDERTARGET, Renderer::RenderGraphBuilder::LoadMode::CLEAR);
                data.depth = builder.Write(_resources.depth, Renderer::RenderGraphBuilder::WriteMode::RENDERTARGET, Renderer::RenderGraphBuilder::LoadMode::CLEAR);
                
                return true; // Return true from setup to enable this pass, return false to disable it
            },
            [&](StartFramePassData& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList) // Execute
            {
                GPU_SCOPED_PROFILER_ZONE(commandList, StartFramePass);
                commandList.MarkFrameStart(_frameIndex);

                // Set viewport
                //vec2 renderSize = _renderer->GetRenderSize();
                vec2 renderSize = _renderer->GetImageDimension(_resources.finalColor);

                commandList.SetViewport(0, 0, renderSize.x, renderSize.y, 0.0f, 1.0f);
                commandList.SetScissorRect(0, static_cast<u32>(renderSize.x), 0, static_cast<u32>(renderSize.y));
            });
    }
    //_terrainRenderer->AddTriangularizationPass(&renderGraph, _resources, _frameIndex);

    //_terrainRenderer->AddGeometryPass(&renderGraph, _resources, _frameIndex);
    _modelRenderer->AddCullingPass(&renderGraph, _resources, _frameIndex);
    _modelRenderer->AddGeometryPass(&renderGraph, _resources, _frameIndex);

    _debugRenderer->Add3DPass(&renderGraph, _resources, _frameIndex);
    _debugRenderer->Add2DPass(&renderGraph, _resources, _frameIndex);
    _uiRenderer->AddImguiPass(&renderGraph, _resources, _frameIndex, _resources.finalColor);

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

    Renderer::ImageID finalTarget = _resources.finalColor;
    _renderer->Present(_window, finalTarget, _resources.sceneRenderedSemaphore);

    // Flip the frameIndex between 0 and 1
    _frameIndex = !_frameIndex;
}

void GameRenderer::CreatePermanentResources()
{
    Renderer::ImageDesc sceneColorDesc;
    sceneColorDesc.debugName = "FinalColor";
    sceneColorDesc.dimensions = vec2(1.0f, 1.0f);
    sceneColorDesc.dimensionType = Renderer::ImageDimensionType::DIMENSION_SCALE_WINDOW;
    sceneColorDesc.format = Renderer::ImageFormat::R16G16B16A16_FLOAT;
    sceneColorDesc.sampleCount = Renderer::SampleCount::SAMPLE_COUNT_1;
    sceneColorDesc.clearColor = Color::Black;

    _resources.finalColor = _renderer->CreateImage(sceneColorDesc);

    // Main depth rendertarget
    Renderer::DepthImageDesc mainDepthDesc;
    mainDepthDesc.debugName = "MainDepth";
    mainDepthDesc.dimensions = vec2(1.0f, 1.0f);
    mainDepthDesc.dimensionType = Renderer::ImageDimensionType::DIMENSION_SCALE_RENDERSIZE;
    mainDepthDesc.format = Renderer::DepthImageFormat::D32_FLOAT;
    mainDepthDesc.sampleCount = Renderer::SampleCount::SAMPLE_COUNT_1;
    mainDepthDesc.depthClearValue = 0.0f;

    _resources.depth = _renderer->CreateDepthImage(mainDepthDesc);

    // Frame allocator, this is a fast allocator for data that is only needed this frame
    {
        const size_t FRAME_ALLOCATOR_SIZE = 16 * 1024 * 1024; // 16 MB

        _frameAllocator = new Memory::StackAllocator();
        _frameAllocator->Init(FRAME_ALLOCATOR_SIZE);
    }

    _resources.sceneRenderedSemaphore = _renderer->CreateNSemaphore();
    for (u32 i = 0; i < _resources.frameSyncSemaphores.Num; i++)
    {
        _resources.frameSyncSemaphores.Get(i) = _renderer->CreateNSemaphore();
    }

    _resources.cameras.SetDebugName("Cameras");
    _resources.cameras.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);
    _resources.cameras.PushBack(Camera());
}

void GameRenderer::InitImgui()
{
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigWindowsMoveFromTitleBarOnly = true;

    ImFontConfig font_cfg;
    font_cfg.FontDataOwnedByAtlas = false;
    io.Fonts->AddFontFromMemoryTTF((void*)ruda, sizeof(ruda), 17.f, &font_cfg);

    // Initialize notify
    ImGui::MergeIconsWithLatestFont(16.f, false);

    // Apply theme
    {
        ImGui::GetStyle().FrameRounding = 4.0f;
        ImGui::GetStyle().GrabRounding = 4.0f;

        ImVec4* colors = ImGui::GetStyle().Colors;
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

void GameRenderer::CapturedMouseMoved(vec2 pos)
{
    if (_captureMouseHasMoved)
    {
        vec2 deltaPosition = _prevMousePosition - pos;

        
        _resources.cameraComponent.yaw += -deltaPosition.x * _mouseSensitivity;

        if (_resources.cameraComponent.yaw > 360)
            _resources.cameraComponent.yaw -= 360;
        else if (_resources.cameraComponent.yaw < 0)
            _resources.cameraComponent.yaw += 360;

        _resources.cameraComponent.pitch = Math::Clamp(_resources.cameraComponent.pitch - (deltaPosition.y * _mouseSensitivity), -89.0f, 89.0f);
    }
    else
        _captureMouseHasMoved = true;

    _prevMousePosition = pos;
}
