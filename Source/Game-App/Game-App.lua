local mod = Solution.Util.CreateModuleTable("Game-App", { "game-lib" })

Solution.Util.CreateConsoleApp(mod.Name, Solution.Projects.Current.BinDir, mod.Dependencies, function()
    local defines = { "_SILENCE_ALL_CXX17_DEPRECATION_WARNINGS", "_SILENCE_ALL_MS_EXT_DEPRECATION_WARNINGS" }
    
    Solution.Util.SetLanguage("C++")
    Solution.Util.SetCppDialect(20)

    local projFile = mod.Path .. "/" .. mod.Name .. ".lua"
    local files = Solution.Util.GetFilesForCpp(mod.Path)
    table.insert(files, projFile)
    table.insert(files, mod.Path .. "/Game-App/Resources/renderdoc.json")

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

        postbuildcommands
        {
            '{COPYFILE} "' .. mod.Path .. '/Game-App/Resources/renderdoc.json" "%{cfg.targetdir}/renderdoc.json"'
        }

        vpaths 
        {
            ['Resources/*'] = { '*.rc', '**.ico', '**.json' },
            ["/*"] = { "*.lua", mod.Name .. "/**" }
        }
    end)
end)
