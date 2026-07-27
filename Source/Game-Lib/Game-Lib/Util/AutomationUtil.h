#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace Scripting
{
    class LuaManager;
}

namespace Util::Automation
{
    bool ResolveArtifactPath(
        const std::filesystem::path& automationRoot,
        const std::filesystem::path& requestedPath,
        std::string_view requiredExtension,
        std::filesystem::path& resolvedPath,
        std::string& error);

    bool ResolveScriptPath(
        const std::filesystem::path& automationRoot,
        const std::filesystem::path& requestedPath,
        std::filesystem::path& resolvedPath,
        std::string& error);

    bool ExecuteScript(
        Scripting::LuaManager& luaManager,
        const std::string& requestId,
        const std::string& requestedPath);
}
