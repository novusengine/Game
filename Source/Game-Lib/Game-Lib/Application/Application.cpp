#include "Application.h"

#include "Game-Lib/ECS/Scheduler.h"
#include "Game-Lib/ECS/Components/Events.h"
#include "Game-Lib/ECS/Singletons/EngineStats.h"
#include "Game-Lib/ECS/Singletons/NetworkState.h"
#include "Game-Lib/ECS/Singletons/RenderState.h"
#include "Game-Lib/ECS/Singletons/Database/CameraSaveSingleton.h"
#include "Game-Lib/ECS/Singletons/Database/ClientDBSingleton.h"
#include "Game-Lib/ECS/Singletons/Database/MapSingleton.h"
#include "Game-Lib/ECS/Util/EventUtil.h"
#include "Game-Lib/ECS/Util/Database/CameraUtil.h"
#include "Game-Lib/ECS/Util/Database/CursorUtil.h"
#include "Game-Lib/ECS/Util/Database/FactionUtil.h"
#include "Game-Lib/ECS/Util/Database/IconUtil.h"
#include "Game-Lib/ECS/Util/Database/ItemUtil.h"
#include "Game-Lib/ECS/Util/Database/LightUtil.h"
#include "Game-Lib/ECS/Util/Database/MapUtil.h"
#include "Game-Lib/ECS/Util/Database/SpellUtil.h"
#include "Game-Lib/ECS/Util/Database/TextureUtil.h"
#include "Game-Lib/ECS/Util/Database/UnitCustomizationUtil.h"
#include "Game-Lib/Editor/EditorHandler.h"
#include "Game-Lib/Gameplay/GameConsole/GameConsole.h"
#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Rendering/Model/ModelLoader.h"
#include "Game-Lib/Rendering/Terrain/TerrainLoader.h"
#include "Game-Lib/Scripting/Handlers/GlobalHandler.h"
#include "Game-Lib/Scripting/Handlers/EventHandler.h"
#include "Game-Lib/Scripting/Handlers/DatabaseHandler.h"
#include "Game-Lib/Scripting/Handlers/UIHandler.h"
#include "Game-Lib/Scripting/Handlers/GameHandler.h"
#include "Game-Lib/Scripting/Handlers/UnitHandler.h"
#include "Game-Lib/Scripting/Handlers/TimeHandler.h"
#include "Game-Lib/Scripting/Handlers/CameraHandler.h"
#include "Game-Lib/Scripting/Handlers/MapHandler.h"
#include "Game-Lib/Scripting/Handlers/SceneHandler.h"
#include "Game-Lib/Scripting/Handlers/EditorToolHandler.h"
#include "Game-Lib/Scripting/Handlers/AssetHandler.h"
#include "Game-Lib/Util/AssetPath.h"
#include "Game-Lib/Util/AssetWriter.h"
#include "Game-Lib/Util/ClientDBUtil.h"
#include "Game-Lib/Util/JoltMemoryTelemetry.h"
#include "Game-Lib/Util/ServiceLocator.h"
#include "Game-Lib/Util/TextureUtil.h"

#include <Base/Types.h>
#include <Base/CVarSystem/CVarSystem.h>
#include <Base/Memory/Bytebuffer.h>
#include <Base/Util/Timer.h>
#include <Base/Util/JsonUtils.h>
#include <Base/Util/DebugHandler.h>
#include <Base/Util/CPUInfo.h>

#include <Filesystem/PactStorage.h>

#include <MetaGen/Game/Lua/Lua.h>

#include <Network/Client.h>

#include <Scripting/LuaManager.h>
#include <Scripting/Zenith.h>

#include <enkiTS/TaskScheduler.h>
#include <entt/entt.hpp>
#include <imgui/backends/imgui_impl_vulkan.h>
#include <imgui/backends/imgui_impl_glfw.h>
#include <imgui/imguizmo/ImGuizmo.h>
#include <Jolt/Jolt.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/RegisterTypes.h>
#include <tracy/Tracy.hpp>

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <xxhash/xxhash32.h>

