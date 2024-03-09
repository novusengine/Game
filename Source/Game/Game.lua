local dependencies = { "base", "fileformat", "input", "network", "renderer", "luau-compiler", "luau-vm", "jolt", "enkiTS", "refl-cpp", "utfcpp", "base64" }
local defines = { "_SILENCE_ALL_CXX17_DEPRECATION_WARNINGS", "_SILENCE_ALL_MS_EXT_DEPRECATION_WARNINGS", "WIN32_LEAN_AND_MEAN", "NOMINMAX" }
ProjectTemplate("Game", "ConsoleApp", ".", Game.binDir, dependencies, defines)

local shaderSourceDir = BuildSettings:Get("Shader Source Dir")
if shaderSourceDir == nil then
  error("Failed to find Shader Source Dir, this setting is supposed to be set during the creation of the shaders project")
end

AddDefines("SHADER_SOURCE_DIR=\"" .. shaderSourceDir .. "\"")

filter { 'system:Windows' }
  files { 'appicon.rc', '**.ico' }
  vpaths { ['Resources/*'] = { '*.rc', '**.ico' } }

local function Include()
  local includeDir = path.getabsolute("Game/", Game.projectsDir)
  local sourceDir = path.getabsolute("Game/", includeDir)

  local files =
  {
    (sourceDir .. "/**.h"),
    (sourceDir .. "/**.hpp"),
    (sourceDir .. "/**.cpp")
  }

  AddFiles(files)
  AddIncludeDirs(includeDir)
  AddDefines({"WIN32_LEAN_AND_MEAN", "NOMINMAX", ("SHADER_SOURCE_DIR=\"" .. shaderSourceDir .. "\"")})
end
CreateDep("game", Include, dependencies)