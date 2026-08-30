local RootedWidget = require("@src/API/UI/RootedWidget");

export type AtlasPanelAPI =
{
    New : (parent : Widget, position : vec, size : vec, layer : integer, atlasPanelTemplateTable : AtlasPanelTemplate) -> AtlasPanel
}

export type AtlasPanelTemplate =
{
    texture : string,
    numTexturesX : number?,
    numTexturesY : number?,
    atlasIndex : number?
}

export type AtlasPanel =
{
    root : Panel,
    SetAtlasIndex : (self : AtlasPanel, index : number) -> (),
    SetAnchor : (self : AtlasPanel, anchor : vec) -> (),
    SetRelativePoint : (self : AtlasPanel, point : vec) -> (),
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
    GetSize : (self : AtlasPanel) -> vec,
    GetWidth : (self : AtlasPanel) -> number,
    GetHeight : (self : AtlasPanel) -> number,
    SetSize : (self : AtlasPanel, size : vec) -> (),
    SetWidth : (self : AtlasPanel, sizeX : number) -> (),
    SetHeight : (self : AtlasPanel, sizeY : number) -> (),
    SetBackground : (self : AtlasPanel, texturePath : string) -> (),
    SetForeground : (self : AtlasPanel, texturePath : string) -> (),
    SetColor : (self : AtlasPanel, rgb : vec) -> (),
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

        atlasPanelTable.root:SetTexCoords(vec.New(texCoordMinX, texCoordMinY), vec.New(texCoordMaxX, texCoordMaxY));
    end

    atlasPanelTable.SetAnchor = function(self : AtlasPanel, anchor : vec)
        self.root:SetAnchor(anchor);
    end
    atlasPanelTable.SetRelativePoint = function(self : AtlasPanel, point : vec)
        self.root:SetRelativePoint(point);
    end

    atlasPanelTable.SetOnMouseDown = function(self : AtlasPanel, func : MouseEventCallback)
        self.root:SetOnMouseDown(func);
    end
    atlasPanelTable.SetOnMouseUp = function(self : AtlasPanel, func : MouseEventCallback)
        self.root:SetOnMouseUp(func);
    end
    atlasPanelTable.SetOnMouseHeld = function(self : AtlasPanel, func : MouseEventCallback)
        self.root:SetOnMouseHeld(func);
    end
    atlasPanelTable.SetOnMouseScroll = function(self : AtlasPanel, func : MouseEventCallback)
        self.root:SetOnMouseScroll(func);
    end
    
    atlasPanelTable.SetOnHoverBegin = function(self : AtlasPanel, func : MouseEventCallback)
        self.root:SetOnHoverBegin(func);
    end
    atlasPanelTable.SetOnHoverEnd = function(self : AtlasPanel, func : MouseEventCallback)
        self.root:SetOnHoverEnd(func);
    end
    atlasPanelTable.SetOnHoverHeld = function(self : AtlasPanel, func : MouseEventCallback)
        self.root:SetOnHoverHeld(func);
    end

    atlasPanelTable.SetOnFocusBegin = function(self : AtlasPanel, func : MouseEventCallback)
        self.root:SetOnFocusBegin(func);
    end
    atlasPanelTable.SetOnFocusEnd = function(self : AtlasPanel, func : MouseEventCallback)
        self.root:SetOnFocusEnd(func);
    end
    atlasPanelTable.SetOnFocusHeld = function(self : AtlasPanel, func : MouseEventCallback)
        self.root:SetOnFocusHeld(func);
    end

    atlasPanelTable.SetOnKeyboard = function(self : AtlasPanel, func : KeyboardEventCallback)
        self.root:SetOnKeyboard(func);
    end

    atlasPanelTable.GetSize = function (self : AtlasPanel)
        return self.root:GetSize();
    end
    atlasPanelTable.GetWidth = function (self : AtlasPanel)
        return self.root:GetWidth();
    end
    atlasPanelTable.GetHeight = function (self : AtlasPanel)
        return self.root:GetHeight();
    end
    atlasPanelTable.SetSize = function (self : AtlasPanel, size : vec)
        self.root:SetSize(size);
    end
    atlasPanelTable.SetWidth = function (self : AtlasPanel, sizeX : number)
        self.root:SetWidth(sizeX);
    end
    atlasPanelTable.SetHeight = function (self : AtlasPanel, sizeY : number)
        self.root:SetHeight(sizeY);
    end
    atlasPanelTable.SetBackground = function (self : AtlasPanel, texturePath : string)
        self.root:SetBackground(texturePath);
    end
    atlasPanelTable.SetForeground = function (self : AtlasPanel, texturePath : string)
        self.root:SetForeground(texturePath);
    end
    atlasPanelTable.SetColor = function (self : AtlasPanel, rgb)
        self.root:SetColor(rgb);
    end
    atlasPanelTable.SetAlpha = function (self : AtlasPanel, alpha)
        self.root:SetAlpha(alpha);
    end

    return atlasPanelTable;
end

function atlasPanelAPI.New(parent : Widget, position : vec, size : vec, layer : integer, atlasPanelTemplateTable : AtlasPanelTemplate)
    local atlasPanelTable : AtlasPanel = {} :: any;

    local texture = atlasPanelTemplateTable["texture"];
    atlasPanelTable.numTexturesX = atlasPanelTemplateTable["numTexturesX"] or 1;
    atlasPanelTable.numTexturesY = atlasPanelTemplateTable["numTexturesY"] or 1;
    atlasPanelTable.atlasIndex = atlasPanelTemplateTable["atlasIndex"] or 1;

    local textureSize = UI.GetTextureSize(texture);
    atlasPanelTable.textureWidth = if textureSize ~= nil then textureSize.x else 0;
    atlasPanelTable.textureHeight = if textureSize ~= nil then textureSize.y else 0;

    -- Create the panel
    atlasPanelTable.root = parent:NewPanel(position, size, layer);
    atlasPanelTable.root:SetBackground(texture);
    RootedWidget.Attach(atlasPanelTable);

    atlasPanelTable = SetupAtlasPanelMethods(atlasPanelTable);
    atlasPanelTable:SetAtlasIndex(atlasPanelTable.atlasIndex);

    return atlasPanelTable;
end

return atlasPanelAPI;