AutoCVar_Int CVAR_FramerateLimit(CVarCategory::Client, "framerateLimit", "enable framerate limit", 1, CVarFlags::EditCheckbox);
AutoCVar_Int CVAR_IOFramerateLimit(CVarCategory::Client, "ioFramerateLimit", "enable framerate limit", 1, CVarFlags::EditCheckbox);
AutoCVar_Int CVAR_FramerateLimitTarget(CVarCategory::Client, "framerateLimitTarget", "target framerate while limited", 60);
AutoCVar_Int CVAR_IOFramerateLimitTarget(CVarCategory::Client, "ioFramerateLimitTarget", "target io framerate while limited", 5);
AutoCVar_Int CVAR_CpuReportDetailLevel(CVarCategory::Client, "cpuReportDetailLevel", "Sets the detail level for CPU info printing on startup. (0 = No Output, 1 = CPU Name, 2 = CPU Name + Feature Support)", 1);
AutoCVar_Int CVAR_ApplicationNumThreads(CVarCategory::Client, "numThreads", "number of threads used for multi threading, 0 = number of hardware threads", 0);
AutoCVar_Int CVAR_ClientDBSaveMethod(CVarCategory::Client, "clientDBSaveMethod", "specifies when clientDBs are saved. (0 = Immediately, 1 = Every x Seconds, 2 = On Shutdown, 3+ = Disabled, default is 1)", 1);
AutoCVar_Float CVAR_ClientDBSaveTimer(CVarCategory::Client, "clientDBSaveTimer", "specifies how often clientDBs are saved when using save method 1 (Specified in seconds, default is 5 seconds)", 5.0f);
AutoCVar_String CVAR_ImguiTheme(CVarCategory::Client, "imguiTheme", "specifies the current imgui theme", "Blue Teal", CVarFlags::Hidden);
AutoCVar_Int CVAR_DeveloperMode(CVarCategory::Client, "developerMode", "enables developer-only Luau APIs (Editor, Time) and dev-tool scripts under Resources/Scripts/Editor", 1, CVarFlags::EditCheckbox);
AutoCVar_Int CVAR_PhysicsLogJoltTraces(CVarCategory::Client | CVarCategory::Physics, "logJoltTraces", "logs trace output emitted by Jolt Physics", 0, CVarFlags::EditCheckbox);

namespace
{
    std::atomic_bool JoltTraceLoggingEnabled = false;

    void JoltTrace(const char* format, ...)
    {
        if (!JoltTraceLoggingEnabled.load(std::memory_order_relaxed))
            return;

        char message[4096];
        va_list args;
        va_start(args, format);
        const i32 length = std::vsnprintf(message, sizeof(message), format, args);
        va_end(args);

        if (length <= 0)
            return;

        const i32 messageLength = Math::Min(length, static_cast<i32>(sizeof(message) - 1));
        message[messageLength] = '\0';

        NC_LOG_INFO("Jolt : {0}", message);
    }
}

Application::Application() : _messagesInbound(256), _messagesOutbound(256)
{
    ServiceLocator::SetApplication(this);
}
Application::~Application()
{
    delete _gameRenderer;
    delete _editorHandler;
    delete _ecsScheduler;
    delete _taskScheduler;
    delete _assetWriter;
}

void Application::Start(bool startInSeparateThread)
{
    if (_isRunning)
        return;

    if (startInSeparateThread)
    {
        _isRunning = true;

        std::thread applicationThread = std::thread(&Application::Run, this);
        applicationThread.detach();
    }
    else
    {
        _isRunning = Init();
    }
}

void Application::Stop()
{
    if (!_isRunning.exchange(false))
        return;

    NC_LOG_INFO("Application : Shutdown Initiated");
    Cleanup();
    NC_LOG_INFO("Application : Shutdown Complete");

    MessageOutbound message(MessageOutbound::Type::Exit);
    _messagesOutbound.enqueue(message);
}

void Application::RequestExit()
{
    _exitRequested = true;
}

void Application::Cleanup()
{
    if (EnttRegistries* enttRegistries = ServiceLocator::GetEnttRegistries())
    {
        entt::registry* registry = enttRegistries->gameRegistry;
        if (registry)
        {
            entt::registry::context& ctx = registry->ctx();
            if (auto* networkState = ctx.find<ECS::Singletons::NetworkState>())
            {
                if (networkState->client && networkState->client->IsConnected())
                    networkState->client->Stop();

                networkState->asioContext.stop();
                if (networkState->asioThread.joinable())
                    networkState->asioThread.join();
            }
        }

        i32 clientDBSaveMethod = CVAR_ClientDBSaveMethod.Get();
        if (clientDBSaveMethod == 2
            && _assetWriter
            && enttRegistries->dbRegistry
            && enttRegistries->dbRegistry->ctx().contains<ECS::Singletons::ClientDBSingleton>())
            SaveCDB();
    }

    if (_gameRenderer)
    {
        if (_gameRenderer->GetTerrainLoader())
            _gameRenderer->GetTerrainLoader()->Shutdown();

        if (_gameRenderer->GetModelLoader())
            _gameRenderer->GetModelLoader()->Shutdown();
    }

    auto* pactStorage = ServiceLocator::GetPactStorage();
    if (!pactStorage->Shutdown())
        NC_LOG_ERROR("Application : PACT shutdown failed because file handles are still alive");
}

