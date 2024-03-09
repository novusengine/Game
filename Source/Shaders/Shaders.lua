local dependencies = { }
local defines = { }
ProjectTemplate("Shaders", "Utility", nil, Game.binDir, dependencies, defines)
dependson ("ShaderCookerStandalone")

local rootDir = path.getabsolute("Shaders/", Game.projectsDir)
local sourceDir = path.getabsolute("Shaders/", rootDir)

local files =
{
    sourceDir .. "/main.cpp",
    sourceDir .. "/**.hlsl",
}

AddFiles(files)

filter("files:**.hlsl")
    flags("ExcludeFromBuild")

filter { }
local shaderCookerStandalonePath = (Game.binDir .. "/%{cfg.buildcfg}/ShaderCookerStandalone.exe")
local shaderOutputPath = (Game.binDir .. "/%{cfg.buildcfg}/Data/Shaders")

prebuildmessage ("Compiling Shaders...")
prebuildcommands { (shaderCookerStandalonePath) .. " " .. (sourceDir) .. " " .. (shaderOutputPath) }

BuildSettings:Add("Shader Source Dir", sourceDir)