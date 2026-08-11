local mod = Solution.Util.CreateModuleTable("ShaderCookerStandalone", { "shadercooker", "base" })

Solution.Util.CreateConsoleApp(mod.Name, Solution.Projects.Current.BinDir, mod.Dependencies, function()
    local defines = { "_SILENCE_ALL_CXX17_DEPRECATION_WARNINGS", "_SILENCE_ALL_MS_EXT_DEPRECATION_WARNINGS", "SLANG_STATIC" }

    Solution.Util.SetLanguage("C++")
    Solution.Util.SetCppDialect(20)

    local projFile = mod.Path .. "/" .. mod.Name .. ".lua"
    local files = Solution.Util.GetFilesForCpp(mod.Path)
    table.insert(files, projFile)

    Solution.Util.SetFiles(files)
    Solution.Util.SetIncludes(mod.Path)
    Solution.Util.SetDefines(defines)

    if os.target() ~= "windows" then
        -- premake's gmake backend generates no build target for Utility projects, so the
        -- Shaders project's prebuild cook never runs there; cook after building the cooker instead
        local shaderSourceDir = path.getabsolute(mod.Path .. "/../Shaders/Shaders")
        local shaderOutputPath = (Solution.Projects.Current.BuildDir .. "/Data/Shaders")
        postbuildmessage ("Compiling Shaders...")
        postbuildcommands { "%{cfg.buildtarget.abspath} " .. shaderSourceDir .. " " .. shaderOutputPath }
    end

    vpaths {
        ["/*"] = { "*.lua", "*.cpp" }
    }
end)