void Application::PassMessage(MessageInbound& message)
{
    _messagesInbound.enqueue(message);
}

bool Application::TryGetMessageOutbound(MessageOutbound& message)
{
    bool messageFound = _messagesOutbound.try_dequeue(message);
    return messageFound;
}

void Application::Run()
{
    tracy::SetThreadName("Application Thread");

    if (Init())
    {
        Timer timer;
        Timer updateTimer;
        Timer renderTimer;

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        entt::registry::context& ctx = registry->ctx();

        auto& renderState = ctx.get<ECS::Singletons::RenderState>();
        auto& engineStats = ctx.get<ECS::Singletons::EngineStats>();

        ECS::Singletons::FrameTimes timings;
        while (!_exitRequested)
        {
            ZoneScoped;
            f32 deltaTime = timer.GetDeltaTime();
            timer.Tick();

            timings.deltaTimeS = deltaTime;

            updateTimer.Reset();
            renderState.frameNumber++;

            bool shouldExit = !_gameRenderer->UpdateWindow(deltaTime);
            if (shouldExit)
                break;

            if (!Tick(deltaTime))
                break;

            timings.simulationFrameTimeS = updateTimer.GetLifeTime();
            renderTimer.Reset();

            ServiceLocator::GetGameConsole()->Render(deltaTime);

            _editorHandler->DrawImGuiMenuBar(deltaTime);

            f32 timeSpentWaiting = 0.0f;
            if (!Render(deltaTime, timeSpentWaiting))
                break;

            {
                ZoneScopedN("TimeQueries");
                timings.renderFrameTimeS = renderTimer.GetLifeTime() - timeSpentWaiting;
                timings.renderWaitTimeS = timeSpentWaiting;

                // Get last GPU Frame time
                Renderer::Renderer* renderer = _gameRenderer->GetRenderer();

                const std::vector<Renderer::TimeQueryID> frameTimeQueries = renderer->GetFrameTimeQueries();
                if (frameTimeQueries.size() > 0)
                {
                    for (Renderer::TimeQueryID timeQueryID : frameTimeQueries)
                    {
                        const std::string& name = renderer->GetTimeQueryName(timeQueryID);
                        f32 durationMS = renderer->GetLastTimeQueryDuration(timeQueryID);

                        engineStats.AddNamedStat(name, durationMS);
                    }

                    Renderer::TimeQueryID totalTimeQuery = frameTimeQueries[0];
                    timings.gpuFrameTimeMS = renderer->GetLastTimeQueryDuration(totalTimeQuery);
                }
                else
                {
                    timings.gpuFrameTimeMS = 0;
                }

                engineStats.AddTimings(timings.deltaTimeS, timings.simulationFrameTimeS, timings.renderFrameTimeS, timings.renderWaitTimeS, timings.gpuFrameTimeMS);
            }

            {
                ZoneScopedN("Framerate Limit");
                bool limitFrameRate = CVAR_FramerateLimit.Get() == 1;
                if (limitFrameRate)
                {
                    f32 targetFramerate = Math::Max(static_cast<f32>(CVAR_FramerateLimitTarget.Get()), 10.0f);
                    f32 targetDelta = 1.0f / targetFramerate;

                    for (deltaTime = timer.GetDeltaTime(); deltaTime < targetDelta; deltaTime = timer.GetDeltaTime())
                    {
                        std::this_thread::yield();
                    }
                }
            }

            FrameMark;
        }
    }

    Stop();
}

