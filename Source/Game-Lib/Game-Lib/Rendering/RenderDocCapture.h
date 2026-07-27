#pragma once

#include <Base/Types.h>

#include <filesystem>
#include <string>

class RenderDocCapture
{
public:
    RenderDocCapture();
    ~RenderDocCapture();

    bool IsAvailable() const { return _api != nullptr; }
    const std::string& GetAvailabilityError() const { return _availabilityError; }

    bool QueueNextFrame(
        const std::filesystem::path& artifactPath,
        std::string& error);
    void BeginFrame();
    void EndFrame();

    static bool ResolveArtifactPath(
        const std::filesystem::path& automationRoot,
        const std::filesystem::path& requestedPath,
        std::filesystem::path& resolvedPath,
        std::string& error);

private:
    struct Api;

    void Fail(const std::string& error);
    void EmitMarker(const char* event, const std::string& error = {}) const;

    Api* _api = nullptr;
    std::string _availabilityError;
    std::filesystem::path _pendingPath;
    u32 _captureCountBeforeFrame = 0;
    i32 _versionMajor = 0;
    i32 _versionMinor = 0;
    i32 _versionPatch = 0;
    bool _capturing = false;
};
