local dependencies = { "base", "shadercooker" }
local defines = { "_SILENCE_ALL_CXX17_DEPRECATION_WARNINGS", "_SILENCE_ALL_MS_EXT_DEPRECATION_WARNINGS" }
ProjectTemplate("ShaderCookerStandalone", "ConsoleApp", ".", Game.binDir, dependencies, defines)

local dxCompilerLibPath = BuildSettings:Get("DXCompiler Dynamic Lib Path")
if dxCompilerLibPath == nil then
  error("Failed to find DXCompiler Dynamic Lib Path, this setting is supposed to be set during the creation of dxcompiler")
end

postbuildcommands { "{COPYFILE} " .. dxCompilerLibPath .. " " .. Game.binDir .. "/%{cfg.buildcfg}/dxcompiler.dll" }