bool Application::Init()
{
    _registries.gameRegistry = new entt::registry();
    _registries.uiRegistry = new entt::registry();
    _registries.dbRegistry = new entt::registry();
    _registries.eventIncomingRegistry = new entt::registry();
    _registries.eventOutgoingRegistry = new entt::registry();
    ServiceLocator::SetEnttRegistries(&_registries);

    std::filesystem::path currentPath = std::filesystem::current_path();
    std::filesystem::path pactPath = currentPath / "data/pact";
    std::filesystem::path customOverlayPath = pactPath / "data/custom";
    std::filesystem::path stagingOverlayPath = pactPath / "data/staging";

    NC_LOG_INFO("Initializing PACT");
    auto pactStorage = new PACT::PactStorage();
    ServiceLocator::SetPactStorage(pactStorage);

    if (!pactStorage->Open(pactPath, PACT::PactOpenOptions{ .FallbackToInit = 1 }))
        return false;

    pactStorage->MountAll();

    PACT::PactManifestHandle customOverlayHandle = pactStorage->AddOverlay(customOverlayPath, true, std::numeric_limits<u32>::max() - 1);
    if (customOverlayHandle == PACT::MANIFEST_INVALID_ID)
        return false;

    PACT::PactManifestHandle stagingOverlayHandle = pactStorage->AddOverlay(stagingOverlayPath, true, std::numeric_limits<u32>::max());
    if (stagingOverlayHandle == PACT::MANIFEST_INVALID_ID)
        return false;

    NC_LOG_INFO("Initialized PACT");

    _assetWriter = new Util::AssetWriter();
    if (!_assetWriter->Init(Util::AssetWriterConfig
    {
        .diskRoot = currentPath / "Data",
        .pactOverlayRoot = stagingOverlayPath,
        .pactStorage = pactStorage,
        .pactOverlayHandle = stagingOverlayHandle
    }))
    {
        return false;
    }

    ServiceLocator::SetAssetWriter(_assetWriter);

    // Setup CVar Config
    {
        NC_LOG_INFO("Current Path : {}", currentPath.string());
        std::filesystem::create_directories("Data/Config");

        nlohmann::ordered_json fallback;
        fallback["version"] = JsonUtils::CVAR_VERSION;
        if (JsonUtils::LoadFromPathOrCreate(_cvarJson, fallback, "Data/Config/CVar.json"))
        {
            JsonUtils::VerifyCVarsOrFallback(_cvarJson, fallback);
            JsonUtils::LoadCVarsFromJson(_cvarJson);
            JsonUtils::SaveCVarsToJson(_cvarJson);
            JsonUtils::SaveToPath(_cvarJson, "Data/Config/CVar.json");
        }
    }

    // Print CPU info
    CPUInfo cpuInfo = CPUInfo::Get();
    cpuInfo.Print(CVAR_CpuReportDetailLevel.Get());

    _taskScheduler = new enki::TaskScheduler();

    i32 numThreads = CVAR_ApplicationNumThreads.Get();
    if (numThreads <= 0)
    {
        _taskScheduler->Initialize();
    }
    else
    {
        _taskScheduler->Initialize(numThreads);
    }

    ServiceLocator::SetTaskScheduler(_taskScheduler);

    _inputManager = new InputManager();
    ServiceLocator::SetInputManager(_inputManager);

    constexpr u32 imguiKeybindGroupPriority = std::numeric_limits<u32>().max();
    KeybindGroup* imguiGroup = _inputManager->CreateKeybindGroup("Imgui", imguiKeybindGroupPriority);
    imguiGroup->SetActive(true);

    JoltTraceLoggingEnabled.store(CVAR_PhysicsLogJoltTraces.Get() != 0, std::memory_order_relaxed);
    CVAR_PhysicsLogJoltTraces.AddOnValueChanged([](const i32& value)
    {
        JoltTraceLoggingEnabled.store(value != 0, std::memory_order_relaxed);
    });
    JPH::Trace = JoltTrace;
    Util::JoltMemoryTelemetry::RegisterAllocator();
    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();

    Util::Texture::DiscoverAll();
    Util::ClientDB::DiscoverAll();

    _gameRenderer = new GameRenderer(_inputManager);

    NC_LOG_INFO("EditorHandler : Initializing");
    _editorHandler = new Editor::EditorHandler();
    ServiceLocator::SetEditorHandler(_editorHandler);
    NC_LOG_INFO("EditorHandler : Initialized");

    _ecsScheduler = new ECS::Scheduler();
    _ecsScheduler->Init(_registries);

    ServiceLocator::SetGameConsole(new GameConsole());

    // Initialize Databases
    DatabaseReload();

    // Init Lua Manager
    {
        _luaManager = new Scripting::LuaManager();
        ServiceLocator::SetLuaManager(_luaManager);

        _luaManager->PrepareToAddLuaHandlers((Scripting::LuaHandlerID)MetaGen::Game::Lua::LuaHandlerTypeEnum::Count);
        _luaManager->SetLuaHandler((Scripting::LuaHandlerID)MetaGen::Game::Lua::LuaHandlerTypeEnum::Global, new Scripting::GlobalHandler());
        _luaManager->SetLuaHandler((Scripting::LuaHandlerID)MetaGen::Game::Lua::LuaHandlerTypeEnum::Event, new Scripting::EventHandler());
        _luaManager->SetLuaHandler((Scripting::LuaHandlerID)MetaGen::Game::Lua::LuaHandlerTypeEnum::Database, new Scripting::Database::DatabaseHandler());
        _luaManager->SetLuaHandler((Scripting::LuaHandlerID)MetaGen::Game::Lua::LuaHandlerTypeEnum::UI, new Scripting::UI::UIHandler());
        _luaManager->SetLuaHandler((Scripting::LuaHandlerID)MetaGen::Game::Lua::LuaHandlerTypeEnum::Game, new Scripting::Game::GameHandler());
        _luaManager->SetLuaHandler((Scripting::LuaHandlerID)MetaGen::Game::Lua::LuaHandlerTypeEnum::Unit, new Scripting::Unit::UnitHandler());
        _luaManager->SetLuaHandler((Scripting::LuaHandlerID)MetaGen::Game::Lua::LuaHandlerTypeEnum::Time, new Scripting::Time::TimeHandler());
        _luaManager->SetLuaHandler((Scripting::LuaHandlerID)MetaGen::Game::Lua::LuaHandlerTypeEnum::Camera, new Scripting::Camera::CameraHandler());
        _luaManager->SetLuaHandler((Scripting::LuaHandlerID)MetaGen::Game::Lua::LuaHandlerTypeEnum::Map, new Scripting::Map::MapHandler());
        _luaManager->SetLuaHandler((Scripting::LuaHandlerID)MetaGen::Game::Lua::LuaHandlerTypeEnum::Scene, new Scripting::Scene::SceneHandler());
        _luaManager->SetLuaHandler((Scripting::LuaHandlerID)MetaGen::Game::Lua::LuaHandlerTypeEnum::Editor, new Scripting::Editor::EditorToolHandler());
        _luaManager->SetLuaHandler((Scripting::LuaHandlerID)MetaGen::Game::Lua::LuaHandlerTypeEnum::Asset, new Scripting::Asset::AssetHandler());

        auto globalKey = Scripting::ZenithInfoKey::MakeGlobal(0, 0);
        _luaManager->GetZenithStateManager().Add(globalKey);

        _luaManager->SetDeveloperMode(CVAR_DeveloperMode.Get() != 0);

        CVarSystem::Get()->AddOnIntValueChanged(CVarCategory::Client, "developerMode"_h, 
            [this](const i32& value)
            {
                _luaManager->SetDeveloperMode(value != 0);
            });

        _luaManager->Init();
    }

    return true;
}

