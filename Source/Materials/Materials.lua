local mod = Solution.Util.CreateModuleTable("Materials", {})

Solution.Util.CreateProject(mod.Name, "Utility", Solution.Projects.Current.BinDir, mod.Dependencies, function()
    dependson { "Shaders" }

    if os.target() == "windows" then
        fastuptodate "Off"
    end

    local shaderSourceDir = Solution.Projects.Current.ModulesDir .. "/Shaders/Shaders"
    local manifestPath = Solution.Projects.Current.BuildDir .. "/Data/Pact/material_program_manifest.json"
    local shaderOutputPath = Solution.Projects.Current.BuildDir .. "/Data/Shaders"
    local reportPath = shaderOutputPath .. "/Materials.cook.json"
    local materialPackPath = shaderOutputPath .. "/Materials.matpack"
    local projectFile = mod.Path .. "/" .. mod.Name .. ".lua"

    Solution.Util.SetFiles {
        projectFile,
        shaderSourceDir .. "/Material/Authored/**.mat.slang"
    }

    local cookerPath = Solution.Projects.Current.BinDir ..
        "/%{cfg.buildcfg}/ShaderCookerStandalone.%{systemToExecutableExtensionMap[cfg.system]}"
    local command = '"' .. cookerPath .. '"' ..
        ' --material-cook "' .. manifestPath .. '"' ..
        ' "' .. reportPath .. '"' ..
        ' --material-pack "' .. materialPackPath .. '"' ..
        ' "' .. shaderSourceDir .. '"' ..
        ' "' .. shaderOutputPath .. '"'

    prebuildmessage "Compiling Materials..."
    prebuildcommands { command }

    Solution.Util.SetFilter("files:**.mat.slang", function()
        flags "ExcludeFromBuild"
    end)

    vpaths {
        ["/*"] = { projectFile },
        ["Authored/*"] = { shaderSourceDir .. "/Material/Authored/**" }
    }
end)
