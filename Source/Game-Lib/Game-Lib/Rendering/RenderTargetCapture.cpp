#include "RenderTargetCapture.h"

#include "Game-Lib/Util/AutomationUtil.h"

#include <Base/Util/DebugHandler.h>

#include <Renderer/Renderer.h>
#include <Renderer/RenderGraph.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../../../../Submodules/Engine/Dependencies/glfw/deps/stb_image_write.h"

#include <glm/gtc/packing.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <ranges>
#include <system_error>

#if defined(_WIN32)
#include <Windows.h>
#endif

namespace fs = std::filesystem;

namespace
{
    constexpr u64 MaxCaptureBytes = 512ull * 1024ull * 1024ull;

    struct DecodedImage
    {
        std::vector<std::array<double, 4>> pixels;
        u8 components = 0;
        bool adaptive = false;
        bool preserveAlpha = false;
    };

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

    void EmitArtifactMarker(
        const char* event,
        const std::string& name,
        const fs::path& path,
        uvec2 dimensions = {},
        const std::string& format = {},
        const std::string& error = {})
    {
        std::string marker =
            "NOVUS_ARTIFACT {\"type\":\"render_target\",\"event\":\"" + std::string(event) +
            "\",\"name\":\"" + EscapeJson(name) +
            "\",\"path\":\"" + EscapeJson(path.generic_string()) + "\"";
        if (dimensions.x && dimensions.y)
        {
            marker +=
                ",\"width\":" + std::to_string(dimensions.x) +
                ",\"height\":" + std::to_string(dimensions.y);
        }
        if (!format.empty())
            marker += ",\"format\":\"" + EscapeJson(format) + "\"";
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

    bool ReplaceFile(const fs::path& temporaryPath, const fs::path& destinationPath)
    {
#if defined(_WIN32)
        return MoveFileExW(
            temporaryPath.c_str(),
            destinationPath.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
        std::error_code error;
        fs::rename(temporaryPath, destinationPath, error);
        return !error;
#endif
    }

    size_t ColorPixelSize(Renderer::ImageFormat format)
    {
        using Renderer::ImageFormat;
        switch (format)
        {
        case ImageFormat::R32G32B32A32_FLOAT:
        case ImageFormat::R32G32B32A32_UINT:
        case ImageFormat::R32G32B32A32_SINT: return 16;
        case ImageFormat::R32G32B32_FLOAT:
        case ImageFormat::R32G32B32_UINT:
        case ImageFormat::R32G32B32_SINT: return 12;
        case ImageFormat::R16G16B16A16_FLOAT:
        case ImageFormat::R16G16B16A16_UNORM:
        case ImageFormat::R16G16B16A16_UINT:
        case ImageFormat::R16G16B16A16_SNORM:
        case ImageFormat::R16G16B16A16_SINT:
        case ImageFormat::R32G32_FLOAT:
        case ImageFormat::R32G32_UINT:
        case ImageFormat::R32G32_SINT: return 8;
        case ImageFormat::R10G10B10A2_UNORM:
        case ImageFormat::R10G10B10A2_UINT:
        case ImageFormat::R11G11B10_UFLOAT:
        case ImageFormat::R8G8B8A8_UNORM:
        case ImageFormat::R8G8B8A8_UNORM_SRGB:
        case ImageFormat::R8G8B8A8_UINT:
        case ImageFormat::R8G8B8A8_SNORM:
        case ImageFormat::R8G8B8A8_SINT:
        case ImageFormat::B8G8R8A8_UNORM:
        case ImageFormat::B8G8R8A8_UNORM_SRGB:
        case ImageFormat::B8G8R8A8_SNORM:
        case ImageFormat::B8G8R8A8_UINT:
        case ImageFormat::B8G8R8A8_SINT:
        case ImageFormat::R16G16_FLOAT:
        case ImageFormat::R16G16_UNORM:
        case ImageFormat::R16G16_UINT:
        case ImageFormat::R16G16_SNORM:
        case ImageFormat::R16G16_SINT:
        case ImageFormat::R32_FLOAT:
        case ImageFormat::R32_UINT:
        case ImageFormat::R32_SINT: return 4;
        case ImageFormat::R8G8_UNORM:
        case ImageFormat::R8G8_UINT:
        case ImageFormat::R8G8_SNORM:
        case ImageFormat::R8G8_SINT:
        case ImageFormat::R16_FLOAT:
        case ImageFormat::D16_UNORM:
        case ImageFormat::R16_UNORM:
        case ImageFormat::R16_UINT:
        case ImageFormat::R16_SNORM:
        case ImageFormat::R16_SINT: return 2;
        case ImageFormat::R8_UNORM:
        case ImageFormat::R8_UINT:
        case ImageFormat::R8_SNORM:
        case ImageFormat::R8_SINT: return 1;
        default: return 0;
        }
    }

    size_t DepthPixelSize(Renderer::DepthImageFormat format)
    {
        using Renderer::DepthImageFormat;
        switch (format)
        {
        case DepthImageFormat::D32_FLOAT_S8X24_UINT:
        case DepthImageFormat::D32_FLOAT:
        case DepthImageFormat::R32_FLOAT:
        case DepthImageFormat::D24_UNORM_S8_UINT: return 4;
        case DepthImageFormat::D16_UNORM:
        case DepthImageFormat::R16_UNORM: return 2;
        default: return 0;
        }
    }

    const char* FormatName(Renderer::ImageFormat format)
    {
        using Renderer::ImageFormat;
#define FORMAT_NAME(value) case ImageFormat::value: return #value
        switch (format)
        {
        FORMAT_NAME(R32G32B32A32_FLOAT);
        FORMAT_NAME(R32G32B32A32_UINT);
        FORMAT_NAME(R32G32B32A32_SINT);
        FORMAT_NAME(R32G32B32_FLOAT);
        FORMAT_NAME(R32G32B32_UINT);
        FORMAT_NAME(R32G32B32_SINT);
        FORMAT_NAME(R16G16B16A16_FLOAT);
        FORMAT_NAME(R16G16B16A16_UNORM);
        FORMAT_NAME(R16G16B16A16_UINT);
        FORMAT_NAME(R16G16B16A16_SNORM);
        FORMAT_NAME(R16G16B16A16_SINT);
        FORMAT_NAME(R32G32_FLOAT);
        FORMAT_NAME(R32G32_UINT);
        FORMAT_NAME(R32G32_SINT);
        FORMAT_NAME(R10G10B10A2_UNORM);
        FORMAT_NAME(R10G10B10A2_UINT);
        FORMAT_NAME(R11G11B10_UFLOAT);
        FORMAT_NAME(R8G8B8A8_UNORM);
        FORMAT_NAME(R8G8B8A8_UNORM_SRGB);
        FORMAT_NAME(R8G8B8A8_UINT);
        FORMAT_NAME(R8G8B8A8_SNORM);
        FORMAT_NAME(R8G8B8A8_SINT);
        FORMAT_NAME(B8G8R8A8_UNORM);
        FORMAT_NAME(B8G8R8A8_UNORM_SRGB);
        FORMAT_NAME(B8G8R8A8_SNORM);
        FORMAT_NAME(B8G8R8A8_UINT);
        FORMAT_NAME(B8G8R8A8_SINT);
        FORMAT_NAME(R16G16_FLOAT);
        FORMAT_NAME(R16G16_UNORM);
        FORMAT_NAME(R16G16_UINT);
        FORMAT_NAME(R16G16_SNORM);
        FORMAT_NAME(R16G16_SINT);
        FORMAT_NAME(R32_FLOAT);
        FORMAT_NAME(R32_UINT);
        FORMAT_NAME(R32_SINT);
        FORMAT_NAME(R8G8_UNORM);
        FORMAT_NAME(R8G8_UINT);
        FORMAT_NAME(R8G8_SNORM);
        FORMAT_NAME(R8G8_SINT);
        FORMAT_NAME(R16_FLOAT);
        FORMAT_NAME(D16_UNORM);
        FORMAT_NAME(R16_UNORM);
        FORMAT_NAME(R16_UINT);
        FORMAT_NAME(R16_SNORM);
        FORMAT_NAME(R16_SINT);
        FORMAT_NAME(R8_UNORM);
        FORMAT_NAME(R8_UINT);
        FORMAT_NAME(R8_SNORM);
        FORMAT_NAME(R8_SINT);
        default: return "UNKNOWN";
        }
#undef FORMAT_NAME
    }

    const char* FormatName(Renderer::DepthImageFormat format)
    {
        using Renderer::DepthImageFormat;
        switch (format)
        {
        case DepthImageFormat::D32_FLOAT_S8X24_UINT: return "D32_FLOAT_S8X24_UINT";
        case DepthImageFormat::D32_FLOAT: return "D32_FLOAT";
        case DepthImageFormat::R32_FLOAT: return "R32_FLOAT";
        case DepthImageFormat::D24_UNORM_S8_UINT: return "D24_UNORM_S8_UINT";
        case DepthImageFormat::D16_UNORM: return "D16_UNORM";
        case DepthImageFormat::R16_UNORM: return "R16_UNORM";
        default: return "UNKNOWN";
        }
    }

    template <typename T>
    T Read(const u8* source)
    {
        T value;
        std::memcpy(&value, source, sizeof(T));
        return value;
    }

    template <typename T, size_t N, typename Transform>
    void DecodeComponents(
        const u8* source,
        std::array<double, 4>& destination,
        Transform&& transform)
    {
        for (size_t component = 0; component < N; ++component)
            destination[component] = transform(Read<T>(source + component * sizeof(T)));
    }

    double Snorm(i64 value, i64 maximum)
    {
        return std::clamp(static_cast<double>(value) / static_cast<double>(maximum), -1.0, 1.0) * 0.5 + 0.5;
    }

    bool DecodeColor(
        Renderer::ImageFormat format,
        uvec2 dimensions,
        const std::vector<u8>& source,
        DecodedImage& decoded,
        std::string& error)
    {
        using Renderer::ImageFormat;
        const size_t pixelSize = ColorPixelSize(format);
        const u64 pixelCount = static_cast<u64>(dimensions.x) * dimensions.y;
        if (!pixelSize || source.size() != pixelCount * pixelSize)
        {
            error = pixelSize ? "Raw render-target size does not match its dimensions" : "Unsupported render-target format";
            return false;
        }

        decoded.pixels.resize(static_cast<size_t>(pixelCount), { 0.0, 0.0, 0.0, 1.0 });
        switch (format)
        {
        case ImageFormat::R32G32B32A32_FLOAT:
        case ImageFormat::R32G32B32A32_UINT:
        case ImageFormat::R32G32B32A32_SINT:
        case ImageFormat::R16G16B16A16_FLOAT:
        case ImageFormat::R16G16B16A16_UNORM:
        case ImageFormat::R16G16B16A16_UINT:
        case ImageFormat::R16G16B16A16_SNORM:
        case ImageFormat::R16G16B16A16_SINT:
        case ImageFormat::R10G10B10A2_UNORM:
        case ImageFormat::R10G10B10A2_UINT:
        case ImageFormat::R8G8B8A8_UNORM:
        case ImageFormat::R8G8B8A8_UNORM_SRGB:
        case ImageFormat::R8G8B8A8_UINT:
        case ImageFormat::R8G8B8A8_SNORM:
        case ImageFormat::R8G8B8A8_SINT:
        case ImageFormat::B8G8R8A8_UNORM:
        case ImageFormat::B8G8R8A8_UNORM_SRGB:
        case ImageFormat::B8G8R8A8_SNORM:
        case ImageFormat::B8G8R8A8_UINT:
        case ImageFormat::B8G8R8A8_SINT:
            decoded.components = 4;
            break;
        case ImageFormat::R32G32B32_FLOAT:
        case ImageFormat::R32G32B32_UINT:
        case ImageFormat::R32G32B32_SINT:
        case ImageFormat::R11G11B10_UFLOAT:
            decoded.components = 3;
            break;
        case ImageFormat::R32G32_FLOAT:
        case ImageFormat::R32G32_UINT:
        case ImageFormat::R32G32_SINT:
        case ImageFormat::R16G16_FLOAT:
        case ImageFormat::R16G16_UNORM:
        case ImageFormat::R16G16_UINT:
        case ImageFormat::R16G16_SNORM:
        case ImageFormat::R16G16_SINT:
        case ImageFormat::R8G8_UNORM:
        case ImageFormat::R8G8_UINT:
        case ImageFormat::R8G8_SNORM:
        case ImageFormat::R8G8_SINT:
            decoded.components = 2;
            break;
        default:
            decoded.components = 1;
            break;
        }

        const auto isInteger = [format]()
        {
            return Renderer::ToImageComponentType(format) == Renderer::ImageComponentType::UINT ||
                Renderer::ToImageComponentType(format) == Renderer::ImageComponentType::SINT;
        };
        decoded.adaptive =
            isInteger() ||
            (decoded.components == 1 &&
                Renderer::ToImageComponentType(format) == Renderer::ImageComponentType::FLOAT);
        decoded.preserveAlpha = decoded.components == 4 && !decoded.adaptive;

        for (size_t pixelIndex = 0; pixelIndex < decoded.pixels.size(); ++pixelIndex)
        {
            const u8* pixel = source.data() + pixelIndex * pixelSize;
            auto& output = decoded.pixels[pixelIndex];

            switch (format)
            {
            case ImageFormat::R32G32B32A32_FLOAT: DecodeComponents<f32, 4>(pixel, output, [](f32 v) { return v; }); break;
            case ImageFormat::R32G32B32_FLOAT: DecodeComponents<f32, 3>(pixel, output, [](f32 v) { return v; }); break;
            case ImageFormat::R32G32_FLOAT: DecodeComponents<f32, 2>(pixel, output, [](f32 v) { return v; }); break;
            case ImageFormat::R32_FLOAT: DecodeComponents<f32, 1>(pixel, output, [](f32 v) { return v; }); break;

            case ImageFormat::R32G32B32A32_UINT: DecodeComponents<u32, 4>(pixel, output, [](u32 v) { return v; }); break;
            case ImageFormat::R32G32B32_UINT: DecodeComponents<u32, 3>(pixel, output, [](u32 v) { return v; }); break;
            case ImageFormat::R32G32_UINT: DecodeComponents<u32, 2>(pixel, output, [](u32 v) { return v; }); break;
            case ImageFormat::R32_UINT: DecodeComponents<u32, 1>(pixel, output, [](u32 v) { return v; }); break;

            case ImageFormat::R32G32B32A32_SINT: DecodeComponents<i32, 4>(pixel, output, [](i32 v) { return v; }); break;
            case ImageFormat::R32G32B32_SINT: DecodeComponents<i32, 3>(pixel, output, [](i32 v) { return v; }); break;
            case ImageFormat::R32G32_SINT: DecodeComponents<i32, 2>(pixel, output, [](i32 v) { return v; }); break;
            case ImageFormat::R32_SINT: DecodeComponents<i32, 1>(pixel, output, [](i32 v) { return v; }); break;

            case ImageFormat::R16G16B16A16_FLOAT: DecodeComponents<u16, 4>(pixel, output, [](u16 v) { return glm::unpackHalf1x16(v); }); break;
            case ImageFormat::R16G16_FLOAT: DecodeComponents<u16, 2>(pixel, output, [](u16 v) { return glm::unpackHalf1x16(v); }); break;
            case ImageFormat::R16_FLOAT: DecodeComponents<u16, 1>(pixel, output, [](u16 v) { return glm::unpackHalf1x16(v); }); break;

            case ImageFormat::R16G16B16A16_UNORM: DecodeComponents<u16, 4>(pixel, output, [](u16 v) { return v / 65535.0; }); break;
            case ImageFormat::R16G16_UNORM: DecodeComponents<u16, 2>(pixel, output, [](u16 v) { return v / 65535.0; }); break;
            case ImageFormat::D16_UNORM:
            case ImageFormat::R16_UNORM: DecodeComponents<u16, 1>(pixel, output, [](u16 v) { return v / 65535.0; }); break;

            case ImageFormat::R16G16B16A16_UINT: DecodeComponents<u16, 4>(pixel, output, [](u16 v) { return v; }); break;
            case ImageFormat::R16G16_UINT: DecodeComponents<u16, 2>(pixel, output, [](u16 v) { return v; }); break;
            case ImageFormat::R16_UINT: DecodeComponents<u16, 1>(pixel, output, [](u16 v) { return v; }); break;

            case ImageFormat::R16G16B16A16_SNORM: DecodeComponents<i16, 4>(pixel, output, [](i16 v) { return Snorm(v, 32767); }); break;
            case ImageFormat::R16G16_SNORM: DecodeComponents<i16, 2>(pixel, output, [](i16 v) { return Snorm(v, 32767); }); break;
            case ImageFormat::R16_SNORM: DecodeComponents<i16, 1>(pixel, output, [](i16 v) { return Snorm(v, 32767); }); break;

            case ImageFormat::R16G16B16A16_SINT: DecodeComponents<i16, 4>(pixel, output, [](i16 v) { return v; }); break;
            case ImageFormat::R16G16_SINT: DecodeComponents<i16, 2>(pixel, output, [](i16 v) { return v; }); break;
            case ImageFormat::R16_SINT: DecodeComponents<i16, 1>(pixel, output, [](i16 v) { return v; }); break;

            case ImageFormat::R8G8B8A8_UNORM:
            case ImageFormat::R8G8B8A8_UNORM_SRGB: DecodeComponents<u8, 4>(pixel, output, [](u8 v) { return v / 255.0; }); break;
            case ImageFormat::R8G8_UNORM: DecodeComponents<u8, 2>(pixel, output, [](u8 v) { return v / 255.0; }); break;
            case ImageFormat::R8_UNORM: DecodeComponents<u8, 1>(pixel, output, [](u8 v) { return v / 255.0; }); break;

            case ImageFormat::B8G8R8A8_UNORM:
            case ImageFormat::B8G8R8A8_UNORM_SRGB:
                output = { pixel[2] / 255.0, pixel[1] / 255.0, pixel[0] / 255.0, pixel[3] / 255.0 };
                break;

            case ImageFormat::R8G8B8A8_UINT: DecodeComponents<u8, 4>(pixel, output, [](u8 v) { return v; }); break;
            case ImageFormat::B8G8R8A8_UINT:
                output = { static_cast<double>(pixel[2]), static_cast<double>(pixel[1]), static_cast<double>(pixel[0]), static_cast<double>(pixel[3]) };
                break;
            case ImageFormat::R8G8_UINT: DecodeComponents<u8, 2>(pixel, output, [](u8 v) { return v; }); break;
            case ImageFormat::R8_UINT: DecodeComponents<u8, 1>(pixel, output, [](u8 v) { return v; }); break;

            case ImageFormat::R8G8B8A8_SNORM: DecodeComponents<i8, 4>(pixel, output, [](i8 v) { return Snorm(v, 127); }); break;
            case ImageFormat::B8G8R8A8_SNORM:
                output = { Snorm(static_cast<i8>(pixel[2]), 127), Snorm(static_cast<i8>(pixel[1]), 127), Snorm(static_cast<i8>(pixel[0]), 127), Snorm(static_cast<i8>(pixel[3]), 127) };
                break;
            case ImageFormat::R8G8_SNORM: DecodeComponents<i8, 2>(pixel, output, [](i8 v) { return Snorm(v, 127); }); break;
            case ImageFormat::R8_SNORM: DecodeComponents<i8, 1>(pixel, output, [](i8 v) { return Snorm(v, 127); }); break;

            case ImageFormat::R8G8B8A8_SINT: DecodeComponents<i8, 4>(pixel, output, [](i8 v) { return v; }); break;
            case ImageFormat::B8G8R8A8_SINT:
                output = { static_cast<double>(static_cast<i8>(pixel[2])), static_cast<double>(static_cast<i8>(pixel[1])), static_cast<double>(static_cast<i8>(pixel[0])), static_cast<double>(static_cast<i8>(pixel[3])) };
                break;
            case ImageFormat::R8G8_SINT: DecodeComponents<i8, 2>(pixel, output, [](i8 v) { return v; }); break;
            case ImageFormat::R8_SINT: DecodeComponents<i8, 1>(pixel, output, [](i8 v) { return v; }); break;

            case ImageFormat::R10G10B10A2_UNORM:
            case ImageFormat::R10G10B10A2_UINT:
            {
                const u32 packed = Read<u32>(pixel);
                output = {
                    static_cast<double>((packed >> 20) & 0x3ff),
                    static_cast<double>((packed >> 10) & 0x3ff),
                    static_cast<double>(packed & 0x3ff),
                    static_cast<double>((packed >> 30) & 0x3)
                };
                if (format == ImageFormat::R10G10B10A2_UNORM)
                {
                    output[0] /= 1023.0;
                    output[1] /= 1023.0;
                    output[2] /= 1023.0;
                    output[3] /= 3.0;
                }
                break;
            }
            case ImageFormat::R11G11B10_UFLOAT:
            {
                const vec3 unpacked = glm::unpackF2x11_1x10(Read<u32>(pixel));
                output = { unpacked.x, unpacked.y, unpacked.z, 1.0 };
                break;
            }
            default:
                error = "Unsupported render-target format";
                return false;
            }
        }

        return true;
    }

    u8 ToByte(double value)
    {
        if (!std::isfinite(value))
            return 0;
        return static_cast<u8>(std::lround(std::clamp(value, 0.0, 1.0) * 255.0));
    }

    void AppendPngBytes(void* context, void* data, int size)
    {
        auto& output = *static_cast<std::vector<u8>*>(context);
        const auto* bytes = static_cast<const u8*>(data);
        output.insert(output.end(), bytes, bytes + size);
    }

    bool EncodeAndPublish(
        const fs::path& path,
        uvec2 dimensions,
        const std::vector<u8>& rgba,
        std::string& error)
    {
        std::vector<u8> png;
        if (!stbi_write_png_to_func(
            AppendPngBytes,
            &png,
            static_cast<int>(dimensions.x),
            static_cast<int>(dimensions.y),
            4,
            rgba.data(),
            static_cast<int>(dimensions.x * 4)))
        {
            error = "PNG encoding failed";
            return false;
        }

        std::error_code pathError;
        fs::create_directories(path.parent_path(), pathError);
        if (pathError)
        {
            error = "Failed to create artifact directory: " + pathError.message();
            return false;
        }

        fs::path temporaryPath = path;
        temporaryPath += ".tmp";
        {
            std::ofstream stream(temporaryPath, std::ios::binary | std::ios::trunc);
            if (!stream)
            {
                error = "Failed to open temporary artifact";
                return false;
            }
            stream.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
            stream.flush();
            if (!stream)
            {
                error = "Failed to write temporary artifact";
                stream.close();
                fs::remove(temporaryPath, pathError);
                return false;
            }
        }

        if (!ReplaceFile(temporaryPath, path))
        {
            error = "Failed to publish artifact atomically";
            fs::remove(temporaryPath, pathError);
            return false;
        }
        return true;
    }
}

RenderTargetCapture::RenderTargetCapture(Renderer::Renderer* renderer)
    : _renderer(renderer)
{
}

bool RenderTargetCapture::ResolveArtifactPath(
    const fs::path& automationRoot,
    const fs::path& requestedPath,
    fs::path& resolvedPath,
    std::string& error)
{
    return Util::Automation::ResolveArtifactPath(
        automationRoot,
        requestedPath,
        ".png",
        resolvedPath,
        error);
}

bool RenderTargetCapture::FindTarget(
    const std::string& debugName,
    Request& request,
    std::string& error) const
{
    u32 matches = 0;
    for (u32 index = 0; index < _renderer->GetNumImages(); ++index)
    {
        const Renderer::ImageID image(static_cast<Renderer::ImageID::type>(index));
        if (_renderer->GetDesc(image).debugName == debugName)
        {
            request.kind = TargetKind::Color;
            request.image = image;
            ++matches;
        }
    }
    for (u32 index = 0; index < _renderer->GetNumDepthImages(); ++index)
    {
        const Renderer::DepthImageID image(static_cast<Renderer::DepthImageID::type>(index));
        if (_renderer->GetDesc(image).debugName == debugName)
        {
            request.kind = TargetKind::Depth;
            request.depthImage = image;
            ++matches;
        }
    }

    if (matches == 0)
    {
        error = "Render target not found: " + debugName;
        return false;
    }
    if (matches > 1)
    {
        error = "Render target name is ambiguous: " + debugName;
        return false;
    }
    return true;
}

bool RenderTargetCapture::Queue(
    const std::string& debugName,
    const fs::path& artifactPath,
    std::string& error)
{
    if (debugName.empty())
    {
        error = "Render-target debug name must not be empty";
        return false;
    }

    const char* automationRoot = std::getenv("NOVUS_AUTOMATION_ROOT");
    if (!automationRoot || automationRoot[0] == '\0')
    {
        error = "NOVUS_AUTOMATION_ROOT is not configured";
        return false;
    }

    Request request;
    request.debugName = debugName;
    if (!ResolveArtifactPath(automationRoot, artifactPath, request.path, error))
        return false;
    if (!FindTarget(debugName, request, error))
        return false;

    _pending.push_back(std::move(request));
    return true;
}

void RenderTargetCapture::AddReadbackPass(Renderer::RenderGraph& renderGraph)
{
    if (_pending.empty())
        return;

    struct Data
    {
        Renderer::ImageResource image;
        Renderer::DepthImageResource depthImage;
    };
    const TargetKind kind = _pending.front().kind;
    const Renderer::ImageID image = _pending.front().image;
    const Renderer::DepthImageID depthImage = _pending.front().depthImage;
    renderGraph.AddPass<Data>("Render Target Readback",
        [kind, image, depthImage](Data& data, Renderer::RenderGraphBuilder& builder)
        {
            if (kind == TargetKind::Color)
                data.image = builder.Read(image, Renderer::PipelineType::BOTH);
            else
                data.depthImage = builder.Read(depthImage, Renderer::PipelineType::BOTH);
            return true;
        },
        [](Data&, Renderer::RenderGraphResources&, Renderer::CommandList&) {},
        Renderer::RenderPassFlags::SideEffect);
}

bool RenderTargetCapture::ConvertColorToRGBA8(
    Renderer::ImageFormat format,
    uvec2 dimensions,
    const std::vector<u8>& source,
    std::vector<u8>& destination,
    std::string& error)
{
    DecodedImage decoded;
    if (!DecodeColor(format, dimensions, source, decoded, error))
        return false;

    std::array<double, 4> minima = {
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity()
    };
    std::array<double, 4> maxima = {
        -std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity()
    };
    if (decoded.adaptive)
    {
        for (const auto& pixel : decoded.pixels)
        {
            for (u8 component = 0; component < decoded.components; ++component)
            {
                if (!std::isfinite(pixel[component]))
                    continue;
                minima[component] = std::min(minima[component], pixel[component]);
                maxima[component] = std::max(maxima[component], pixel[component]);
            }
        }
    }

    destination.resize(decoded.pixels.size() * 4);
    for (size_t index = 0; index < decoded.pixels.size(); ++index)
    {
        std::array<double, 4> color = decoded.pixels[index];
        if (decoded.adaptive)
        {
            for (u8 component = 0; component < decoded.components; ++component)
            {
                const double range = maxima[component] - minima[component];
                color[component] =
                    std::isfinite(range) && range > std::numeric_limits<double>::epsilon()
                        ? (color[component] - minima[component]) / range
                        : (color[component] == 0.0 ? 0.0 : 1.0);
            }
        }

        if (decoded.components == 1)
            color[1] = color[2] = color[0];
        else if (decoded.components == 2)
            color[2] = 0.0;

        destination[index * 4 + 0] = ToByte(color[0]);
        destination[index * 4 + 1] = ToByte(color[1]);
        destination[index * 4 + 2] = ToByte(color[2]);
        destination[index * 4 + 3] = decoded.preserveAlpha ? ToByte(color[3]) : 255;
    }
    return true;
}

bool RenderTargetCapture::ConvertDepthToRGBA8(
    Renderer::DepthImageFormat format,
    uvec2 dimensions,
    const std::vector<u8>& source,
    std::vector<u8>& destination,
    std::string& error)
{
    const size_t pixelSize = DepthPixelSize(format);
    const u64 pixelCount = static_cast<u64>(dimensions.x) * dimensions.y;
    if (!pixelSize || source.size() != pixelCount * pixelSize)
    {
        error = pixelSize ? "Raw depth-target size does not match its dimensions" : "Unsupported depth-target format";
        return false;
    }

    std::vector<double> values(static_cast<size_t>(pixelCount));
    for (size_t index = 0; index < values.size(); ++index)
    {
        const u8* pixel = source.data() + index * pixelSize;
        switch (format)
        {
        case Renderer::DepthImageFormat::D32_FLOAT_S8X24_UINT:
        case Renderer::DepthImageFormat::D32_FLOAT:
        case Renderer::DepthImageFormat::R32_FLOAT:
            values[index] = Read<f32>(pixel);
            break;
        case Renderer::DepthImageFormat::D24_UNORM_S8_UINT:
            values[index] = (Read<u32>(pixel) & 0x00ffffffu) / static_cast<double>(0x00ffffffu);
            break;
        case Renderer::DepthImageFormat::D16_UNORM:
        case Renderer::DepthImageFormat::R16_UNORM:
            values[index] = Read<u16>(pixel) / 65535.0;
            break;
        default:
            error = "Unsupported depth-target format";
            return false;
        }
    }

    double minimum = std::numeric_limits<double>::infinity();
    double maximum = -std::numeric_limits<double>::infinity();
    for (const double value : values)
    {
        if (std::isfinite(value))
        {
            minimum = std::min(minimum, value);
            maximum = std::max(maximum, value);
        }
    }

    destination.resize(values.size() * 4);
    const double range = maximum - minimum;
    for (size_t index = 0; index < values.size(); ++index)
    {
        const double normalized =
            std::isfinite(range) && range > std::numeric_limits<double>::epsilon()
                ? (maximum - values[index]) / range
                : 1.0 - std::clamp(values[index], 0.0, 1.0);
        const u8 grayscale = ToByte(normalized);
        destination[index * 4 + 0] = grayscale;
        destination[index * 4 + 1] = grayscale;
        destination[index * 4 + 2] = grayscale;
        destination[index * 4 + 3] = 255;
    }
    return true;
}

void RenderTargetCapture::ProcessPending()
{
    if (_pending.empty())
        return;

    Request request = std::move(_pending.front());
    _pending.pop_front();
    Process(std::move(request));
}

void RenderTargetCapture::Process(Request request)
{
    uvec2 dimensions;
    size_t pixelSize = 0;
    std::string format;
    if (request.kind == TargetKind::Color)
    {
        const Renderer::ImageDesc& desc = _renderer->GetDesc(request.image);
        dimensions = _renderer->GetImageDimensions(request.image);
        pixelSize = ColorPixelSize(desc.format);
        format = FormatName(desc.format);
        if (desc.sampleCount != Renderer::SampleCount::SAMPLE_COUNT_1 || desc.depth != 1)
        {
            EmitArtifactMarker("artifact_failed", request.debugName, request.path, dimensions, format, "Multisampled and array render targets are not supported");
            return;
        }
    }
    else
    {
        const Renderer::DepthImageDesc& desc = _renderer->GetDesc(request.depthImage);
        dimensions = _renderer->GetImageDimensions(request.depthImage);
        pixelSize = DepthPixelSize(desc.format);
        format = FormatName(desc.format);
        if (desc.sampleCount != Renderer::SampleCount::SAMPLE_COUNT_1)
        {
            EmitArtifactMarker("artifact_failed", request.debugName, request.path, dimensions, format, "Multisampled depth targets are not supported");
            return;
        }
    }

    const u64 byteCount = static_cast<u64>(dimensions.x) * dimensions.y * pixelSize;
    if (!pixelSize || byteCount == 0 || byteCount > MaxCaptureBytes)
    {
        EmitArtifactMarker("artifact_failed", request.debugName, request.path, dimensions, format,
            pixelSize ? "Render target exceeds the 512 MiB capture limit" : "Unsupported render-target format");
        return;
    }

    std::vector<u8> source(static_cast<size_t>(byteCount));
    const bool read = request.kind == TargetKind::Color
        ? _renderer->ReadImageImmediate(request.image, source.data(), source.size())
        : _renderer->ReadImageImmediate(request.depthImage, source.data(), source.size());
    if (!read)
    {
        EmitArtifactMarker("artifact_failed", request.debugName, request.path, dimensions, format, "GPU readback failed");
        return;
    }

    std::vector<u8> rgba;
    std::string error;
    const bool converted = request.kind == TargetKind::Color
        ? ConvertColorToRGBA8(_renderer->GetDesc(request.image).format, dimensions, source, rgba, error)
        : ConvertDepthToRGBA8(_renderer->GetDesc(request.depthImage).format, dimensions, source, rgba, error);
    if (!converted || !EncodeAndPublish(request.path, dimensions, rgba, error))
    {
        EmitArtifactMarker("artifact_failed", request.debugName, request.path, dimensions, format, error);
        return;
    }

    EmitArtifactMarker("artifact_ready", request.debugName, request.path, dimensions, format);
}
