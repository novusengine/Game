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

    vpaths {
        ["/*"] = { "*.lua", "*.cpp" }
    }
end)
