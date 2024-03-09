local dependencies = { }
local defines = { }
ProjectTemplate("Generate", "StaticLib", ".", Game.binDir, dependencies, defines)

local solutionType = BuildSettings:Get("Solution Type")
postbuildcommands
{
    "cd " .. Game.rootDir,
    "premake5 " .. solutionType
}