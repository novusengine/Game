-- Dependencies are order sensitive on Linux, keep that in mind when adding new dependencies.
local mod = Solution.Util.CreateModuleTable("Game-Lib", { "renderer", "fileformat", "scripting", "gameplay", "input", "meta", "luau-analysis", "luau-compiler", "luau-vm", "luau-codegen", "enkits", "utfcpp", "base64", "jolt", "spake2-ee" })

Solution.Util.CreateStaticLib(mod.Name, Solution.Projects.Current.BinDir, mod.Dependencies, function()
    local defines = { "_SILENCE_ALL_CXX17_DEPRECATION_WARNINGS", "_SILENCE_ALL_MS_EXT_DEPRECATION_WARNINGS"}
    dependson("Shaders")
    
    Solution.Util.SetLanguage("C++")
    Solution.Util.SetCppDialect(20)

    local files = Solution.Util.GetFilesForCpp(mod.Path)
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
