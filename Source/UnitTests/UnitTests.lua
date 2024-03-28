if BuildSettings:Get("Build UnitTests") == false then return end

local dependencies = { "game", "catch2", "catch2-withmain" }
local defines = { "_SILENCE_ALL_CXX17_DEPRECATION_WARNINGS", "_SILENCE_ALL_MS_EXT_DEPRECATION_WARNINGS" }
ProjectTemplate("Game-Tests", "ConsoleApp", ".", Game.binDir, dependencies, defines)
vpaths { ["Tests"] = "**" }