bool Application::Tick(f32 deltaTime)
{
    ZoneScoped;
    // Imgui New Frame
    {
        ZoneScopedN("_editorHandler->NewFrame");
        _editorHandler->NewFrame();
    }
    {
        ZoneScopedN("ImGui_ImplVulkan_NewFrame");
        ImGui_ImplVulkan_NewFrame();
    }
    {
        ZoneScopedN("ImGui_ImplGlfw_NewFrame");
        ImGui_ImplGlfw_NewFrame();
    }
    {
        ZoneScopedN("ImGui::NewFrame");
        ImGui::NewFrame();
    }
    {
        ZoneScopedN("ImGuizmo::BeginFrame");
        ImGuizmo::BeginFrame();
    }

    _editorHandler->BeginImGui();
    _editorHandler->BeginEditor();

    MessageInbound message;
    while (_messagesInbound.try_dequeue(message))
    {
        assert(message.type != MessageInbound::Type::Invalid);

        switch (message.type)
        {
            case MessageInbound::Type::Print:
            {
                NC_LOG_INFO("{}", message.data);
                break;
            }

            case MessageInbound::Type::Ping:
            {
                MessageOutbound pongMessage(MessageOutbound::Type::Pong);
                _messagesOutbound.enqueue(pongMessage);

                NC_LOG_INFO("Main Thread -> Application Thread : Ping");
                break;
            }

            case MessageInbound::Type::DoString:
            {
                auto key = Scripting::ZenithInfoKey::MakeGlobal(0, 0);
                Scripting::Zenith* zenith = _luaManager->GetZenithStateManager().Get(key);

                if (!zenith || !_luaManager->DoString(zenith, message.data))
                {
                    NC_LOG_ERROR("Failed to run Lua DoString");
                }
                
                break;
            }

            case MessageInbound::Type::ReloadScripts:
            {
                ServiceLocator::GetLuaManager()->SetDirty();
                break;
            }

            case MessageInbound::Type::RefreshDB:
            {
                ECS::Util::EventUtil::PushEvent(ECS::Components::DatabaseReloadEvent{});
                break;
            }

            case MessageInbound::Type::Exit:
                return false;

            default: break;
        }
    }

    ECS::Util::EventUtil::OnEvent<ECS::Components::DatabaseReloadEvent>([&]()
    {
        DatabaseReload();
    });

    _ecsScheduler->Update(_registries, deltaTime);

    // Handle Double Buffered Event Swap
    {
        _registries.eventIncomingRegistry->clear();

        entt::registry* temp = _registries.eventIncomingRegistry;
        _registries.eventIncomingRegistry = _registries.eventOutgoingRegistry;
        _registries.eventOutgoingRegistry = temp;
    }

    _editorHandler->Update(deltaTime);

    _gameRenderer->UpdateRenderers(deltaTime);

    if (CVarSystem::Get()->IsDirty())
    {
        JsonUtils::SaveCVarsToJson(_cvarJson);
        JsonUtils::SaveToPath(_cvarJson, "Data/Config/CVar.json");

        CVarSystem::Get()->ClearDirty();
    }

    i32 clientDBSaveMethod = CVAR_ClientDBSaveMethod.Get();
    if (clientDBSaveMethod == 0)
    {
        SaveCDB();
    }
    else if (clientDBSaveMethod == 1)
    {
        static f32 saveClientDBTimer = 0.0f;
        f32 maxClientDBTimer = CVAR_ClientDBSaveTimer.GetFloat();

        saveClientDBTimer += deltaTime;

        if (saveClientDBTimer >= maxClientDBTimer)
        {
            SaveCDB();

            saveClientDBTimer -= maxClientDBTimer;
        }
    }

    return true;
}

