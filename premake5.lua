Game = { }
Game.name = "Game"
Game.rootDir = ""
Game.buildDir = ""
Game.binDir = ""
Game.isRoot = false

Game.Init = function(self, rootDir, buildDir, binDir)
    self.rootDir = rootDir
    self.buildDir = buildDir
    self.binDir = binDir .. "/" .. self.name

    workspace (self.name)
        location (buildDir)
        configurations { "Debug", "RelDebug", "Release" }
		startproject "Game"

        filter "system:Windows"
            system "windows"
            platforms "Win64"

        filter "system:Unix"
            system "linux"
            platforms "Linux"

    local projectUtils = path.getabsolute("Premake/ProjectUtil.lua", rootDir)
    include(projectUtils)

    local buildSettings = path.getabsolute("Premake/BuildSettings.lua", rootDir)
    local silentFailOnDuplicateSetting = false
    InitBuildSettings(silentFailOnDuplicateSetting)
    include(buildSettings)

    IncludeSubmodule("Engine", rootDir, binDir)

    print("-- Configuring (" .. self.name .. ") --\n")
    print(" Root Directory : " .. rootDir)
    print(" Build Directory : " .. buildDir)
    print(" Bin Directory : " .. binDir)
    print("--\n")

    local deps = path.getabsolute("Dependencies/Dependencies.lua", rootDir)
    local projects = path.getabsolute("Source/Projects.lua", rootDir)
    include(deps)
    include(projects)

    print("-- Done (" .. self.name .. ") --")
end

if HasRoot == nil then
    HasRoot = true

    local rootDir = path.getabsolute(".")
    local buildDir = path.getabsolute("Build/", rootDir)
    local binDir = path.getabsolute("Bin/", buildDir)

    Game.isRoot = true
    Game:Init(rootDir, buildDir, binDir)
end