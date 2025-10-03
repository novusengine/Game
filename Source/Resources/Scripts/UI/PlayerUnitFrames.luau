local Input = require("@src/API/Input/Input");
local UIUnitFrame = require("@src/API/UI/UnitFrame");
local UIActionBar = require("@src/API/UI/ActionBar");

type Aura = 
{
    background : Panel,
    timerText  : TextWidget,
    stackText  : TextWidget,

    unitID   : number,
    auraID   : number,
    spellID  : number,
    duration : number,
    stacks   : number
}

type AuraTable = 
{
    widget : Widget,

    auraList : { [number] : Aura },
    auraIDToIndex : { [number] : number }, -- Maps auraID to index in auraList
}

type PlayerUnitFramesAPI = 
{
    playerUnitFrame : UIUnitFrame.UnitFrame,
    targetUnitFrame : UIUnitFrame.UnitFrame,

    playerBuffBar : AuraTable,
    playerDebuffBar : AuraTable,
    targetBuffBar : AuraTable,
    targetDebuffBar : AuraTable
}

local unitFrames : PlayerUnitFramesAPI =
{
    playerUnitFrame = nil,
    targetUnitFrame = nil,

    playerBuffBar = { },
    playerDebuffBar = { },
    targetAuras = { }
};

local function RegisterTemplates()
    UI.RegisterTextTemplate("UnitFrameName",
    {
        font = "Data/Fonts/OpenSans-Regular.ttf", -- Obviously we want a different font
        size = 9.0,
        --color = vector.create(0.0/255.0, 132.0/255.0, 241.0/255.0),
        color = vector.create(240.0/255.0, 240.0/255.0, 240.0/255.0),
        borderSize = 1.2,
        borderColor = vector.create(0.0, 0.0, 0.0),
    });

    UI.RegisterTextTemplate("UnitFrameHealth",
    {
        font = "Data/Fonts/OpenSans-Regular.ttf", -- Obviously we want a different font
        size = 10.0,
        color = vector.create(240.0/255.0, 240.0/255.0, 240.0/255.0),
        borderSize = 1.2,
        borderColor = vector.create(0.0, 0.0, 0.0),
    });

    UI.RegisterTextTemplate("UnitFrameResource",
    {
        font = "Data/Fonts/OpenSans-Regular.ttf", -- Obviously we want a different font
        size = 8.0,
        color = vector.create(240.0/255.0, 240.0/255.0, 240.0/255.0),
        borderSize = 1.2,
        borderColor = vector.create(0.0, 0.0, 0.0),
    });

    UI.RegisterTextTemplate("UnitFrameAuraText",
    {
        font = "Data/Fonts/OpenSans-Regular.ttf", -- Obviously we want a different font
        size = 9.0,
        color = vector.create(255.0/255.0, 255.0/255.0, 255.0/255.0),
        borderSize = 1.2,
        borderColor = vector.create(0.0, 0.0, 0.0),
    });
    
    UI.RegisterPanelTemplate("UnitFrameHealthFill",
    {
        cornerRadius = 0.0,
        color = vector.create(0.0, 0.6, 0.0),
    });

    UI.RegisterPanelTemplate("UnitFrameResourceFill",
    {
        cornerRadius = 0.0,
        color = vector.create(0.0, 0.4, 1.0),
    });

    UI.RegisterPanelTemplate("ActionBarButton",
    {
        cornerRadius = 2.0,
        color = vector.create(0.33, 0.33, 0.33),
        --borderSize = 1.0,
        --borderColor = vector.create(0.0, 0.0, 0.0),
        --background = "Data/Texture/interface/framegeneral/ui-background-marble.dds"
    });

    UI.RegisterTextTemplate("ActionBarButtonText",
    {
        font = "Data/Fonts/OpenSans-Regular.ttf", -- Obviously we want a different font
        size = 10.0,
        color = vector.create(255.0/255.0, 255.0/255.0, 255.0/255.0),
        borderSize = 1.0,
        borderColor = vector.create(0.0, 0.0, 0.0)
    });
end

