local dependencies = { "base", "shadercooker" }
local defines = { }
ProjectTemplate("ShaderCookerStandalone", "ConsoleApp", ".", Game.binDir, dependencies, defines)

local dxCompilerLibPath = BuildSettings:Get("DXCompiler Dynamic Lib Path")
if dxCompilerLibPath == nil then
  error("Failed to find DXCompiler Dynamic Lib Path, this setting is supposed to be set during the creation of dxcompiler")
end

postbuildcommands { "{COPYFILE} " .. dxCompilerLibPath .. " " .. Game.binDir .. "/%{cfg.buildcfg}/dxcompiler.dll" }