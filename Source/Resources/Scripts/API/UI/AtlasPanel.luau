type AtlasPanelAPI = 
{
    NewAtlasPanel : (parent : Panel, posX : number, posY : number, sizeX : number, sizeY : number, layer : number, atlasPanelTemplateTable : AtlasPanelTemplate) -> AtlasPanel
}

type AtlasPanelTemplate =
{
    texture : string,
    numTexturesX : number?,
    numTexturesY : number?,
    atlasIndex : number?
}

type AtlasPanel =
{
    SetAtlasIndex : (self : AtlasPanel, index : number) -> (),
    SetAnchor : (self : AtlasPanel, x : number, y : number) -> (),
    SetRelativePoint : (self : AtlasPanel, x : number, y : number) -> (),
    SetOnMouseDown : (self : AtlasPanel, func : () -> ()) -> (),
    SetOnMouseUp : (self : AtlasPanel, func : () -> ()) -> (),
    SetOnMouseHeld : (self : AtlasPanel, func : () -> ()) -> (),
    SetOnMouseScroll : (self : AtlasPanel, func : () -> ()) -> (),
    SetOnHoverBegin : (self : AtlasPanel, func : () -> ()) -> (),
    SetOnHoverEnd : (self : AtlasPanel, func : () -> ()) -> (),
    SetOnHoverHeld : (self : AtlasPanel, func : () -> ()) -> (),
    SetOnFocusBegin : (self : AtlasPanel, func : () -> ()) -> (),
    SetOnFocusEnd : (self : AtlasPanel, func : () -> ()) -> (),
    SetOnFocusHeld : (self : AtlasPanel, func : () -> ()) -> (),
    SetOnKeyboard : (self : AtlasPanel, func : () -> ()) -> (),
    GetSize : (self : AtlasPanel) -> ( number, number ),
    GetWidth : (self : AtlasPanel) -> number,
    GetHeight : (self : AtlasPanel) -> number,
    SetSize : (self : AtlasPanel, sizeX : number, sizeY : number) -> (),
    SetWidth : (self : AtlasPanel, sizeX : number) -> (),
    SetHeight : (self : AtlasPanel, sizeY : number) -> (),
    SetBackground : (self : AtlasPanel, texturePath : string) -> (),
    SetForeground : (self : AtlasPanel, texturePath : string) -> (),
    SetColor : (self : AtlasPanel, rgb : Color) -> (),
    SetAlpha : (self : AtlasPanel, alpha : number) -> ()
}

local atlasPanelAPI : AtlasPanelAPI = {};

