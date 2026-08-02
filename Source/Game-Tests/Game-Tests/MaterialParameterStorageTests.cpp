#include "Game-Lib/Rendering/Material/MaterialParameterStorage.h"

#include <catch2/catch2.hpp>

#include <array>

TEST_CASE("Material parameter storage aligns and deduplicates blocks", "[Rendering][MaterialParameterStorage]")
{
    MaterialLoading::MaterialParameterStorage storage;
    constexpr std::array<u8, 4> firstBytes = {1, 2, 3, 4};
    constexpr std::array<u8, 4> secondBytes = {5, 6, 7, 8};

    u32 firstOffset = 0;
    u32 duplicateOffset = 0;
    u32 secondOffset = 0;
    REQUIRE(storage.Append(firstBytes, 16, firstOffset));
    REQUIRE(storage.Append(firstBytes, 16, duplicateOffset));
    REQUIRE(storage.Append(secondBytes, 16, secondOffset));

    CHECK(firstOffset == 0);
    CHECK(duplicateOffset == firstOffset);
    CHECK(secondOffset == 16);
    CHECK(storage.GetByte(secondOffset + 3) == 8);

    const MaterialLoading::MaterialParameterStorageStats stats = storage.GetStats();
    CHECK(stats.uniqueBlocks == 2);
    CHECK(stats.dedupHits == 1);
    CHECK(stats.usedBytes == 20);
}

TEST_CASE("Material parameter storage rejects invalid alignment", "[Rendering][MaterialParameterStorage]")
{
    MaterialLoading::MaterialParameterStorage storage;
    constexpr std::array<u8, 1> bytes = {1};
    u32 offset = 0;

    CHECK_FALSE(storage.Append(bytes, 0, offset));
    CHECK_FALSE(storage.Append(bytes, 3, offset));
}
