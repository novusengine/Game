#include "RenderDocCapture.h"

#include "Game-Lib/Util/AutomationUtil.h"

#include <Base/Util/DebugHandler.h>

#include <cstdlib>
#include <vector>

#if defined(_WIN32)
#include <Windows.h>
#endif

namespace fs = std::filesystem;

namespace
{
#if defined(_WIN32)
    using RenderDocGetApi = i32(__cdecl*)(i32 version, void** api);
    using GetApiVersion = void(__cdecl*)(i32* major, i32* minor, i32* patch);
    using SetCaptureFilePathTemplate = void(__cdecl*)(const char* pathTemplate);
    using GetNumCaptures = u32(__cdecl*)();
    using GetCapture = u32(__cdecl*)(u32 index, char* filename, u32* pathLength, u64* timestamp);
    using StartFrameCapture = void(__cdecl*)(void* device, void* window);
    using IsFrameCapturing = u32(__cdecl*)();
    using EndFrameCapture = u32(__cdecl*)(void* device, void* window);

    // RenderDoc 1.0's stable function-table prefix. Unused entries remain opaque
    // pointers so this integration does not vendor RenderDoc's large public header.
    struct RenderDocApiPrefix
    {
        GetApiVersion getApiVersion;
        void* setCaptureOptionU32;
        void* setCaptureOptionF32;
        void* getCaptureOptionU32;
        void* getCaptureOptionF32;
        void* setFocusToggleKeys;
        void* setCaptureKeys;
        void* getOverlayBits;
        void* maskOverlayBits;
        void* removeHooks;
        void* unloadCrashHandler;
        SetCaptureFilePathTemplate setCaptureFilePathTemplate;
        void* getCaptureFilePathTemplate;
        GetNumCaptures getNumCaptures;
        GetCapture getCapture;
        void* triggerCapture;
        void* isTargetControlConnected;
        void* launchReplayUi;
        void* setActiveWindow;
        StartFrameCapture startFrameCapture;
        IsFrameCapturing isFrameCapturing;
        EndFrameCapture endFrameCapture;
    };

    constexpr i32 RenderDocApiVersion100 = 10000;
#endif

    std::string EscapeJson(const std::string& value)
    {
        std::string result;
        result.reserve(value.size());
        constexpr char Hex[] = "0123456789abcdef";
        for (const unsigned char character : value)
        {
            switch (character)
            {
            case '"': result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\b': result += "\\b"; break;
            case '\f': result += "\\f"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:
                if (character < 0x20)
                {
                    result += "\\u00";
                    result.push_back(Hex[character >> 4]);
                    result.push_back(Hex[character & 0x0f]);
                }
                else
                {
                    result.push_back(static_cast<char>(character));
                }
                break;
            }
        }
        return result;
    }

    std::string ToUtf8(const fs::path& path)
    {
        const std::u8string value = path.generic_u8string();
        return std::string(
            reinterpret_cast<const char*>(value.data()),
            value.size());
    }

    fs::path FromUtf8(const char* value)
    {
        return fs::path(reinterpret_cast<const char8_t*>(value));
    }

