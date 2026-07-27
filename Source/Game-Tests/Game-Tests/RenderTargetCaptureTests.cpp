#include <Game-Lib/Rendering/RenderTargetCapture.h>

#include <catch2/catch2.hpp>

#include <cstring>
#include <filesystem>

namespace
{
    template <typename T>
    void Append(std::vector<u8>& bytes, T value)
    {
        const size_t offset = bytes.size();
        bytes.resize(offset + sizeof(T));
        std::memcpy(bytes.data() + offset, &value, sizeof(T));
    }
}

TEST_CASE("Render-target artifacts resolve below the configured Artifacts root")
{
    const std::filesystem::path automationRoot =
        std::filesystem::temp_directory_path() / "novus-render-target-tests";
    std::filesystem::path resolved;
    std::string error;

    REQUIRE(RenderTargetCapture::ResolveArtifactPath(
        automationRoot,
        "captures/scene.png",
        resolved,
        error));
    CHECK(resolved == automationRoot / "Artifacts" / "captures" / "scene.png");

    CHECK_FALSE(RenderTargetCapture::ResolveArtifactPath(
        automationRoot,
        "../outside.png",
        resolved,
        error));
    CHECK(error.find("escapes") != std::string::npos);

    CHECK_FALSE(RenderTargetCapture::ResolveArtifactPath(
        automationRoot,
        "captures/scene.jpg",
        resolved,
        error));
}

TEST_CASE("Normalized color targets convert to RGBA8")
{
    const std::vector<u8> source = {
        255, 0, 128, 255,
        10, 20, 30, 40
    };
    std::vector<u8> destination;
    std::string error;

    REQUIRE(RenderTargetCapture::ConvertColorToRGBA8(
        Renderer::ImageFormat::R8G8B8A8_UNORM,
        uvec2(2, 1),
        source,
        destination,
        error));
    CHECK(destination == source);

    REQUIRE(RenderTargetCapture::ConvertColorToRGBA8(
        Renderer::ImageFormat::B8G8R8A8_UNORM,
        uvec2(2, 1),
        source,
        destination,
        error));
    CHECK(destination == std::vector<u8>({
        128, 0, 255, 255,
        30, 20, 10, 40
    }));
}

TEST_CASE("Integer color targets are normalized for inspection")
{
    std::vector<u8> source;
    Append<u32>(source, 100);
    Append<u32>(source, 200);
    std::vector<u8> destination;
    std::string error;

    REQUIRE(RenderTargetCapture::ConvertColorToRGBA8(
        Renderer::ImageFormat::R32_UINT,
        uvec2(2, 1),
        source,
        destination,
        error));
    CHECK(destination == std::vector<u8>({
        0, 0, 0, 255,
        255, 255, 255, 255
    }));
}

TEST_CASE("Float color targets preserve inspectable values")
{
    std::vector<u8> source;
    Append<u16>(source, 0x0000);
    Append<u16>(source, 0x3800);
    Append<u16>(source, 0x3c00);
    Append<u16>(source, 0x3c00);
    std::vector<u8> destination;
    std::string error;

    REQUIRE(RenderTargetCapture::ConvertColorToRGBA8(
        Renderer::ImageFormat::R16G16B16A16_FLOAT,
        uvec2(1, 1),
        source,
        destination,
        error));
    CHECK(destination == std::vector<u8>({ 0, 128, 255, 255 }));
}

TEST_CASE("Scalar float targets are normalized for inspection")
{
    std::vector<u8> source;
    Append<f32>(source, 0.25f);
    Append<f32>(source, 0.75f);
    std::vector<u8> destination;
    std::string error;

    REQUIRE(RenderTargetCapture::ConvertColorToRGBA8(
        Renderer::ImageFormat::R32_FLOAT,
        uvec2(2, 1),
        source,
        destination,
        error));
    CHECK(destination == std::vector<u8>({
        0, 0, 0, 255,
        255, 255, 255, 255
    }));
}

TEST_CASE("Depth targets are normalized and inverted for inspection")
{
    std::vector<u8> source;
    Append<f32>(source, 0.25f);
    Append<f32>(source, 0.75f);
    std::vector<u8> destination;
    std::string error;

    REQUIRE(RenderTargetCapture::ConvertDepthToRGBA8(
        Renderer::DepthImageFormat::D32_FLOAT,
        uvec2(2, 1),
        source,
        destination,
        error));
    CHECK(destination == std::vector<u8>({
        255, 255, 255, 255,
        0, 0, 0, 255
    }));
}
