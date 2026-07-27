#include <Game-Lib/Util/AutomationUtil.h>

#include <catch2/catch2.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>

namespace
{
    class TemporaryAutomationRoot
    {
    public:
        TemporaryAutomationRoot()
        {
            _path = std::filesystem::temp_directory_path() /
                ("novus-automation-" + std::to_string(
                    std::chrono::steady_clock::now().time_since_epoch().count()));
            std::filesystem::create_directories(_path / "Scripts");
            std::ofstream(_path / "Scripts" / "smoke.luau") << "print(\"smoke\")";
            std::filesystem::create_directories(_path / "Outside");
            std::ofstream(_path / "Outside" / "escape.luau") << "print(\"escape\")";
        }

        ~TemporaryAutomationRoot()
        {
            std::error_code error;
            std::filesystem::remove_all(_path, error);
        }

        const std::filesystem::path& GetPath() const { return _path; }

    private:
        std::filesystem::path _path;
    };
}

TEST_CASE("Automation scripts resolve below the configured Scripts root")
{
    TemporaryAutomationRoot root;
    std::filesystem::path resolved;
    std::string error;

    REQUIRE(Util::Automation::ResolveScriptPath(
        root.GetPath(),
        "Scripts/smoke.luau",
        resolved,
        error));
    CHECK(resolved.filename() == "smoke.luau");
}

TEST_CASE("Automation scripts reject traversal and non-Luau files")
{
    TemporaryAutomationRoot root;
    std::filesystem::path resolved;
    std::string error;

    CHECK_FALSE(Util::Automation::ResolveScriptPath(
        root.GetPath(),
        "Scripts/../Outside/escape.luau",
        resolved,
        error));
    CHECK(error.find("escapes") != std::string::npos);

    CHECK_FALSE(Util::Automation::ResolveScriptPath(
        root.GetPath(),
        "Scripts/smoke.lua",
        resolved,
        error));
}
