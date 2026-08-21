--[[
DevTopBar - dev menu bar drawn below the ImGui main menu during the
ImGui->Lua migration. Greyed entries are not yet ported.
]]

local MenuBar = require("@src/API/Editor/UI/MenuBar")
local EditorRegistry = require("@src/API/Editor/Registry")

-- TODO: query ImGui::GetFrameHeight() instead of hardcoding 11.
local IMGUI_MENU_BAR_HEIGHT = 25
local BAR_LAYER = 1000i
local BAR_HEIGHT = 22
local CANVAS_W = 1920
local CANVAS_H = 1080

local canvas = UI.GetCanvas("DevTopBar", vec.New(0, 0), vec.New(CANVAS_W, CANVAS_H), false)

local bar = MenuBar.New(canvas, vec.New(0, -IMGUI_MENU_BAR_HEIGHT), vec.New(CANVAS_W, BAR_HEIGHT), BAR_LAYER, {
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
            { label = "Map Editor", enabled = true, onClick = function() EditorRegistry.Toggle("MapEditor") end },
            { label = "Hierarchy", enabled = true, onClick = function() EditorRegistry.Toggle("Hierarchy") end },
            { label = "Inspector", enabled = true, onClick = function() EditorRegistry.Toggle("Inspector") end },
            { label = "Asset Browser", enabled = true, onClick = function() EditorRegistry.Toggle("AssetBrowser") end },
            { label = "Console Variables", enabled = true, onClick = function() EditorRegistry.Toggle("CVarEditor") end },
            { label = "Spell Editor", enabled = true, onClick = function() EditorRegistry.Toggle("SpellEditor") end },
            { label = "Creature AI", enabled = true, onClick = function() EditorRegistry.Toggle("CreatureAI") end },
            { label = "Localized Text", enabled = true, onClick = function() EditorRegistry.Toggle("LocalizedTextEditor") end },
            { label = "Conditions", enabled = true, onClick = function() EditorRegistry.Toggle("ConditionEditor") end },
            { label = "Gossip Flow", enabled = true, onClick = function() EditorRegistry.Toggle("GossipFlowEditor") end },
            { label = "Creature Interactions", enabled = true, onClick = function() EditorRegistry.Toggle("CreatureInteractionEditor") end },
            { label = "Terrain Editor", enabled = true, onClick = function() EditorRegistry.Toggle("TerrainEditor") end },
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
bar:SetAnchor(vec.New(0.0, 1.0))
bar:SetRelativePoint(vec.New(0.0, 1.0))
