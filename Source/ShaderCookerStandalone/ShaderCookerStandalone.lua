local mod = Solution.Util.CreateModuleTable("ShaderCookerStandalone", { "shadercooker", "base" })

Solution.Util.CreateConsoleApp(mod.Name, Solution.Projects.Current.BinDir, mod.Dependencies, function()
    local dxCompilerLibPath = BuildSettings:Get("DXCompiler Dynamic Lib Path")
    if dxCompilerLibPath == nil then
      Solution.Util.PrintError("Failed to find DXCompiler Dynamic Lib Path, this setting is supposed to be set during the setup of dxcompiler in premake")
    end

    local defines = { "_SILENCE_ALL_CXX17_DEPRECATION_WARNINGS", "_SILENCE_ALL_MS_EXT_DEPRECATION_WARNINGS" }

    Solution.Util.SetLanguage("C++")
    Solution.Util.SetCppDialect(20)

    local files = Solution.Util.GetFilesForCpp(mod.Path)
    Solution.Util.SetFiles(files)
    Solution.Util.SetIncludes(mod.Path)
    Solution.Util.SetDefines(defines)
    if os.target() == "windows" then
        postbuildcommands { "{COPYFILE} " .. dxCompilerLibPath .. " " .. Solution.Projects.Current.BinDir .. "/%{cfg.buildcfg}/dxcompiler.%{systemToDynamicLibExtensionMap[cfg.system]}" }
    end
end)
