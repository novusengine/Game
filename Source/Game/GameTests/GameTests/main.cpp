#include <Base/Types.h>
#include <Base/Util/DebugHandler.h>

#include <Game/Application/Application.h>
#include <Game/ECS/Components/Camera.h>

#include <catch2/catch_test_macros.hpp>
 
TEST_CASE("Application Start", "[Application]")
{
    Application app;
    app.Start(false);

    f32 deltaTime = 1.0f / 60.0f;
    app.Tick(deltaTime);

    REQUIRE(app.IsRunning());
}