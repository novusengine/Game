#include "Game-Lib/ECS/Singletons/EngineStats.h"

#include <Base/Util/StringUtils.h>

#include <catch2/catch2.hpp>

TEST_CASE("Engine stats retain exactly the declared sample window", "[ECS][EngineStats]")
{
    ECS::Singletons::EngineStats stats;
    for (u32 index = 0; index < ECS::Singletons::EngineStats::MAX_ENTRIES + 7u; ++index)
    {
        const f32 value = static_cast<f32>(index);
        stats.AddTimings(value, value, value, value, value, value);
        stats.AddNamedStat("pass", value);
    }

    CHECK(stats.frameStats.size() == ECS::Singletons::EngineStats::MAX_ENTRIES);
    const u32 passHash = StringUtils::fnv1a_32("pass", 4);
    REQUIRE(stats.namedStats.contains(passHash));
    CHECK(stats.namedStats.at(passHash).size() == ECS::Singletons::EngineStats::MAX_ENTRIES);
}

TEST_CASE("Engine stats averages clamp to available samples and reject empty windows", "[ECS][EngineStats]")
{
    ECS::Singletons::EngineStats stats;
    stats.AddTimings(2.0f, 4.0f, 6.0f, 8.0f, 10.0f, 12.0f);
    stats.AddTimings(4.0f, 6.0f, 8.0f, 10.0f, 12.0f, 14.0f);
    stats.AddNamedStat("pass", 2.0f);
    stats.AddNamedStat("pass", 4.0f);

    CHECK(stats.AverageFrame(99).deltaTimeS == Catch::Approx(3.0f));
    CHECK(stats.AverageFrame(0).deltaTimeS == 0.0f);

    f32 average = -1.0f;
    CHECK(stats.AverageNamed("pass", 99, average));
    CHECK(average == Catch::Approx(3.0f));
    CHECK_FALSE(stats.AverageNamed("pass", 0, average));
    CHECK(average == 0.0f);
    CHECK_FALSE(stats.AverageNamed("missing", 99, average));
}
