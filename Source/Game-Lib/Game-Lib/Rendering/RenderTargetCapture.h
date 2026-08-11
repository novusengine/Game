#pragma once

#include <Base/Types.h>

#include <Renderer/Descriptors/DepthImageDesc.h>
#include <Renderer/Descriptors/ImageDesc.h>

#include <array>
#include <deque>
#include <filesystem>
#include <string>
#include <vector>

namespace Renderer
{
    class RenderGraph;
    class Renderer;
}

class RenderTargetCapture
{
public:
    explicit RenderTargetCapture(Renderer::Renderer* renderer);

    bool Queue(
        const std::string& debugName,
        const std::filesystem::path& artifactPath,
        std::string& error);
    void AddReadbackPass(Renderer::RenderGraph& renderGraph);
    void ProcessPending();

    static bool ResolveArtifactPath(
        const std::filesystem::path& automationRoot,
        const std::filesystem::path& requestedPath,
        std::filesystem::path& resolvedPath,
        std::string& error);

    static bool ConvertColorToRGBA8(
        Renderer::ImageFormat format,
        uvec2 dimensions,
        const std::vector<u8>& source,
        std::vector<u8>& destination,
        std::string& error);
    static bool ConvertDepthToRGBA8(
        Renderer::DepthImageFormat format,
        uvec2 dimensions,
        const std::vector<u8>& source,
        std::vector<u8>& destination,
        std::string& error);

private:
    enum class TargetKind
    {
        Color,
        Depth
    };

    struct Request
    {
        std::string debugName;
        std::filesystem::path path;
        TargetKind kind = TargetKind::Color;
        Renderer::ImageID image = Renderer::ImageID::Invalid();
        Renderer::DepthImageID depthImage = Renderer::DepthImageID::Invalid();
    };

    bool FindTarget(const std::string& debugName, Request& request, std::string& error) const;
    void Process(Request request);

    Renderer::Renderer* _renderer = nullptr;
    std::deque<Request> _pending;
};
