local mod = Solution.Util.CreateModuleTable("Shaders", {})

Solution.Util.CreateProject(mod.Name, "Utility", Solution.Projects.Current.BinDir, mod.Dependencies, function()
    dependson { "ShaderCookerStandalone" }

    if os.target() == "windows" then
        fastuptodate "Off"
    end

    local sourceDir = mod.Path .. "/Shaders"
    local files =
    {
        sourceDir .. "/main.cpp",
        sourceDir .. "/**.slang",
        mod.Path .. "/" .. mod.Name .. ".lua"
    }
    Solution.Util.SetFiles(files)

    local shaderCookerStandalonePath = (Solution.Projects.Current.BinDir .. "/%{cfg.buildcfg}/ShaderCookerStandalone.%{systemToExecutableExtensionMap[cfg.system]}")
    local shaderOutputPath = (Solution.Projects.Current.BuildDir .. "/Data/Shaders")

    prebuildmessage ("Compiling Shaders...")
    prebuildcommands { (shaderCookerStandalonePath) .. " " .. (sourceDir) .. " " .. (shaderOutputPath) }
    
    BuildSettings:Add("Shader Source Dir", sourceDir)

    Solution.Util.SetFilter("files:**.slang", function()
        flags("ExcludeFromBuild")
    end)

    vpaths {
        ["/*"] = { "*.lua", mod.Name .. "/**" }
    }
end)