--[[
HorizontalLayout — main axis is X. Children stack left-to-right by default; pass
`reverse=true` for right-to-left. See LinearLayout.luau for the full opts/childOpts
/algorithm reference.

Example:
    local h = UIHorizontalLayout.New(parent, vec.New(0, 0), vec.New(600, 60), 0i, {
        spacing = 8, padding = 4, alignV = "center",
    })
    local label = h:NewText("Name:", vec.New(0, 0), 0i, "YellowText")
    local field = h:NewPanel(vec.New(0, 0), vec.New(200, 30), 0i, "InputBoxBackground")
    h:SetChildOpts(field, { modeX = "flex", weightX = 1 })   -- field fills remaining horizontal space
]]

local LinearLayout = require("@src/API/UI/LinearLayout")

export type HorizontalLayoutAPI =
{
    New : (parent : Widget, position : vec, size : vec, layer : integer, options : LinearLayout.LinearLayoutOpts?) -> any,
}
local horizontalLayoutAPI : HorizontalLayoutAPI = {}

function horizontalLayoutAPI.New(parent : Widget, position : vec, size : vec, layer : integer, opts : LinearLayout.LinearLayoutOpts?)
    opts = opts or {}
    opts.axis = "x"
    return LinearLayout.New(parent, position, size, layer, opts)
end

return horizontalLayoutAPI
