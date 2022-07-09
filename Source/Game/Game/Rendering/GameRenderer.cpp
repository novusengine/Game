#include "GameRenderer.h"
#include "UIRenderer.h"

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

GameRenderer::GameRenderer()
{
	_window = new Window();
	_window->Init(Renderer::Settings::SCREEN_WIDTH, Renderer::Settings::SCREEN_HEIGHT);

	_renderer = new Renderer::RendererVK();

    std::string shaderSourcePath = SHADER_SOURCE_DIR;
    _renderer->SetShaderSourceDirectory(shaderSourcePath);

    InitImgui();
	_renderer->InitDebug();
	_renderer->InitWindow(_window);

    _uiRenderer = new UIRenderer(_renderer);

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
    // Reset the memory in the frameAllocator
    _frameAllocator->Reset();
}

void GameRenderer::Render()
{
    // If the window is minimized we want to pause rendering
    if (_window->IsMinimized())
        return;

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
            Renderer::RenderPassMutableResource visibilityBuffer;
            Renderer::RenderPassMutableResource sceneColor;
            Renderer::RenderPassMutableResource finalColor;
            Renderer::RenderPassMutableResource transparencyWeights;
            Renderer::RenderPassMutableResource depth;
        };

        renderGraph.AddPass<StartFramePassData>("StartFramePass",
            [=](StartFramePassData& data, Renderer::RenderGraphBuilder& builder) // Setup
            {
                data.finalColor = builder.Write(_resources.finalColor, Renderer::RenderGraphBuilder::WriteMode::RENDERTARGET, Renderer::RenderGraphBuilder::LoadMode::CLEAR);
                
                return true; // Return true from setup to enable this pass, return false to disable it
            },
            [&](StartFramePassData& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList) // Execute
            {
                GPU_SCOPED_PROFILER_ZONE(commandList, StartFramePass);
                commandList.MarkFrameStart(_frameIndex);

                // Set viewport
                vec2 renderSize = _renderer->GetRenderSize();
                commandList.SetViewport(0, 0, renderSize.x, renderSize.y, 0.0f, 1.0f);
                commandList.SetScissorRect(0, static_cast<u32>(renderSize.x), 0, static_cast<u32>(renderSize.y));
            });
    }

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
