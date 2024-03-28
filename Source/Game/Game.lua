local basePath = path.getabsolute("Game/", Game.projectsDir)

local dependencies = { "base", "fileformat", "input", "network", "renderer", "luau-compiler", "luau-vm", "jolt", "enkiTS", "refl-cpp", "utfcpp", "base64" }
local defines = { "_SILENCE_ALL_CXX17_DEPRECATION_WARNINGS", "_SILENCE_ALL_MS_EXT_DEPRECATION_WARNINGS", "WIN32_LEAN_AND_MEAN", "NOMINMAX" }

local shaderSourceDir = BuildSettings:Get("Shader Source Dir")
if shaderSourceDir == nil then
  error("Failed to find Shader Source Dir, this setting is supposed to be set during the creation of the shaders project")
end

local function SetupLib()
  ProjectTemplate("Game", "StaticLib", nil, Game.binDir, dependencies, defines)

  local sourceDir = path.getabsolute("Game", basePath)
  local includeDir = { basePath }

  local files =
  {
    sourceDir .. "/**.h",
    sourceDir .. "/**.hpp",
    sourceDir .. "/**.c",
    sourceDir .. "/**.cpp"
  }

  AddFiles(files)
  AddIncludeDirs(includeDir)
  AddDefines("SHADER_SOURCE_DIR=\"" .. shaderSourceDir .. "\"")
end
SetupLib()

local function Include()
  local includeDir = path.getabsolute("Game/", Game.projectsDir)

  AddIncludeDirs(includeDir)
  AddDefines({"WIN32_LEAN_AND_MEAN", "NOMINMAX", ("SHADER_SOURCE_DIR=\"" .. shaderSourceDir .. "\"")})
  AddLinks("Game")
end
CreateDep("game", Include, dependencies)

ProjectTemplate("Game-App", "ConsoleApp", nil, Game.binDir, { "game" }, defines)

AddFiles({basePath .. "/main.cpp"})
vpaths { [""] = "**.cpp" }

filter { 'system:Windows' }
  files { 'appicon.rc', '**.ico' }
  vpaths { ['Resources/*'] = { '*.rc', '**.ico' } }