bool Application::Render(f32 deltaTime, f32& timeSpentWaiting)
{
    ZoneScoped;
    timeSpentWaiting = _gameRenderer->Render();

    {
        ZoneScopedN("ImGui::UpdatePlatformWindows");
        ImGui::UpdatePlatformWindows();
    }
    {
        ZoneScopedN("ImGui::RenderPlatformWindowsDefault");
        ImGui::RenderPlatformWindowsDefault();
    }

    return true;
}

void Application::DatabaseReload()
{
    NC_LOG_INFO("Application : Database Reload Init");
    ECSUtil::Icon::Refresh();
    ECSUtil::Camera::Refresh();
    ECSUtil::Cursor::Refresh();
    ECSUtil::Map::Refresh();
    ECSUtil::Light::Refresh();
    ECSUtil::Texture::Refresh();
    ECSUtil::Spell::Refresh();
    ECSUtil::Item::Refresh();
    ECSUtil::UnitCustomization::Refresh();
    ECSUtil::Faction::Refresh();
    NC_LOG_INFO("Application : Database Reload Finish");
}

void Application::SaveCDB()
{
    entt::registry::context& ctx = _registries.dbRegistry->ctx();
    auto& clientDBSingleton = ctx.get<ECS::Singletons::ClientDBSingleton>();
    Util::AssetWriter* assetWriter = ServiceLocator::GetAssetWriter();

    clientDBSingleton.Each([&clientDBSingleton, assetWriter](ClientDBHash dbHash, ClientDB::Data* db)
    {
        if (!db->IsDirty())
            return;

        const std::string& dbName = clientDBSingleton.GetDBName(dbHash);
        std::string virtualPath = Util::AssetPath::Create("clientdb", dbName + ClientDB::FILE_EXTENSION);

        std::shared_ptr<Bytebuffer> buffer = Bytebuffer::BorrowRuntime(db->GetSerializedSize());
        if (!db->Save(buffer))
        {
            NC_LOG_ERROR("Application : Failed to serialize ClientDB \"{0}\"", dbName);
            return;
        }

        if (!assetWriter->WriteBytes(virtualPath, *buffer, Util::AssetWriteTarget::PactOverlay))
        {
            NC_LOG_ERROR("Application : Failed to save ClientDB \"{0}\" to PACT staging overlay", dbName);
            return;
        }

        db->ClearDirty();
    });
}
