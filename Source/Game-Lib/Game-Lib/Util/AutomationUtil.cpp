#include "AutomationUtil.h"

#include <Base/Util/DebugHandler.h>

#include <Scripting/LuaManager.h>
#include <Scripting/Zenith.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <ranges>

namespace fs = std::filesystem;

namespace
{
    constexpr std::uintmax_t MaxAutomationScriptSize = 8 * 1024 * 1024;

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

    bool IsValidRequestId(const std::string& requestId)
    {
        if (requestId.empty() || requestId.size() > 128)
            return false;
        return std::ranges::all_of(requestId, [](unsigned char character)
        {
            return std::isalnum(character) != 0 ||
                character == '-' ||
                character == '_' ||
                character == '.' ||
                character == ':';
        });
    }

    void EmitMarker(
        const std::string& requestId,
        const char* event,
        const std::string& script,
        const std::string& error = "")
    {
        std::string marker =
            "NOVUS_AUTOMATION {\"requestId\":\"" + EscapeJson(requestId) +
            "\",\"event\":\"" + event +
            "\",\"script\":\"" + EscapeJson(script) + "\"";
        if (!error.empty())
            marker += ",\"error\":\"" + EscapeJson(error) + "\"";
        marker += "}";
        NC_LOG_INFO("{}", marker);
    }

    bool IsBelow(const fs::path& root, const fs::path& candidate)
    {
        auto rootIterator = root.begin();
        auto candidateIterator = candidate.begin();
        while (rootIterator != root.end())
        {
            if (candidateIterator == candidate.end() || *rootIterator != *candidateIterator)
                return false;
            ++rootIterator;
            ++candidateIterator;
        }
        return true;
    }
}

namespace Util::Automation
{
    bool ResolveArtifactPath(
        const fs::path& automationRoot,
        const fs::path& requestedPath,
        std::string_view requiredExtension,
        fs::path& resolvedPath,
        std::string& error)
    {
        if (automationRoot.empty() || !automationRoot.is_absolute())
        {
            error = "NOVUS_AUTOMATION_ROOT must be an absolute path";
            return false;
        }
        if (requestedPath.empty() || requestedPath.is_absolute())
        {
            error = "Artifact path must be relative";
            return false;
        }
        if (requestedPath.extension() != requiredExtension)
        {
            error = "Artifact path must use the " + std::string(requiredExtension) + " extension";
            return false;
        }

        const fs::path lexicalRoot = (automationRoot / "Artifacts").lexically_normal();
        std::error_code pathError;
        fs::path artifactRoot = fs::weakly_canonical(lexicalRoot, pathError);
        if (pathError)
        {
            pathError.clear();
            artifactRoot = lexicalRoot;
        }

        resolvedPath = fs::weakly_canonical(artifactRoot / requestedPath, pathError);
        if (pathError)
            resolvedPath = (artifactRoot / requestedPath).lexically_normal();

        if (!IsBelow(artifactRoot, resolvedPath))
        {
            error = "Artifact path escapes the configured Artifacts root";
            return false;
        }
        return true;
    }

    bool ResolveScriptPath(
        const fs::path& automationRoot,
        const fs::path& requestedPath,
        fs::path& resolvedPath,
        std::string& error)
    {
        if (automationRoot.empty() || !automationRoot.is_absolute())
        {
            error = "NOVUS_AUTOMATION_ROOT must be an absolute path";
            return false;
        }
        if (requestedPath.empty() || requestedPath.is_absolute())
        {
            error = "Script path must be relative";
            return false;
        }
        if (requestedPath.extension() != ".luau")
        {
            error = "Script path must use the .luau extension";
            return false;
        }

        auto firstComponent = requestedPath.begin();
        if (firstComponent == requestedPath.end() || *firstComponent != "Scripts")
        {
            error = "Script path must be below Scripts";
            return false;
        }

        std::error_code pathError;
        const fs::path scriptsRoot = fs::weakly_canonical(automationRoot / "Scripts", pathError);
        if (pathError)
        {
            error = "Failed to resolve Scripts root: " + pathError.message();
            return false;
        }

        resolvedPath = fs::canonical(automationRoot / requestedPath, pathError);
        if (pathError)
        {
            error = "Failed to resolve script: " + pathError.message();
            return false;
        }
        if (!IsBelow(scriptsRoot, resolvedPath))
        {
            error = "Script path escapes the configured Scripts root";
            return false;
        }
        if (!fs::is_regular_file(resolvedPath, pathError) || pathError)
        {
            error = "Script path is not a regular file";
            return false;
        }
        if (fs::file_size(resolvedPath, pathError) > MaxAutomationScriptSize || pathError)
        {
            error = pathError ? "Failed to inspect script size" : "Script exceeds the 8 MiB size limit";
            return false;
        }
        return true;
    }

    bool ExecuteScript(
        Scripting::LuaManager& luaManager,
        const std::string& requestId,
        const std::string& requestedPath)
    {
        if (!IsValidRequestId(requestId))
        {
            NC_LOG_ERROR("Invalid automation request ID");
            return false;
        }

        const char* automationRootRaw = std::getenv("NOVUS_AUTOMATION_ROOT");
        if (!automationRootRaw || automationRootRaw[0] == '\0')
        {
            EmitMarker(requestId, "failed", requestedPath, "NOVUS_AUTOMATION_ROOT is not configured");
            return false;
        }

        fs::path resolvedPath;
        std::string error;
        if (!ResolveScriptPath(automationRootRaw, requestedPath, resolvedPath, error))
        {
            EmitMarker(requestId, "failed", requestedPath, error);
            return false;
        }

        std::ifstream stream(resolvedPath, std::ios::binary);
        std::string source{
            std::istreambuf_iterator<char>(stream),
            std::istreambuf_iterator<char>()};
        if (!stream && !stream.eof())
        {
            EmitMarker(requestId, "failed", requestedPath, "Failed to read script");
            return false;
        }

        const auto key = Scripting::ZenithInfoKey::MakeGlobal(0, 0);
        Scripting::Zenith* zenith = luaManager.GetZenithStateManager().Get(key);
        if (!zenith)
        {
            EmitMarker(requestId, "failed", requestedPath, "Global Luau state is unavailable");
            return false;
        }

        EmitMarker(requestId, "started", requestedPath);
        const bool succeeded = luaManager.DoString(zenith, source);
        EmitMarker(requestId, succeeded ? "succeeded" : "failed", requestedPath,
            succeeded ? "" : "Luau execution failed");
        return succeeded;
    }
}
