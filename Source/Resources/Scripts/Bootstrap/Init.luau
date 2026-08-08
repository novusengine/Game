local UITemplates = require("@src/API/UI/Templates")
UITemplates.RegisterTemplates();

local DragContext = require("@src/API/Game/UI/DragContext")
local GameTooltip = require("@src/API/Game/UI/GameTooltip")

DragContext:Initialize()
GameTooltip:Initialize()
