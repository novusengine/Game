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

TEST_CASE("Mutable material parameter blocks remain independent and accept updates", "[Rendering][MaterialParameterStorage]")
{
    MaterialLoading::MaterialParameterStorage storage;
    const std::array<u8, 16> initial = {};
    u32 firstOffset = 0;
    u32 secondOffset = 0;
    REQUIRE(storage.AppendMutable(initial, 16, firstOffset));
    REQUIRE(storage.AppendMutable(initial, 16, secondOffset));
    CHECK(firstOffset != secondOffset);

    const std::array<u8, 4> update = {1, 2, 3, 4};
    REQUIRE(storage.Write(firstOffset + 4, update));
    CHECK(storage.GetByte(firstOffset + 4) == 1);
    CHECK(storage.GetByte(secondOffset + 4) == 0);
}

TEST_CASE("Sub-vector animation writes preserve adjacent material parameters", "[Rendering][MaterialParameterStorage]")
{
    MaterialLoading::MaterialParameterStorage storage;
    const std::array<u8, 16> initial = {10, 11, 12, 13, 20, 21, 22, 23, 30, 31, 32, 33, 40, 41, 42, 43};
    u32 offset = 0;
    REQUIRE(storage.AppendMutable(initial, 16, offset));

    const std::array<u8, 8> vec2Sample = {1, 2, 3, 4, 5, 6, 7, 8};
    REQUIRE(storage.Write(offset, vec2Sample));
    for (u32 index = 0; index < vec2Sample.size(); ++index)
        CHECK(storage.GetByte(offset + index) == vec2Sample[index]);
    for (u32 index = static_cast<u32>(vec2Sample.size()); index < initial.size(); ++index)
        CHECK(storage.GetByte(offset + index) == initial[index]);
}
