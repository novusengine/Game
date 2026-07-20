#include "Game-Lib/ECS/Util/UIInputUtil.h"

#include <catch2/catch2.hpp>

TEST_CASE("UI input coordinates scale between render and reference space", "[UI][Input]")
{
    const vec2 referenceSize(1920.0f, 1080.0f);
    const vec2 renderSizes[] =
    {
        { 1280.0f, 720.0f },
        { 1600.0f, 900.0f },
        { 1920.0f, 1080.0f },
        { 2560.0f, 1440.0f },
        { 1600.0f, 1200.0f },
        { 3440.0f, 1440.0f }
    };

    for (const vec2& renderSize : renderSizes)
    {
        vec2 referencePosition;
        REQUIRE(ECS::Util::UIInput::PhysicalTopLeftToReference(renderSize * vec2(0.25f, 0.75f), renderSize, referenceSize, referencePosition));
        CHECK(referencePosition.x == Catch::Approx(480.0f));
        CHECK(referencePosition.y == Catch::Approx(270.0f));

        vec2 physicalBottomLeft;
        REQUIRE(ECS::Util::UIInput::ReferenceToPhysicalBottomLeft(referencePosition, renderSize, referenceSize, physicalBottomLeft));
        CHECK(physicalBottomLeft.x == Catch::Approx(renderSize.x * 0.25f));
        CHECK(physicalBottomLeft.y == Catch::Approx(renderSize.y * 0.25f));
    }
}

TEST_CASE("UI input coordinate conversion rejects zero-sized targets", "[UI][Input]")
{
    vec2 result;
    CHECK_FALSE(ECS::Util::UIInput::PhysicalTopLeftToReference(vec2(0.0f), vec2(0.0f, 1080.0f), vec2(1920.0f, 1080.0f), result));
    CHECK_FALSE(ECS::Util::UIInput::PhysicalTopLeftToReference(vec2(0.0f), vec2(1920.0f, 0.0f), vec2(1920.0f, 1080.0f), result));
}

TEST_CASE("UI input rectangles include minimum and exclude maximum edges", "[UI][Input]")
{
    const vec2 min(10.0f, 20.0f);
    const vec2 max(30.0f, 40.0f);

    CHECK(ECS::Util::UIInput::IsWithin(min, min, max));
    CHECK(ECS::Util::UIInput::IsWithin(vec2(29.999f, 39.999f), min, max));
    CHECK_FALSE(ECS::Util::UIInput::IsWithin(vec2(max.x, min.y), min, max));
    CHECK_FALSE(ECS::Util::UIInput::IsWithin(vec2(min.x, max.y), min, max));
}
