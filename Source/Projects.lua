-- Projects
Game.projectsDir = path.getabsolute("Source/", Game.rootDir)

print("-- Creating Modules --")

if (Game.isRoot) then
    group "[Build System]"
    filter { }
    include("Generate/Generate.lua")
end

group (Game.name .. "/[Modules]")

local modules =
{
    "ShaderCookerStandalone/ShaderCookerStandalone.lua",
    "Shaders/Shaders.lua",
    "Game/Game.lua",
    "UnitTests/UnitTests.lua",
}

for k,v in pairs(modules) do
    filter { }
    include(v)
end

filter { }
group (Game.name)

print("-- Finished with Modules --\n")