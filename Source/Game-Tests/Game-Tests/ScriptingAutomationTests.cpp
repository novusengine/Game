#include <Game-Lib/Scripting/Handlers/GameHandler.h>
#include <Game-Lib/Scripting/Handlers/SchedulerHandler.h>

#include <MetaGen/Game/Lua/Lua.h>

#include <Scripting/LuaManager.h>
#include <Scripting/Zenith.h>

#include <catch2/catch2.hpp>
#include <lualib.h>

#include <chrono>
#include <memory>
#include <string>
#include <thread>

namespace
{
    class ScriptingAutomationHarness
    {
    public:
        ScriptingAutomationHarness()
        {
            _luaManager.PrepareToAddLuaHandlers(
                static_cast<u16>(MetaGen::Game::Lua::LuaHandlerTypeEnum::Count));

            _gameHandler = std::make_unique<Scripting::Game::GameHandler>();
            _schedulerHandler = std::make_unique<Scripting::Scheduler::SchedulerHandler>();
            _luaManager.SetLuaHandler(
                static_cast<Scripting::LuaHandlerID>(MetaGen::Game::Lua::LuaHandlerTypeEnum::Game),
                _gameHandler.get());
            _luaManager.SetLuaHandler(
                static_cast<Scripting::LuaHandlerID>(MetaGen::Game::Lua::LuaHandlerTypeEnum::Scheduler),
                _schedulerHandler.get());

            REQUIRE(_luaManager.GetZenithStateManager().Add(_key));
            _zenith = _luaManager.GetZenithStateManager().Get(_key);
            REQUIRE(_zenith != nullptr);

            _zenith->SetState(luaL_newstate());
            REQUIRE(_zenith->state != nullptr);
            REQUIRE(_luaManager.GetZenithStateManager().Add(_key, _zenith->state));

            _zenith->RegisterDefaultLibraries();
            _zenith->PushLightUserData(&_luaManager);
            _zenith->SetGlobalKey("Zenith");

            Scripting::LuaMethodTable::Set(_zenith, Scripting::Game::gameGlobalMethods, "Game");
            _schedulerHandler->Register(_zenith);
        }

        ~ScriptingAutomationHarness()
        {
            _schedulerHandler->Clear(_zenith);
            _luaManager.GetZenithStateManager().Remove(_key);
        }

        bool Execute(const std::string& source)
        {
            return _luaManager.DoString(_zenith, source);
        }

        i32 GetGlobalInteger(const char* name)
        {
            _zenith->GetGlobalKey(name);
            const i32 value = _zenith->Get<i32>(-1);
            _zenith->Pop();
            return value;
        }

        bool GetGlobalBoolean(const char* name)
        {
            _zenith->GetGlobalKey(name);
            const bool value = _zenith->Get<bool>(-1);
            _zenith->Pop();
            return value;
        }

        Scripting::Game::GameHandler& GameHandler() { return *_gameHandler; }
        Scripting::Scheduler::SchedulerHandler& SchedulerHandler() { return *_schedulerHandler; }
        Scripting::Zenith* Zenith() { return _zenith; }

    private:
        Scripting::ZenithInfoKey _key = Scripting::ZenithInfoKey::MakeGlobal(0, 0);
        Scripting::LuaManager _luaManager;
        std::unique_ptr<Scripting::Game::GameHandler> _gameHandler;
        std::unique_ptr<Scripting::Scheduler::SchedulerHandler> _schedulerHandler;
        Scripting::Zenith* _zenith = nullptr;
    };
}

TEST_CASE("Native automation primitives support deterministic script execution")
{
    ScriptingAutomationHarness harness;

    REQUIRE(harness.Execute(R"(
        fired = 0
        Scheduler.AfterSeconds(0, function()
            fired += 1
            Scheduler.AfterSeconds(0, function()
                fired += 10
            end)
        end)

        cancelledFired = false
        cancelledHandle = Scheduler.AfterSeconds(0, function()
            cancelledFired = true
        end)
        firstCancel = Scheduler.Cancel(cancelledHandle)
        secondCancel = Scheduler.Cancel(cancelledHandle)
    )"));

    CHECK(harness.GetGlobalInteger("fired") == 0);
    CHECK(harness.GetGlobalBoolean("firstCancel"));
    CHECK_FALSE(harness.GetGlobalBoolean("secondCancel"));

    harness.SchedulerHandler().Update(harness.Zenith(), 0.0f);
    CHECK(harness.GetGlobalInteger("fired") == 1);
    CHECK_FALSE(harness.GetGlobalBoolean("cancelledFired"));

    harness.SchedulerHandler().Update(harness.Zenith(), 0.0f);
    CHECK(harness.GetGlobalInteger("fired") == 11);

    REQUIRE(harness.Execute(R"(
        delayedFired = false
        Scheduler.AfterSeconds(0.05, function()
            delayedFired = true
        end)
    )"));

    harness.SchedulerHandler().Update(harness.Zenith(), 0.0f);
    CHECK_FALSE(harness.GetGlobalBoolean("delayedFired"));

    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    harness.SchedulerHandler().Update(harness.Zenith(), 0.0f);
    CHECK(harness.GetGlobalBoolean("delayedFired"));

    REQUIRE(harness.Execute(R"(
        frameStage = 0
        Scheduler.AfterFrames(3, function()
            frameStage = 1
            Scheduler.AfterFrames(2, function()
                frameStage = 2
            end)
        end)
    )"));

    harness.SchedulerHandler().Update(harness.Zenith(), 0.0f);
    CHECK(harness.GetGlobalInteger("frameStage") == 0);
    harness.SchedulerHandler().Update(harness.Zenith(), 0.0f);
    CHECK(harness.GetGlobalInteger("frameStage") == 0);
    harness.SchedulerHandler().Update(harness.Zenith(), 0.0f);
    CHECK(harness.GetGlobalInteger("frameStage") == 1);
    harness.SchedulerHandler().Update(harness.Zenith(), 0.0f);
    CHECK(harness.GetGlobalInteger("frameStage") == 1);
    harness.SchedulerHandler().Update(harness.Zenith(), 0.0f);
    CHECK(harness.GetGlobalInteger("frameStage") == 2);

    REQUIRE(harness.Execute(R"(
        firedAfterClear = false
        Scheduler.AfterSeconds(0, function()
            firedAfterClear = true
        end)
        Scheduler.AfterFrames(1, function()
            firedAfterClear = true
        end)
    )"));

    harness.SchedulerHandler().Clear(harness.Zenith());
    harness.SchedulerHandler().Update(harness.Zenith(), 0.0f);
    CHECK_FALSE(harness.GetGlobalBoolean("firedAfterClear"));

    REQUIRE(harness.Execute("gameLoadedBeforePostLoad = Game.IsLoaded()"));
    CHECK_FALSE(harness.GetGlobalBoolean("gameLoadedBeforePostLoad"));

    harness.GameHandler().PostLoad(harness.Zenith());
    REQUIRE(harness.Execute("gameLoadedAfterPostLoad = Game.IsLoaded()"));
    CHECK(harness.GetGlobalBoolean("gameLoadedAfterPostLoad"));

    harness.GameHandler().Clear(harness.Zenith());
    REQUIRE(harness.Execute("gameLoadedAfterClear = Game.IsLoaded()"));
    CHECK_FALSE(harness.GetGlobalBoolean("gameLoadedAfterClear"));
}
