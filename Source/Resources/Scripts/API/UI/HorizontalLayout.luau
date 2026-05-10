--[[
HorizontalLayout — main axis is X. Children stack left-to-right by default; pass
`reverse=true` for right-to-left. See LinearLayout.luau for the full opts/childOpts
/algorithm reference.

Example:
    local h = UIHorizontalLayout.NewHorizontalLayout(parent, 0, 0, 600, 60, 0, {
        spacing = 8, padding = 4, alignV = "center",
    })
    local label = h:NewText("Name:", 0, 0, 0, "YellowText")
    local field = h:NewPanel(0, 0, 200, 30, 0, "InputBoxBackground")
    h:SetChildOpts(field, { modeX = "flex", weightX = 1 })   -- field fills remaining horizontal space
]]

local LinearLayout = require("@src/API/UI/LinearLayout")

local horizontalLayoutAPI = {}

function horizontalLayoutAPI.NewHorizontalLayout(parent, posX, posY, sizeX, sizeY, layer, opts)
    opts = opts or {}
    opts.axis = "x"
    return LinearLayout.NewLinearLayout(parent, posX, posY, sizeX, sizeY, layer, opts)
end

return horizontalLayoutAPI
