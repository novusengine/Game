#include <Game-Lib/Rendering/RenderDocCapture.h>

#include <catch2/catch2.hpp>

#include <filesystem>

TEST_CASE("RenderDoc artifacts resolve below the configured Artifacts root")
{
    const std::filesystem::path automationRoot =
        std::filesystem::temp_directory_path() / "novus-renderdoc-tests";
    std::filesystem::path resolved;
    std::string error;

    REQUIRE(RenderDocCapture::ResolveArtifactPath(
        automationRoot,
        "feature-184/frame.rdc",
        resolved,
        error));
    CHECK(resolved == automationRoot / "Artifacts" / "feature-184" / "frame.rdc");
}

TEST_CASE("RenderDoc artifacts reject traversal, absolute paths, and other extensions")
{
    const std::filesystem::path automationRoot =
        std::filesystem::temp_directory_path() / "novus-renderdoc-tests";
    std::filesystem::path resolved;
    std::string error;

    CHECK_FALSE(RenderDocCapture::ResolveArtifactPath(
        automationRoot,
        "../outside.rdc",
        resolved,
        error));
    CHECK(error.find("escapes") != std::string::npos);

    CHECK_FALSE(RenderDocCapture::ResolveArtifactPath(
        automationRoot,
        automationRoot / "absolute.rdc",
        resolved,
        error));
    CHECK(error.find("relative") != std::string::npos);

    CHECK_FALSE(RenderDocCapture::ResolveArtifactPath(
        automationRoot,
        "feature-184/frame.png",
        resolved,
        error));
    CHECK(error.find(".rdc") != std::string::npos);
}