local function CreateUnitFrame(unitID : number, posX : number, posY : number, sizeX : number, sizeY : number) : UIUnitFrame.UnitFrame
    local canvas : Canvas = UI.GetCanvas("UnitFrames", 0, 0, 1920, 1080);

    local unitFrame : UIUnitFrame.UnitFrame = UIUnitFrame.NewUnitFrame(canvas, posX, posY, sizeX, sizeY, 1, {
        unitID = unitID,
        unitName = Unit.GetName(unitID) 
    });

    return unitFrame;
end

local function UpdateUnitFrame(unitFrame : UIUnitFrame.UnitFrame)
    local unitID = unitFrame:GetUnitID();

    local health, maxHealth = Unit.GetHealth(unitID);
    unitFrame:SetHealth(math.ceil(health));
    unitFrame:SetMaxHealth(math.ceil(maxHealth));
    unitFrame:UpdateHealth();

    local resourceType = Unit.GetResourceType(unitID);
    local resource, maxResource = Unit.GetResource(unitID, resourceType);
    unitFrame:SetResource(math.ceil(resource));
    unitFrame:SetMaxResource(math.ceil(maxResource));
    unitFrame:UpdateResource();
end

local function AddAuraToTable(auraTable : AuraTable, unitID : number, auraID : number, spellID : number, duration : number, stacks : number)
    if auraTable.auraIDToIndex[auraID] then
        -- Already have this buff, update it instead
        local index = auraTable.auraIDToIndex[auraID];
        local aura = auraTable.auraList[index];

        aura.duration = duration;
        aura.stacks = stacks;

        -- Update the timer text
        if duration > 0 then
            local durationText = tostring(math.floor(duration));
            aura.timerText:SetText(durationText);
            aura.timerText:SetEnabled(true);
        else
            aura.timerText:SetEnabled(false);
        end

        if stacks > 1 then
            local stackText = tostring(stacks);
            aura.stackText:SetText(stackText);
            aura.stackText:SetEnabled(true);
        else
            aura.stackText:SetEnabled(false);
        end

        return;
    end

    local index = #auraTable.auraList + 1;
    local aura : Aura = {
        background = nil,
        timerText = nil,
        stackText = nil,

        unitID = unitID,
        auraID = auraID,
        spellID = spellID,
        duration = duration,
        stacks = stacks
    };

    local paddingX = 1;
    local posX = (index - 1) * (32 + paddingX); -- 32px icon + padding
    aura.background = auraTable.widget:NewPanel(posX, 0, 32, 32, 1);
    
    local spellInfo = Spell.GetSpellInfo(spellID);
    local iconInfo = spellInfo and Spell.GetIconInfo(spellInfo.IconID);
    if iconInfo then
        aura.background:SetBackground(iconInfo.Texture);
    else
        aura.background:SetBackground("Data/Textures/Interface/icons/inv_misc_questionmark.dds");
    end

    aura.timerText = aura.background:NewText("", 0, 2, 1, "UnitFrameAuraText");
    aura.timerText:SetAnchor(1, 0);
    aura.timerText:SetRelativePoint(1, 0);

    aura.stackText = aura.background:NewText("", 0, -2, 1, "UnitFrameAuraText");
    aura.stackText:SetAnchor(1, 1);
    aura.stackText:SetRelativePoint(1, 1);

    if duration > 0 then
        local durationText = tostring(math.floor(duration));
        aura.timerText:SetText(durationText);
    else
        aura.timerText:SetEnabled(false);
    end

    if stacks > 1 then
        local stackText = tostring(stacks);
        aura.stackText:SetText(stackText);
    else
        aura.stackText:SetEnabled(false);
    end

    table.insert(auraTable.auraList, aura);
    auraTable.auraIDToIndex[auraID] = index;
end

local function RemoveAuraFromTable(auraTable : AuraTable, auraID : number)
    local index = auraTable.auraIDToIndex[auraID];
    if not index then
        return; -- Aura not found
    end

    local aura = auraTable.auraList[index];
    UI.DestroyWidget(aura.background);
    UI.DestroyWidget(aura.timerText);

    table.remove(auraTable.auraList, index);
    auraTable.auraIDToIndex[auraID] = nil;

    -- Update indices for remaining auras
    for i = index, #auraTable.auraList do
        local currentAura = auraTable.auraList[i];
        auraTable.auraIDToIndex[currentAura.auraID] = i;

        -- Update position
        local paddingX = 2;
        local posX = (i - 1) * (32 + paddingX); -- 32px icon + padding
        currentAura.background:SetPos(posX, 0);
    end
