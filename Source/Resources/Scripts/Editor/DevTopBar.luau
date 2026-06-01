--[[
DevTopBar — dev menu bar drawn below the ImGui main menu during the
ImGui->Lua migration. Greyed entries are not yet ported.
]]

local MenuBar = require("@src/API/UI/MenuBar")
local EditorRegistry = require("@src/API/UI/EditorRegistry")

-- TODO: query ImGui::GetFrameHeight() instead of hardcoding 11.
local IMGUI_MENU_BAR_HEIGHT = 11
local BAR_LAYER = 1000
local BAR_HEIGHT = 22
local CANVAS_W = 1920
local CANVAS_H = 1080

local canvas = UI.GetCanvas("DevTopBar", 0, 0, CANVAS_W, CANVAS_H, false)

local bar = MenuBar.NewMenuBar(canvas, 0, -IMGUI_MENU_BAR_HEIGHT, CANVAS_W, BAR_HEIGHT, BAR_LAYER, {
    paddingLeft = 8,
    spacing = 4,
    menus = {
        { label = "Debug", items = {
            { label = "Reload Scripts", enabled = false },
            { label = "Reload Shaders", enabled = false },
        }},
        { label = "Window", items = {
            { label = "Camera Info", enabled = true, onClick = function() EditorRegistry.Toggle("CameraInfo") end },
            { label = "Clock", enabled = true, onClick = function() EditorRegistry.Toggle("Clock") end },
            { label = "Map", enabled = true, onClick = function() EditorRegistry.Toggle("Map") end },
            { label = "Hierarchy", enabled = true, onClick = function() EditorRegistry.Toggle("Hierarchy") end },
            { label = "Inspector", enabled = true, onClick = function() EditorRegistry.Toggle("Inspector") end },
            { label = "Asset Browser", enabled = true, onClick = function() EditorRegistry.Toggle("AssetBrowser") end },
        }},
        { label = "Editor", items = {
            { label = "(none)", enabled = false },
        }},
        { label = "Theme", items = {
            { label = "(none)", enabled = false },
        }},
        { label = "Help", items = {
            { label = "(none)", enabled = false },
        }},
    },
})
bar:SetAnchor(0.0, 1.0)
bar:SetRelativePoint(0.0, 1.0)
