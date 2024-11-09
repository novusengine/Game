#include "Application.h"

#include "Game-Lib/Animation/AnimationSystem.h"
#include "Game-Lib/ECS/Scheduler.h"
#include "Game-Lib/ECS/Singletons/CameraSaveDB.h"
#include "Game-Lib/ECS/Singletons/ClientDBCollection.h"
#include "Game-Lib/ECS/Singletons/EngineStats.h"
#include "Game-Lib/ECS/Singletons/MapDB.h"
#include "Game-Lib/ECS/Singletons/NetworkState.h"
#include "Game-Lib/ECS/Singletons/RenderState.h"
#include "Game-Lib/ECS/Util/CharSectionUtil.h"
#include "Game-Lib/ECS/Util/MapUtil.h"
#include "Game-Lib/Editor/EditorHandler.h"
#include "Game-Lib/Gameplay/GameConsole/GameConsole.h"
#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Scripting/LuaManager.h"
#include "Game-Lib/Util/ServiceLocator.h"
#include "Game-Lib/Loaders/ClientDB/ClientDBLoader.h"
#include "Game-Lib/Loaders/Texture/TextureLoader.h"
#include "Game-Lib/Scripting/LuaManager.h"
#include "Game-Lib/Scripting/Systems/LuaSystemBase.h"

#include <Base/Types.h>
#include <Base/CVarSystem/CVarSystem.h>
#include <Base/Util/Timer.h>
#include <Base/Util/JsonUtils.h>
#include <Base/Util/DebugHandler.h>
#include <Base/Util/CPUInfo.h>

#include <Network/Client.h>

#include <enkiTS/TaskScheduler.h>
#include <entt/entt.hpp>
#include <imgui/backends/imgui_impl_vulkan.h>
#include <imgui/backends/imgui_impl_glfw.h>
#include <imgui/imguizmo/ImGuizmo.h>
#include <Jolt/Jolt.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/RegisterTypes.h>
#include <tracy/Tracy.hpp>

#include <filesystem>

AutoCVar_Int CVAR_FramerateLimit(CVarCategory::Client, "framerateLimit", "enable framerate limit", 1, CVarFlags::EditCheckbox);
AutoCVar_Int CVAR_FramerateLimitTarget(CVarCategory::Client, "framerateLimitTarget", "target framerate while limited", 60);
AutoCVar_Int CVAR_CpuReportDetailLevel(CVarCategory::Client, "cpuReportDetailLevel", "Sets the detail level for CPU info printing on startup. (0 = No Output, 1 = CPU Name, 2 = CPU Name + Feature Support)", 1);
AutoCVar_Int CVAR_ApplicationNumThreads(CVarCategory::Client, "numThreads", "number of threads used for multi threading, 0 = number of hardware threads", 0);
AutoCVar_Int CVAR_ClientDBSaveMethod(CVarCategory::Client, "clientDBSaveMethod", "specifies when clientDBs are saved. (0 = Immediately, 1 = Every x Seconds, 2 = On Shutdown, 3+ = Disabled, default is 1)", 1);
AutoCVar_Float CVAR_ClientDBSaveTimer(CVarCategory::Client, "clientDBSaveTimer", "specifies how often clientDBs are saved when using save method 1 (Specified in seconds, default is 5 seconds)", 5.0f);

Application::Application() : _messagesInbound(256), _messagesOutbound(256) { }
Application::~Application()
{
    delete _gameRenderer;
    delete _editorHandler;
    delete _ecsScheduler;
    delete _taskScheduler;
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
    if (!_isRunning)
        return;

    NC_LOG_INFO("Application : Shutdown Initiated");
    Cleanup();
    NC_LOG_INFO("Application : Shutdown Complete");

    MessageOutbound message(MessageOutbound::Type::Exit);
    _messagesOutbound.enqueue(message);
}

