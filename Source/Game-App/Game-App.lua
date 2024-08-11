local mod = Solution.Util.CreateModuleTable("Game-App", { "game" })

Solution.Util.CreateConsoleApp(mod.Name, Solution.Projects.Current.BinDir, mod.Dependencies, function()
    local defines = { "_SILENCE_ALL_CXX17_DEPRECATION_WARNINGS", "_SILENCE_ALL_MS_EXT_DEPRECATION_WARNINGS" }
    
    Solution.Util.SetLanguage("C++")
    Solution.Util.SetCppDialect(20)
  
    local files = Solution.Util.GetFilesForCpp(mod.Path)
    Solution.Util.SetFiles(files)
    Solution.Util.SetIncludes(mod.Path)
    Solution.Util.SetDefines(defines)
    
    Solution.Util.SetFilter("system:Windows", function()
        local appIconFiles =
        {
            "appicon.rc",
            "**.ico"
        }
        Solution.Util.SetFiles(appIconFiles)

        vpaths 
        {
            ['Resources/*'] = { '*.rc', '**.ico' },
            ['*'] = { '**.*' }
        }
    end)
end)