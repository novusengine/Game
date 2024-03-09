-- Dependencies
Game.dependencyDir = path.getabsolute("Dependencies/", Game.rootDir)

print("-- Creating Dependencies --")

Game.dependencyGroup = (Game.name .. "/Dependencies")
group (Game.dependencyGroup)

local dependencies =
{
    "jolt/jolt.lua",
}

for k,v in pairs(dependencies) do
    filter { }
    include(v)
end

filter { }
group (Game.name)

print("-- Finished with Dependencies --\n")