#include <Base/Types.h>
#include <Base/Util/DebugHandler.h>

#include <Game/ECS/Components/Camera.h>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Example Test", "[camera]")
{
    ECS::Components::Camera camera;
    camera.dirtyView = false;

    SECTION("Camera Test 1")
    {
        camera.dirtyView = true;
        REQUIRE(camera.dirtyView == true);
    }

    SECTION("Camera Test 2")
    {
        REQUIRE(camera.dirtyView == false);
    }

    camera.dirtyView = true;

    SECTION("Camera Test 3")
    {
        REQUIRE(camera.dirtyView == true);
    }
}