    bool PublishCapture(
        const fs::path& source,
        const fs::path& destination,
        std::string& error)
    {
        if (source == destination)
            return true;

#if defined(_WIN32)
        if (MoveFileExW(
            source.c_str(),
            destination.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0)
        {
            return true;
        }
        error = "Failed to publish RenderDoc capture atomically (Windows error " +
            std::to_string(GetLastError()) + ")";
        return false;
#else
        std::error_code moveError;
        fs::rename(source, destination, moveError);
        if (!moveError)
            return true;
        error = "Failed to publish RenderDoc capture atomically: " + moveError.message();
        return false;
#endif
    }
}

struct RenderDocCapture::Api
{
#if defined(_WIN32)
    RenderDocApiPrefix* functions = nullptr;
#endif
};

RenderDocCapture::RenderDocCapture()
{
#if defined(_WIN32)
    HMODULE module = GetModuleHandleW(L"renderdoc.dll");
    if (!module)
    {
        _availabilityError =
            "-renderdoc was specified, but the RenderDoc Vulkan layer did not load";
        return;
    }

    const auto getApi = reinterpret_cast<RenderDocGetApi>(
        GetProcAddress(module, "RENDERDOC_GetAPI"));
    if (!getApi)
    {
        _availabilityError = "renderdoc.dll does not export RENDERDOC_GetAPI";
        return;
    }

    void* functions = nullptr;
    if (getApi(RenderDocApiVersion100, &functions) != 1 || !functions)
    {
        _availabilityError = "RENDERDOC_GetAPI rejected API version 1.0.0";
        return;
    }

    _api = new Api();
    _api->functions = static_cast<RenderDocApiPrefix*>(functions);
    _api->functions->getApiVersion(&_versionMajor, &_versionMinor, &_versionPatch);
    NC_LOG_INFO(
        "RenderDoc in-application API {}.{}.{} is available",
        _versionMajor,
        _versionMinor,
        _versionPatch);
#else
    _availabilityError = "RenderDoc automation is currently implemented only on Windows";
#endif
}

RenderDocCapture::~RenderDocCapture()
{
    delete _api;
}

bool RenderDocCapture::ResolveArtifactPath(
    const fs::path& automationRoot,
    const fs::path& requestedPath,
    fs::path& resolvedPath,
    std::string& error)
{
    return Util::Automation::ResolveArtifactPath(
        automationRoot,
        requestedPath,
        ".rdc",
        resolvedPath,
        error);
}

bool RenderDocCapture::QueueNextFrame(
    const fs::path& artifactPath,
    std::string& error)
{
    if (!_api)
    {
        error = _availabilityError;
        return false;
    }
    if (_capturing || !_pendingPath.empty())
    {
        error = "A RenderDoc capture is already queued or in progress";
        return false;
    }

    const char* automationRoot = std::getenv("NOVUS_AUTOMATION_ROOT");
    if (!automationRoot || automationRoot[0] == '\0')
    {
        error = "NOVUS_AUTOMATION_ROOT is not configured";
        return false;
    }
    if (!ResolveArtifactPath(automationRoot, artifactPath, _pendingPath, error))
        return false;

    NC_LOG_INFO(
        "RenderDoc capture queued for next frame: {}",
        _pendingPath.generic_string());
    return true;
}

void RenderDocCapture::BeginFrame()
{
#if defined(_WIN32)
    if (!_api || _pendingPath.empty() || _capturing)
        return;

    std::error_code pathError;
    fs::create_directories(_pendingPath.parent_path(), pathError);
    if (pathError)
    {
        Fail("Failed to create artifact directory: " + pathError.message());
        return;
    }

    fs::path captureTemplate = _pendingPath;
    captureTemplate.replace_extension();
    const std::string captureTemplateUtf8 = ToUtf8(captureTemplate);

    NC_LOG_INFO("RenderDoc capture: querying existing capture count");
    _captureCountBeforeFrame = _api->functions->getNumCaptures();
    NC_LOG_INFO(
        "RenderDoc capture: setting path template to {}",
        captureTemplate.generic_string());
    _api->functions->setCaptureFilePathTemplate(captureTemplateUtf8.c_str());
    NC_LOG_INFO("RenderDoc capture: starting frame");
    _api->functions->startFrameCapture(nullptr, nullptr);
    NC_LOG_INFO("RenderDoc capture: checking capture state");
    _capturing = _api->functions->isFrameCapturing() != 0;
    if (!_capturing)
        Fail("RenderDoc did not begin capturing the requested frame");
    else
        NC_LOG_INFO("RenderDoc capture: frame capture started");
#endif
}

void RenderDocCapture::EndFrame()
{
#if defined(_WIN32)
    if (!_api || !_capturing)
        return;

    _capturing = false;
    NC_LOG_INFO("RenderDoc capture: ending frame");
    if (_api->functions->endFrameCapture(nullptr, nullptr) == 0)
    {
        Fail("RenderDoc failed to end the frame capture");
        return;
    }

    const u32 captureCount = _api->functions->getNumCaptures();
    if (captureCount <= _captureCountBeforeFrame)
    {
        Fail("RenderDoc completed without registering a capture file");
        return;
    }

    u32 pathLength = 0;
    if (_api->functions->getCapture(captureCount - 1, nullptr, &pathLength, nullptr) == 0 ||
        pathLength == 0)
    {
        Fail("RenderDoc did not report the generated capture path");
        return;
    }

    std::vector<char> generatedPath(pathLength + 1, '\0');
    if (_api->functions->getCapture(
        captureCount - 1,
        generatedPath.data(),
        &pathLength,
        nullptr) == 0)
    {
        Fail("RenderDoc failed to return the generated capture path");
        return;
    }

    const fs::path sourcePath = FromUtf8(generatedPath.data());
    std::error_code fileError;
    if (!fs::is_regular_file(sourcePath, fileError) || fileError)
    {
        Fail("RenderDoc capture file is missing after capture completion");
        return;
    }

    std::string publishError;
    if (!PublishCapture(sourcePath, _pendingPath, publishError))
    {
        Fail(publishError);
        return;
    }

    EmitMarker("artifact_ready");
    _pendingPath.clear();
#endif
}

void RenderDocCapture::Fail(const std::string& error)
{
    _capturing = false;
    EmitMarker("artifact_failed", error);
    _pendingPath.clear();
}

void RenderDocCapture::EmitMarker(
    const char* event,
    const std::string& error) const
{
    std::string marker =
        "NOVUS_ARTIFACT {\"type\":\"renderdoc\",\"event\":\"" + std::string(event) +
        "\",\"path\":\"" + EscapeJson(_pendingPath.generic_string()) +
        "\",\"apiVersion\":\"" +
        std::to_string(_versionMajor) + "." +
        std::to_string(_versionMinor) + "." +
        std::to_string(_versionPatch) + "\"";
    if (!error.empty())
        marker += ",\"error\":\"" + EscapeJson(error) + "\"";
    marker += "}";

    if (error.empty())
    {
        NC_LOG_INFO("{}", marker);
    }
    else
    {
        NC_LOG_ERROR("{}", marker);
    }
}