end

local function OnGameLoaded(eventID : number, data : any)
    RegisterTemplates();

    -- Ensure UnitFrames Canvas is created
    local canvas : Canvas = UI.GetCanvas("UnitFrames", 0, 0, 1920, 1080);
    unitFrames.playerUnitFrame = CreateUnitFrame(INVALID_UNIT_ID, 750, 550, 150, 50);
    unitFrames.playerUnitFrame:SetEnabled(false);

    unitFrames.targetUnitFrame = CreateUnitFrame(INVALID_UNIT_ID, 1170, 550, 150, 50);
    unitFrames.targetUnitFrame:SetEnabled(false);

    unitFrames.playerBuffBar = {};
    unitFrames.playerBuffBar.widget = canvas:NewWidget(675, 580, 1);
    unitFrames.playerBuffBar.auraList = {};
    unitFrames.playerBuffBar.auraIDToIndex = {};

    unitFrames.targetBuffBar = {};
    unitFrames.targetBuffBar.widget = canvas:NewWidget(1095, 580, 1);
    unitFrames.targetBuffBar.auraList = {};
    unitFrames.targetBuffBar.auraIDToIndex = {};

    local actionBarPos = vector.create(800, 360);
    local actionBarPadding = vector.create(1, 0);
    local actionBar = UIActionBar.CreateActionBar(canvas, actionBarPos, 1, 8, 40, actionBarPadding);

    actionBar:SetSpell(1, 22);
    actionBar:SetKeyForButton(1, Input.GetKeyValue("1")); -- Key '1'

    actionBar:SetSpell(2, 21);
    actionBar:SetKeyForButton(2, Input.GetKeyValue("2")); -- Key '2'

    actionBar:SetSpell(3, 23);
    actionBar:SetKeyForButton(3, Input.GetKeyValue("3")); -- Key '3'

    local localUnitID = Unit.GetLocal();
    if localUnitID == INVALID_UNIT_ID then
        return;
    end

    local localAuraList = Unit.GetAuras(localUnitID);
    for _, auraInfo in ipairs(localAuraList) do
        AddAuraToTable(unitFrames.playerBuffBar, localUnitID, auraInfo.auraID, auraInfo.spellID, auraInfo.duration, auraInfo.stacks)
    end
end

local function OnGameUpdated(eventID : number, data : any)
    if unitFrames.playerUnitFrame:IsEnabled() then
        UpdateUnitFrame(unitFrames.playerUnitFrame);
        
        -- Update buffs
        for _, aura in ipairs(unitFrames.playerBuffBar.auraList) do
            if aura.duration > 0 and aura.timerText:IsEnabled() then
                aura.duration = math.max(0, aura.duration - data.deltaTime);

                local durationText = tostring(math.floor(aura.duration));
                aura.timerText:SetText(durationText);
            end
        end
    end

    if unitFrames.targetUnitFrame:IsEnabled() then
        UpdateUnitFrame(unitFrames.targetUnitFrame);

        -- Update buffs
        for _, aura in ipairs(unitFrames.targetBuffBar.auraList) do
            if aura.duration > 0 and aura.timerText:IsEnabled() then
                aura.duration = math.max(0, aura.duration - data.deltaTime);

                local durationText = tostring(math.floor(aura.duration));
                aura.timerText:SetText(durationText);
            end
        end
    end
end
        
local function OnLocalMoverChanged(eventID : number, data : any)
    if data.moverID == INVALID_UNIT_ID then
        unitFrames.playerUnitFrame:SetEnabled(false);
    else
        unitFrames.playerUnitFrame:SetUnitID(data.moverID);
        unitFrames.playerUnitFrame:SetUnitName(Unit.GetName(data.moverID))
        
        local class = Unit.GetClass(data.moverID);
        unitFrames.playerUnitFrame:UpdateHealthFillColor(class);
        
        local resourceType = Unit.GetResourceType(data.moverID);
        unitFrames.playerUnitFrame:UpdateResourceFillColor(resourceType);

        unitFrames.playerUnitFrame:SetEnabled(true);
    end
