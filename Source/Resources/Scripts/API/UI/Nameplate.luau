type NameplateAPI = 
{
    NewNameplate : (parent : Panel, posX : number, posY : number, sizeX : number, sizeY : number, layer : number, nameplateTemplateTable : NameplateTemplate) -> AtlasPanel,

    nameplates : { Nameplate },
}

type NameplateTemplate =
{
    unitID : number,
}

type Nameplate =
{
    SetAnchor : (self : Nameplate, anchorX : number, anchorY : number) -> (),
    SetRelativePoint : (self : Nameplate, relativeX : number, relativeY : number) -> (),

    unitID : number,
    background : Panel,
    foreground : Panel,
}

local nameplateAPI : NameplateAPI = {};

local function SetupNameplateMethods(nameplateTable : Nameplate)

    nameplateTable.SetAnchor = function(self : Nameplate, anchorX : number, anchorY : number)
        self.border:SetAnchor(anchorX, anchorY);
    end

    nameplateTable.SetRelativePoint = function(self : Nameplate, relativeX : number, relativeY : number)
        self.border:SetRelativePoint(relativeX, relativeY);
    end

    nameplateTable.SetPos3D = function(self : Nameplate, pos : Vector3)
        self.border:SetPos3D(pos);
    end

    nameplateTable.GetHealth = function(self : Nameplate)
        return self.health;
    end

    nameplateTable.SetHealth = function(self : Nameplate, health : number)
        self.health = health;
        self:UpdateHealth();
    end

    nameplateTable.GetMaxHealth = function(self : Nameplate)
        return self.maxHealth;
    end

    nameplateTable.SetMaxHealth = function(self : Nameplate, maxHealth : number)
        self.maxHealth = maxHealth;
        self:UpdateHealth();
    end

    nameplateTable.UpdateHealth = function(self : Nameplate)
        local healthPercent = self.health / self.maxHealth;
        local newWidth = self.background:GetWidth() * healthPercent;
        --nameplateTable.fill:SetClipRect(0, 0, 1, 1);
        self.fill:SetWidth(newWidth);

        local text = string.format("%d (%.0f%%)", self.health, healthPercent * 100);
        nameplateTable.healthText:SetText(text);
    end

    return nameplateTable;
end

function nameplateAPI.NewNameplate(parent : Widget, posX : number, posY : number, sizeX : number, sizeY : number, layer : number, nameplateTemplateTable : NameplateTemplate)
    local nameplateTable : Nameplate = {};

    nameplateTable.unitID = nameplateTemplateTable.unitID or -1;
    nameplateTable.health = 0;
    nameplateTable.maxHealth = 100;

    local borderWidth = 2.0;

    -- Create the border panel
    nameplateTable.border = parent:NewPanel(posX, posY, sizeX, sizeY, layer);
    nameplateTable.border:SetRelativePoint(0.5, 0.5);
    nameplateTable.border:SetColor(1.0, 1.0, 1.0);

    -- Create the background panel
    local sizeX = sizeX - borderWidth * 2;
    local sizeY = sizeY - borderWidth * 2;

    nameplateTable.background = nameplateTable.border:NewPanel(0, 0, sizeX, sizeY, layer, "DebugRed");
    nameplateTable.background:SetBackground("Data/Texture/interface/raidframe/raid-bar-hp-fill.dds");
    nameplateTable.background:SetAnchor(0.5, 0.5);
    nameplateTable.background:SetRelativePoint(0.5, 0.5);

    -- Create the fill panel which gets resized based on HP
    nameplateTable.fill = nameplateTable.background:NewPanel(0, 0, sizeX, sizeY, layer, "DebugGreen");
    nameplateTable.fill:SetBackground("Data/Texture/interface/raidframe/raid-bar-hp-fill.dds");
    nameplateTable.fill:SetClipRect(0, 0, 1, 1);

    -- Create name text
    nameplateTable.nameText = nameplateTable.border:NewText("Pursche", 5, 2, 0, "NameplateName");
    nameplateTable.nameText:SetAnchor(0.0, 1.0);
    nameplateTable.nameText:SetRelativePoint(0.0, 0.0);

    -- Create health text
    nameplateTable.healthText = nameplateTable.border:NewText("1000 (100%)", 0, 5, 0, "NameplateHealth");
    nameplateTable.healthText:SetAnchor(1.0, 0.5);
    nameplateTable.healthText:SetRelativePoint(1.0, 0.5);

    nameplateTable = SetupNameplateMethods(nameplateTable);

    return nameplateTable;
end

return nameplateAPI;