void Application::Cleanup()
{
    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
    entt::registry::context& ctx = registry->ctx();
    auto& networkState = ctx.get<ECS::Singletons::NetworkState>();
    if (networkState.client && networkState.client->IsConnected())
    {
        networkState.client->Stop();
    }

    i32 clientDBSaveMethod = CVAR_ClientDBSaveMethod.Get();
    if (clientDBSaveMethod == 2)
    {
        SaveCDB();
    }
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
        while (true)
        {
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

            timings.renderFrameTimeS = renderTimer.GetLifeTime() - timeSpentWaiting;
            timings.renderWaitTimeS = timeSpentWaiting;

            // Get last GPU Frame time
            Renderer::Renderer* renderer = _gameRenderer->GetRenderer();

            const std::vector<Renderer::TimeQueryID> frameTimeQueries = renderer->GetFrameTimeQueries();

            for (Renderer::TimeQueryID timeQueryID : frameTimeQueries)
            {
                const std::string& name = renderer->GetTimeQueryName(timeQueryID);
                f32 durationMS = renderer->GetLastTimeQueryDuration(timeQueryID);

                engineStats.AddNamedStat(name, durationMS);
            }

            Renderer::TimeQueryID totalTimeQuery = frameTimeQueries[0];
            timings.gpuFrameTimeMS = renderer->GetLastTimeQueryDuration(totalTimeQuery);

            engineStats.AddTimings(timings.deltaTimeS, timings.simulationFrameTimeS, timings.renderFrameTimeS, timings.renderWaitTimeS, timings.gpuFrameTimeMS);

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

            FrameMark;
        }
    }

    Stop();
}