end

local function OnTargetChanged(eventID : number, data : any)
    local localUnitID = Unit.GetLocal();
    if data.unitID ~= localUnitID then
        return;
    end

    -- Clear existing target buffs
    for _, aura in ipairs(unitFrames.targetBuffBar.auraList) do
        UI.DestroyWidget(aura.background);
        UI.DestroyWidget(aura.timerText);
    end
    unitFrames.targetBuffBar.auraList = {};
    unitFrames.targetBuffBar.auraIDToIndex = {};

    if data.targetID == INVALID_UNIT_ID then
        unitFrames.targetUnitFrame:SetEnabled(false);
        unitFrames.targetBuffBar.widget:SetEnabled(false);
    else
        local resourceType = Unit.GetResourceType(data.targetID);
        unitFrames.targetUnitFrame:UpdateResourceFillColor(resourceType)

        unitFrames.targetUnitFrame:SetUnitID(data.targetID);
        unitFrames.targetUnitFrame:SetUnitName(Unit.GetName(data.targetID))
        UpdateUnitFrame(unitFrames.targetUnitFrame);

        unitFrames.targetUnitFrame:SetEnabled(true);
        unitFrames.targetBuffBar.widget:SetEnabled(true);

        -- Add current target buffs
        local localAuraList = Unit.GetAuras(data.targetID);
        for _, auraInfo in ipairs(localAuraList) do
            AddAuraToTable(unitFrames.targetBuffBar, data.targetID, auraInfo.auraID, auraInfo.spellID, auraInfo.duration, auraInfo.stacks)
        end
    end
end

local function OnAuraAdd(eventID : number, data : any)
    local localUnitID = Unit.GetLocal();
    if data.unitID == localUnitID then
        AddAuraToTable(unitFrames.playerBuffBar, data.unitID, data.auraID, data.spellID, data.duration, data.stacks)
    elseif data.unitID == unitFrames.targetUnitFrame:GetUnitID() then
        AddAuraToTable(unitFrames.targetBuffBar, data.unitID, data.auraID, data.spellID, data.duration, data.stacks)
    end
end

local function OnAuraUpdate(eventID : number, data : any)
    local localUnitID = Unit.GetLocal();
    if data.unitID == localUnitID then
        local auraIndex = unitFrames.playerBuffBar.auraIDToIndex[data.auraID];
        if not auraIndex then return end

        local aura = unitFrames.playerBuffBar.auraList[auraIndex];
        AddAuraToTable(unitFrames.playerBuffBar, data.unitID, data.auraID, aura.spellID, data.duration, data.stacks)
    elseif data.unitID == unitFrames.targetUnitFrame:GetUnitID() then
        local auraIndex = unitFrames.targetBuffBar.auraIDToIndex[data.auraID];
        if not auraIndex then return end

        local aura = unitFrames.targetBuffBar.auraList[auraIndex];
        AddAuraToTable(unitFrames.targetBuffBar, data.unitID, data.auraID, aura.spellID, data.duration, data.stacks)
    end
end

local function OnAuraRemove(eventID : number, data : any)
    local localUnitID = Unit.GetLocal();
    if data.unitID == localUnitID then
        RemoveAuraFromTable(unitFrames.playerBuffBar, data.auraID)
    elseif data.unitID == unitFrames.targetUnitFrame:GetUnitID() then
        RemoveAuraFromTable(unitFrames.targetBuffBar, data.auraID)
    end
end

RegisterEvent(GameEvent.Loaded, OnGameLoaded);
RegisterEvent(GameEvent.Updated, OnGameUpdated);
RegisterEvent(GameEvent.LocalMoverChanged, OnLocalMoverChanged);
RegisterEvent(UnitEvent.TargetChanged, OnTargetChanged);
RegisterEvent(UnitEvent.AuraAdd, OnAuraAdd)
RegisterEvent(UnitEvent.AuraUpdate, OnAuraUpdate)
RegisterEvent(UnitEvent.AuraRemove, OnAuraRemove)