--[[
VerticalLayout — main axis is Y. Children stack top-down by default; pass `reverse=true`
for bottom-up. See LinearLayout.luau for the full opts/childOpts/algorithm reference.

Example:
    local v = UIVerticalLayout.NewVerticalLayout(parent, 0, 0, 400, 600, 0, {
        spacing = 10, padding = 5, heightMode = "shrink", alignH = "center",
    })
    local row1 = v:NewPanel(0, 0, 0, 40, 0, "DialogBox")
    local row2 = v:NewText("hello", 0, 0, 0, "DefaultButtonText")
    v:SetChildOpts(row1, { modeY = "flex", weightY = 1 })   -- row1 fills remaining vertical space
    print(v:GetMeasuredHeight())                            -- triggers Refresh, returns total content height
]]

local LinearLayout = require("@src/API/UI/LinearLayout")

local verticalLayoutAPI = {}

function verticalLayoutAPI.NewVerticalLayout(parent, posX, posY, sizeX, sizeY, layer, opts)
    opts = opts or {}
    opts.axis = "y"
    return LinearLayout.NewLinearLayout(parent, posX, posY, sizeX, sizeY, layer, opts)
end

return verticalLayoutAPI