local function SetupAtlasPanelMethods(atlasPanelTable : AtlasPanel)

    function atlasPanelTable.SetAtlasIndex(self : AtlasPanel, index : number)
        local numTexturesX = self.numTexturesX;
        local numTexturesY = self.numTexturesY;
    
        -- Assuming the atlas uses normalized coordinates [0, 1] and index is 1-based:
        local cellWidth = 1 / numTexturesX;
        local cellHeight = 1 / numTexturesY;
    
        local col = (index - 1) % numTexturesX;
        local row = math.floor((index - 1) / numTexturesX);
    
        local texCoordMinX = col * cellWidth;
        local texCoordMinY = row * cellHeight;
        local texCoordMaxX = texCoordMinX + cellWidth;
        local texCoordMaxY = texCoordMinY + cellHeight;

        atlasPanelTable.panel:SetTexCoords(texCoordMinX, texCoordMinY, texCoordMaxX, texCoordMaxY);
    end

    atlasPanelTable.SetAnchor = function(self : AtlasPanel, x : number, y : number)
        self.panel:SetAnchor(x, y);
    end
    atlasPanelTable.SetRelativePoint = function(self : AtlasPanel, x : number, y : number)
        self.panel:SetRelativePoint(x, y);
    end

    atlasPanelTable.SetOnMouseDown = function(self : AtlasPanel, func : MouseEventCallback)
        self.panel:SetOnMouseDown(func);
    end
    atlasPanelTable.SetOnMouseUp = function(self : AtlasPanel, func : MouseEventCallback)
        self.panel:SetOnMouseUp(func);
    end
    atlasPanelTable.SetOnMouseHeld = function(self : AtlasPanel, func : MouseEventCallback)
        self.panel:SetOnMouseHeld(func);
    end
    atlasPanelTable.SetOnMouseScroll = function(self : AtlasPanel, func : MouseEventCallback)
        self.panel:SetOnMouseScroll(func);
    end
    
    atlasPanelTable.SetOnHoverBegin = function(self : AtlasPanel, func : MouseEventCallback)
        self.panel:SetOnHoverBegin(func);
    end
    atlasPanelTable.SetOnHoverEnd = function(self : AtlasPanel, func : MouseEventCallback)
        self.panel:SetOnHoverEnd(func);
    end
    atlasPanelTable.SetOnHoverHeld = function(self : AtlasPanel, func : MouseEventCallback)
        self.panel:SetOnHoverHeld(func);
    end

    atlasPanelTable.SetOnFocusBegin = function(self : AtlasPanel, func : MouseEventCallback)
        self.panel:SetOnFocusBegin(func);
    end
    atlasPanelTable.SetOnFocusEnd = function(self : AtlasPanel, func : MouseEventCallback)
        self.panel:SetOnFocusEnd(func);
    end
    atlasPanelTable.SetOnFocusHeld = function(self : AtlasPanel, func : MouseEventCallback)
        self.panel:SetOnFocusHeld(func);
    end

    atlasPanelTable.SetOnKeyboard = function(self : AtlasPanel, func : KeyboardEventCallback)
        self.panel:SetOnKeyboard(func);
    end

    atlasPanelTable.GetSize = function (self : AtlasPanel)
        return self.panel:GetSize();
    end
    atlasPanelTable.GetWidth = function (self : AtlasPanel)
        return self.panel:GetWidth();
    end
    atlasPanelTable.GetHeight = function (self : AtlasPanel)
        return self.panel:GetHeight();
    end
    atlasPanelTable.SetSize = function (self : AtlasPanel, sizeX : number, sizeY : number)
        self.panel:SetSize(sizeX, sizeY);
    end
    atlasPanelTable.SetWidth = function (self : AtlasPanel, sizeX : number)
        self.panel:SetWidth(sizeX);
    end
    atlasPanelTable.SetHeight = function (self : AtlasPanel, sizeY : number)
        self.panel:SetHeight(sizeY);
    end
    atlasPanelTable.SetBackground = function (self : AtlasPanel, texturePath : string)
        self.panel:SetBackground(texturePath);
    end
    atlasPanelTable.SetForeground = function (self : AtlasPanel, texturePath : string)
        self.panel:SetForeground(texturePath);
    end
    atlasPanelTable.SetColor = function (self : AtlasPanel, rgb)
        self.panel:SetColor(rgb);
    end
    atlasPanelTable.SetAlpha = function (self : AtlasPanel, alpha)
        self.panel:SetAlpha(alpha);
    end

    return atlasPanelTable;
end

function atlasPanelAPI.NewAtlasPanel(parent : Widget, posX : number, posY : number, sizeX : number, sizeY : number, layer : number, atlasPanelTemplateTable : AtlasPanelTemplate)
    local atlasPanelTable : AtlasPanel = {};

    local texture = atlasPanelTemplateTable["texture"];
    atlasPanelTable.numTexturesX = atlasPanelTemplateTable["numTexturesX"] or 1;
    atlasPanelTable.numTexturesY = atlasPanelTemplateTable["numTexturesY"] or 1;
    atlasPanelTable.atlasIndex = atlasPanelTemplateTable["atlasIndex"] or 1;

    atlasPanelTable.textureWidth, atlasPanelTable.textureHeight = UI.GetTextureSize(texture);

    -- Create the panel
    atlasPanelTable.panel = parent:NewPanel(posX, posY, sizeX, sizeY, layer);
    atlasPanelTable.panel:SetBackground(texture);

    atlasPanelTable = SetupAtlasPanelMethods(atlasPanelTable);
    atlasPanelTable:SetAtlasIndex(atlasPanelTable.atlasIndex);

    return atlasPanelTable;
end

return atlasPanelAPI;