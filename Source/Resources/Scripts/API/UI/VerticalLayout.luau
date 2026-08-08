--[[
VerticalLayout — main axis is Y. Children stack top-down by default; pass `reverse=true`
for bottom-up. See LinearLayout.luau for the full opts/childOpts/algorithm reference.

Example:
    local v = UIVerticalLayout.New(parent, vec.New(0, 0), vec.New(400, 600), 0i, {
        spacing = 10, padding = 5, heightMode = "shrink", alignH = "center",
    })
    local row1 = v:NewPanel(vec.New(0, 0), vec.New(0, 40), 0i, "DialogBox")
    local row2 = v:NewText("hello", vec.New(0, 0), 0i, "DefaultButtonText")
    v:SetChildOpts(row1, { modeY = "flex", weightY = 1 })   -- row1 fills remaining vertical space
    print(v:GetMeasuredHeight())                            -- triggers Refresh, returns total content height
]]

local LinearLayout = require("@src/API/UI/LinearLayout")

export type VerticalLayoutAPI =
{
    New : (parent : Widget, position : vec, size : vec, layer : integer, options : LinearLayout.LinearLayoutOpts?) -> any,
}
local verticalLayoutAPI : VerticalLayoutAPI = {}

function verticalLayoutAPI.New(parent : Widget, position : vec, size : vec, layer : integer, opts : LinearLayout.LinearLayoutOpts?)
    opts = opts or {}
    opts.axis = "y"
    return LinearLayout.New(parent, position, size, layer, opts)
end

return verticalLayoutAPI
