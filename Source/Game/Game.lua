local mod = Solution.Util.CreateModuleTable("Game", { "base", "fileformat", "input", "network", "renderer", "luau-compiler", "luau-vm", "jolt", "enkits", "refl-cpp", "utfcpp", "base64", "fidelityfx" })

Solution.Util.CreateStaticLib(mod.Name, Solution.Projects.Current.BinDir, mod.Dependencies, function()
    local defines = { "_SILENCE_ALL_CXX17_DEPRECATION_WARNINGS", "_SILENCE_ALL_MS_EXT_DEPRECATION_WARNINGS", "WIN32_LEAN_AND_MEAN", "NOMINMAX" }

    Solution.Util.SetLanguage("C++")
    Solution.Util.SetCppDialect(20)

    local files = Solution.Util.GetFilesForCpp(mod.Path .. "/Game")
    Solution.Util.SetFiles(files)
    Solution.Util.SetIncludes(mod.Path)
    Solution.Util.SetDefines(defines)

    local shaderSourceDir = BuildSettings:Get("Shader Source Dir")
    if shaderSourceDir == nil then
      Solution.Util.PrintError("Failed to find Shader Source Dir, this setting is supposed to be set during the creation of the shaders project")
    else
      Solution.Util.SetDefines("SHADER_SOURCE_DIR=\"" .. shaderSourceDir .. "\"")
    end
end)

Solution.Util.CreateDep(mod.NameLow, mod.Dependencies, function()
    Solution.Util.SetIncludes(mod.Path)
    Solution.Util.SetLinks(mod.Name)
    
    Solution.Util.SetFilter("platforms:Win64", function()
        Solution.Util.SetDefines({"WIN32_LEAN_AND_MEAN", "NOMINMAX"})
    end)
end)

local gameAppMod = Solution.Util.CreateModuleTable("Game-App", { "game" })

Solution.Util.CreateConsoleApp(gameAppMod.Name, Solution.Projects.Current.BinDir, gameAppMod.Dependencies, function()
    local defines = { "_SILENCE_ALL_CXX17_DEPRECATION_WARNINGS", "_SILENCE_ALL_MS_EXT_DEPRECATION_WARNINGS" }
    
    Solution.Util.SetLanguage("C++")
    Solution.Util.SetCppDialect(20)
  
    local files =
    {
        mod.Path .. "/main.cpp"
    }
    Solution.Util.SetFiles(files)
    Solution.Util.SetDefines(defines)
    
    vpaths { [""] = "**.cpp" }
    
    Solution.Util.SetFilter("system:Windows", function()
        local appIconFiles =
        {
            "appicon.rc",
            "**.ico"
        }
        Solution.Util.SetFiles(appIconFiles)
        vpaths { ['Resources/*'] = { '*.rc', '**.ico' } }
    end)
end)