local RootedWidget = require("@src/API/UI/RootedWidget");

export type Tooltip =
{
    root : Panel,
    content : Panel,
    ShowAt : (self : Tooltip, position : vec) -> (),
    Hide : (self : Tooltip) -> (),
    Clear : (self : Tooltip) -> (),
    AddLine : (self : Tooltip, text : string, template : string?, color : vec?) -> TextWidget,
}

export type TooltipAPI = { New : (parent : Widget, size : vec, layer : integer) -> Tooltip }
local tooltipAPI : TooltipAPI = { };

function tooltipAPI.New(parent : Widget, size : vec, layer : integer) : Tooltip
    local root = parent:NewPanel(vec.New(0, 0), size, layer, "DialogBox");
    root:SetEnabled(false);
    local self = { root = root, content = root, _lines = { }, _cursorY = -8 } :: any;
    RootedWidget.Attach(self);

    function self:ShowAt(position : vec)
        self.root:SetWorldPos(position);
        self.root:SetEnabled(true);
    end
    function self:Hide() self.root:SetEnabled(false); end
    function self:Clear()
        for _, line in self._lines do UI.DestroyWidget(line); end
        table.clear(self._lines);
        self._cursorY = -8;
    end
    function self:AddLine(text : string, template : string?, color : vec?) : TextWidget
        local line = self.content:NewText(text, vec.New(8, self._cursorY), 1i, template or "DefaultButtonText");
        line:SetAnchor(vec.New(0.0, 1.0));
        line:SetRelativePoint(vec.New(0.0, 1.0));
        if color ~= nil then line:SetColor(color); end
        table.insert(self._lines, line);
        self._cursorY -= line:GetHeight();
        return line;
    end

    return self :: Tooltip;
end

return tooltipAPI;