bool Application::Init()
{
    // Setup CVar Config
    {
        std::filesystem::path currentPath = std::filesystem::current_path();
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

    _registries.gameRegistry = new entt::registry();
    _registries.uiRegistry = new entt::registry();
    _registries.eventIncomingRegistry = new entt::registry();
    _registries.eventOutgoingRegistry = new entt::registry();
    ServiceLocator::SetEnttRegistries(&_registries);

    _inputManager = new InputManager();
    ServiceLocator::SetInputManager(_inputManager);

    constexpr u32 imguiKeybindGroupPriority = std::numeric_limits<u32>().max();
    KeybindGroup* imguiGroup = _inputManager->CreateKeybindGroup("Imgui", imguiKeybindGroupPriority);
    imguiGroup->SetActive(true);

    JPH::RegisterDefaultAllocator();
    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();

    TextureLoader::Init();
    ClientDBLoader::Init();

    _gameRenderer = new GameRenderer(_inputManager);
    _editorHandler = new Editor::EditorHandler();
    ServiceLocator::SetEditorHandler(_editorHandler);

    _ecsScheduler = new ECS::Scheduler();
    _ecsScheduler->Init(_registries);

    ServiceLocator::SetGameConsole(new GameConsole());

    _luaManager = new Scripting::LuaManager();
    ServiceLocator::SetLuaManager(_luaManager);
    _luaManager->Init();

    // Init Singletons for CDB
    {
        using namespace ECS::Singletons;

        entt::registry::context& ctx = _registries.gameRegistry->ctx();
        auto& clientDBCollection = ctx.get<ClientDBCollection>();

        clientDBCollection.Register<ClientDB::Definitions::Map>(ClientDBHash::Map, "Map");
        clientDBCollection.Register<ClientDB::Definitions::LiquidObject>(ClientDBHash::LiquidObject, "LiquidObject");
        clientDBCollection.Register<ClientDB::Definitions::LiquidType>(ClientDBHash::LiquidType, "LiquidType");
        clientDBCollection.Register<ClientDB::Definitions::LiquidMaterial>(ClientDBHash::LiquidMaterial, "LiquidMaterial");
        clientDBCollection.Register<ClientDB::Definitions::CinematicCamera>(ClientDBHash::CinematicCamera, "CinematicCamera");
        clientDBCollection.Register<ClientDB::Definitions::CinematicSequence>(ClientDBHash::CinematicSequence, "CinematicSequence");
        clientDBCollection.Register<ClientDB::Definitions::CameraSave>(ClientDBHash::CameraSave, "CameraSave");
        clientDBCollection.Register<ClientDB::Definitions::Cursor>(ClientDBHash::Cursor, "Cursor");
        clientDBCollection.Register<ClientDB::Definitions::AnimationData>(ClientDBHash::AnimationData, "AnimationData");
        clientDBCollection.Register<ClientDB::Definitions::CreatureDisplayInfo>(ClientDBHash::CreatureDisplayInfo, "CreatureDisplayInfo");
        clientDBCollection.Register<ClientDB::Definitions::CreatureDisplayInfoExtra>(ClientDBHash::CreatureDisplayInfoExtra, "CreatureDisplayInfoExtra");
        clientDBCollection.Register<ClientDB::Definitions::CreatureModelData>(ClientDBHash::CreatureModelData, "CreatureModelData");
        clientDBCollection.Register<ClientDB::Definitions::CharSection>(ClientDBHash::CharSection, "CharSection");

        _registries.gameRegistry->ctx().emplace<MapDB>();
        ECS::Util::Map::Refresh();

        auto& cameraSaveDB = _registries.gameRegistry->ctx().emplace<CameraSaveDB>();
        cameraSaveDB.Refresh();

        // Setup Cursors
        {
            auto* cursors = clientDBCollection.Get<ClientDB::Definitions::Cursor>(ClientDBHash::Cursor);

            if (cursors->Count() == 0)
            {
                struct CursorEntry
                {
                public:
                    std::string name;
                    std::string texture;
                };
            
                std::vector<CursorEntry> cursorEntries =
                {
                    { "architect", "Data/Texture/interface/cursor/architect.dds" },
                    { "argusteleporter", "Data/Texture/interface/cursor/argusteleporter.dds" },
                    { "attack", "Data/Texture/interface/cursor/attack.dds" },
                    { "unableattack", "Data/Texture/interface/cursor/unableattack.dds" },
                    { "buy", "Data/Texture/interface/cursor/buy.dds" },
                    { "unablebuy", "Data/Texture/interface/cursor/unablebuy.dds" },
                    { "cast", "Data/Texture/interface/cursor/cast.dds" },
                    { "unablecast", "Data/Texture/interface/cursor/unablecast.dds" },
                    { "crosshairs", "Data/Texture/interface/cursor/crosshairs.dds" },
                    { "unablecrosshairs", "Data/Texture/interface/cursor/unablecrosshairs.dds" },
                    { "directions", "Data/Texture/interface/cursor/directions.dds" },
                    { "unabledirections", "Data/Texture/interface/cursor/unabledirections.dds" },
                    { "driver", "Data/Texture/interface/cursor/driver.dds" },
                    { "engineerskin", "Data/Texture/interface/cursor/engineerskin.dds" },
                    { "unableengineerskin", "Data/Texture/interface/cursor/unableengineerskin.dds" },
                    { "gatherherbs", "Data/Texture/interface/cursor/gatherherbs.dds" },
                    { "unablegatherherbs", "Data/Texture/interface/cursor/unablegatherherbs.dds" },
                    { "gunner", "Data/Texture/interface/cursor/gunner.dds" },
                    { "unablegunner", "Data/Texture/interface/cursor/unablegunner.dds" },
                    { "innkeeper", "Data/Texture/interface/cursor/innkeeper.dds" },
                    { "unableinnkeeper", "Data/Texture/interface/cursor/unableinnkeeper.dds" },
                    { "inspect", "Data/Texture/interface/cursor/inspect.dds" },
                    { "unableinspect", "Data/Texture/interface/cursor/unableinspect.dds" },
                    { "interact", "Data/Texture/interface/cursor/interact.dds" },
                    { "unableinteract", "Data/Texture/interface/cursor/unableinteract.dds" },
                    { "lootall", "Data/Texture/interface/cursor/lootall.dds" },
                    { "unablelootall", "Data/Texture/interface/cursor/unablelootall.dds" },
                    { "mail", "Data/Texture/interface/cursor/mail.dds" },
                    { "unablemail", "Data/Texture/interface/cursor/unablemail.dds" },
                    { "mine", "Data/Texture/interface/cursor/mine.dds" },
                    { "unablemine", "Data/Texture/interface/cursor/unablemine.dds" },
                    { "missions", "Data/Texture/interface/cursor/missions.dds" },
                    { "unablemissions", "Data/Texture/interface/cursor/unablemissions.dds" },
                    { "openhand", "Data/Texture/interface/cursor/openhand.dds" },
                    { "unableopenhand", "Data/Texture/interface/cursor/unableopenhand.dds" },
                    { "openhandglow", "Data/Texture/interface/cursor/openhandglow.dds" },
                    { "unableopenhandglow", "Data/Texture/interface/cursor/unableopenhandglow.dds" },
                    { "picklock", "Data/Texture/interface/cursor/picklock.dds" },
                    { "unablepicklock", "Data/Texture/interface/cursor/unablepicklock.dds" },
                    { "unablepickup", "Data/Texture/interface/cursor/unablepickup.dds" },
                    { "point", "Data/Texture/interface/cursor/point.dds" },
                    { "unablepoint", "Data/Texture/interface/cursor/unablepoint.dds" },
                    { "pvp", "Data/Texture/interface/cursor/pvp.dds" },
                    { "unablepvp", "Data/Texture/interface/cursor/unablepvp.dds" },
                    { "quest", "Data/Texture/interface/cursor/quest.dds" },
                    { "unablequest", "Data/Texture/interface/cursor/unablequest.dds" },
                    { "questinteract", "Data/Texture/interface/cursor/questinteract.dds" },
                    { "unablequestinteract", "Data/Texture/interface/cursor/unablequestinteract.dds" },
                    { "questrepeatable", "Data/Texture/interface/cursor/questrepeatable.dds" },
                    { "unablequestrepeatable", "Data/Texture/interface/cursor/unablequestrepeatable.dds" },
                    { "questturnin", "Data/Texture/interface/cursor/questturnin.dds" },
                    { "unablequestturnin", "Data/Texture/interface/cursor/unablequestturnin.dds" },
                    { "reforge", "Data/Texture/interface/cursor/reforge.dds" },
                    { "unablereforge", "Data/Texture/interface/cursor/unablereforge.dds" },
                    { "repair", "Data/Texture/interface/cursor/repair.dds" },
                    { "unablerepair", "Data/Texture/interface/cursor/unablerepair.dds" },
                    { "repairnpc", "Data/Texture/interface/cursor/repairnpc.dds" },
                    { "unablerepairnpc", "Data/Texture/interface/cursor/unablerepairnpc.dds" },
                    { "skin", "Data/Texture/interface/cursor/skin.dds" },
                    { "unableskin", "Data/Texture/interface/cursor/unableskin.dds" },
                    { "speak", "Data/Texture/interface/cursor/speak.dds" },
                    { "unablespeak", "Data/Texture/interface/cursor/unablespeak.dds" },
                    { "taxi", "Data/Texture/interface/cursor/taxi.dds" },
                    { "unabletaxi", "Data/Texture/interface/cursor/unabletaxi.dds" },
                    { "trainer", "Data/Texture/interface/cursor/trainer.dds" },
                    { "unabletrainer", "Data/Texture/interface/cursor/unabletrainer.dds" },
                    { "transmogrify", "Data/Texture/interface/cursor/transmogrify.dds" },
                    { "unabletransmogrify", "Data/Texture/interface/cursor/unabletransmogrify.dds" },
                    { "move", "Data/Texture/interface/cursor/ui-cursor-move.dds" },
                    { "unablemove", "Data/Texture/interface/cursor/unableui-cursor-move.dds" }
                };
            
                for (const CursorEntry& cursorEntry : cursorEntries)
                {
                    ClientDB::Definitions::Cursor entry;
                    entry.name = cursorEntry.name;
                    entry.texturePath = cursorEntry.texture;
            
                    cursors->AddRow(entry);
                }
            
                cursors->SetDirty();
            }
        }

        ECS::Util::CharSection::RefreshData(*_registries.gameRegistry);
    }

    // Init AnimationSystem
    {
        // ModelRenderer is optional and nullptr can be passed in to run the AnimationSystem without it
        ModelRenderer* modelRenderer = _gameRenderer->GetModelRenderer();
        _animationSystem = new Animation::AnimationSystem();

        ServiceLocator::SetAnimationSystem(_animationSystem);
    }

    return true;
}

bool Application::Tick(f32 deltaTime)
{
    // Imgui New Frame
    {
        _editorHandler->NewFrame();
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
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
                if (!ServiceLocator::GetLuaManager()->DoString(message.data))
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

            case MessageInbound::Type::Exit:
                return false;

            default: break;
        }
    }

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
        static f32 saveClientDBTimer = 0.0f;
        f32 maxClientDBTimer = CVAR_ClientDBSaveTimer.GetFloat();

        saveClientDBTimer += deltaTime;

        if (saveClientDBTimer >= maxClientDBTimer)
        {
            SaveCDB();

            saveClientDBTimer -= maxClientDBTimer;
        }
    }
    else if (clientDBSaveMethod == 1)
    {
        SaveCDB();
    }

    return true;
}

bool Application::Render(f32 deltaTime, f32& timeSpentWaiting)
{
    timeSpentWaiting = _gameRenderer->Render();

    ImGui::UpdatePlatformWindows();
    ImGui::RenderPlatformWindowsDefault();

    return true;
}

void Application::SaveCDB()
{
    std::filesystem::path absolutePath = std::filesystem::absolute("Data/ClientDB").make_preferred();

    entt::registry::context& ctx = _registries.gameRegistry->ctx();
    auto& clientDBCollection = ctx.get<ECS::Singletons::ClientDBCollection>();

    for (auto& clientDB : clientDBCollection._dbs)
    {
        if (!clientDB->IsDirty())
            continue;

        std::filesystem::path savePath = (absolutePath / clientDB->GetName()).replace_extension(ClientDB::FILE_EXTENSION);

        clientDB->Save(savePath.string());
        clientDB->ClearDirty();
    }
}
