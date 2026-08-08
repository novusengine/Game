local Input = require("@src/API/Input/Input");
local UIUnitFrame = require("@src/API/Game/UI/UnitFrame");
local ActionBar = require("@src/API/Game/UI/ActionBar");
local SpellAction = require("@src/API/Game/UI/SpellAction");

type Aura =
{
    background : Panel,
    timerText  : TextWidget,
    stackText  : TextWidget,

    unitID   : integer,
    auraID   : integer,
    spellID  : integer,
    duration : number,
    stacks   : number
}

type AuraTable =
{
    widget : Widget,

    auraList : { [number] : Aura },
    auraIDToIndex : { [integer] : number }, -- Maps auraID to index in auraList
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
    targetBuffBar = { },
    targetDebuffBar = { }
};

local unitTargetIDs : { [integer] : integer } = {};

local function RegisterTemplates()
    local frameSliceInsets = Box.new(vec.New(2, 2), vec.New(2, 2));

    UI.RegisterPanelTemplate("UnitFrameOuter",
    {
        background = "texture/ui/fantasy/1x/border/panel-border-015.png",
        nineSliceInsets = frameSliceInsets,
        color = vec.New(0.44, 0.46, 0.43),
    });

    UI.RegisterPanelTemplate("UnitFrameBackground",
    {
        cornerRadius = 2.0,
        color = vec.New(0.065, 0.07, 0.064),
    });

    UI.RegisterPanelTemplate("UnitFrameHeaderRule",
    {
        color = vec.New(0.44, 0.46, 0.43),
    });

    UI.RegisterPanelTemplate("UnitFrameBarTrack",
    {
        cornerRadius = 1.5,
        color = vec.New(0.035, 0.04, 0.035),
    });

    UI.RegisterPanelTemplate("UnitFrameResourceBorder",
    {
        cornerRadius = 1.5,
        color = vec.New(0.035, 0.04, 0.035),
    });

    UI.RegisterPanelTemplate("UnitFrameResourceTrack",
    {
        cornerRadius = 0.75,
        color = vec.New(0.04, 0.043, 0.04),
    });

    UI.RegisterPanelTemplate("UnitFrameLevelBadge",
    {
        cornerRadius = 1.5,
        color = vec.New(0.30, 0.32, 0.29),
    });

    UI.RegisterPanelTemplate("UnitFrameLevelBadgeFill",
    {
        cornerRadius = 0.5,
        color = vec.New(0.05, 0.055, 0.049),
    });

    UI.RegisterTextTemplate("UnitFrameLevel",
    {
        font = "Data/Fonts/OpenSans-Regular.ttf",
        size = 7.0,
        color = vec.New(0.86, 0.82, 0.68),
        borderSize = 1.0,
        borderColor = vec.New(0.0, 0.0, 0.0),
    });

    UI.RegisterTextTemplate("UnitFrameName",
    {
        font = "Data/Fonts/OpenSans-Regular.ttf", -- Obviously we want a different font
        size = 8.0,
        color = vec.New(240.0/255.0, 240.0/255.0, 240.0/255.0),
        borderSize = 1.0,
        borderColor = vec.New(0.0, 0.0, 0.0),
    });

    UI.RegisterTextTemplate("UnitFrameHealth",
    {
        font = "Data/Fonts/OpenSans-Regular.ttf", -- Obviously we want a different font
        size = 9.0,
        color = vec.New(240.0/255.0, 240.0/255.0, 240.0/255.0),
        borderSize = 1.0,
        borderColor = vec.New(0.0, 0.0, 0.0),
    });

    UI.RegisterTextTemplate("UnitFrameResource",
    {
        font = "Data/Fonts/OpenSans-Regular.ttf", -- Obviously we want a different font
        size = 8.0,
        color = vec.New(240.0/255.0, 240.0/255.0, 240.0/255.0),
        borderSize = 1.0,
        borderColor = vec.New(0.0, 0.0, 0.0),
    });

    UI.RegisterTextTemplate("UnitFrameAuraText",
    {
        font = "Data/Fonts/OpenSans-Regular.ttf", -- Obviously we want a different font
        size = 9.0,
        color = vec.New(255.0/255.0, 255.0/255.0, 255.0/255.0),
        borderSize = 1.0,
        borderColor = vec.New(0.0, 0.0, 0.0),
    });

    UI.RegisterPanelTemplate("UnitFrameHealthFill",
    {
        cornerRadius = 1.0,
        color = vec.New(0.42, 0.55, 0.08),
    });

    UI.RegisterPanelTemplate("UnitFrameHealthDamageTrail",
    {
        cornerRadius = 1.0,
        color = vec.New(0.68, 0.20, 0.055),
    });

    UI.RegisterPanelTemplate("UnitFrameHealthHealingTrail",
    {
        cornerRadius = 1.0,
        color = vec.New(0.62, 0.90, 0.18),
    });

    UI.RegisterPanelTemplate("UnitFrameResourceFill",
    {
        cornerRadius = 1.0,
        color = vec.New(0.0, 0.4, 1.0),
    });

    UI.RegisterPanelTemplate("ActionBarButton",
    {
        cornerRadius = 2.0,
        color = vec.New(0.33, 0.33, 0.33),
    });

    UI.RegisterTextTemplate("ActionBarButtonText",
    {
        font = "Data/Fonts/QTFrizQuad.otf", -- Obviously we want a different font
        size = 10.0,
        color = vec.New(255.0/255.0, 255.0/255.0, 255.0/255.0),
        borderSize = 1.0,
        borderColor = vec.New(0.0, 0.0, 0.0)
    });
end

local function CreateUnitFrame(unitID : integer, posX : number, posY : number, sizeX : number, sizeY : number) : UIUnitFrame.UnitFrame
    local canvas : Canvas = UI.GetCanvas("UnitFrames", vec.New(0, 0), vec.New(1920, 1080));

    local unitFrame : UIUnitFrame.UnitFrame = UIUnitFrame.New(canvas, vec.New(posX, posY), vec.New(sizeX, sizeY), 1i, {
        unitID = unitID,
        unitName = Unit.GetName(unitID)
    });

    return unitFrame;
end

local function RefreshUnitFrameReaction(unitFrame : UIUnitFrame.UnitFrame)
    local unitID = unitFrame:GetUnitID();
    local localUnitID = Unit.GetLocal();
    if unitID == INVALID_UNIT_ID or localUnitID == INVALID_UNIT_ID then
        return;
    end

    unitFrame:SetUnitReaction(Unit.GetReaction(unitID), integer.tonumber(Unit.GetLevel(localUnitID)));
end

local function UpdateUnitFrame(unitFrame : UIUnitFrame.UnitFrame, deltaTime : number, smoothBars : boolean)
    local unitID = unitFrame:GetUnitID();
    local localUnitID = Unit.GetLocal();
    local playerLevel = if localUnitID == INVALID_UNIT_ID then 0 else integer.tonumber(Unit.GetLevel(localUnitID));
    unitFrame:SetUnitLevel(integer.tonumber(Unit.GetLevel(unitID)), playerLevel);

    local health, maxHealth = Unit.GetHealth(unitID);
    unitFrame:SetHealthValues(math.ceil(health), math.ceil(maxHealth));

    local resourceType = Unit.GetResourceType(unitID);
    local resource, maxResource = Unit.GetResource(unitID, resourceType);
    unitFrame:SetResourceValues(math.ceil(resource), math.ceil(maxResource));
    unitFrame:UpdateBarFills(deltaTime, smoothBars);
end

local function AddAuraToTable(auraTable : AuraTable, unitID : integer, auraID : integer, spellID : integer, duration : number, stacks : number)
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
    aura.background = auraTable.widget:NewPanel(vec.New(posX, 0), vec.New(32, 32), 1i);

    local spellInfo = Spell.GetSpellInfo(spellID);
    local iconInfo = spellInfo and Spell.GetIconInfo(spellInfo.IconID);
    if iconInfo then
        aura.background:SetBackground(iconInfo.Texture);
    else
        aura.background:SetBackground("texture/ui/icons/50x50/white/question.png");
    end

    aura.timerText = aura.background:NewText("", vec.New(0, 2), 1i, "UnitFrameAuraText");
    aura.timerText:SetAnchor(vec.New(1, 0));
    aura.timerText:SetRelativePoint(vec.New(1, 0));

    aura.stackText = aura.background:NewText("", vec.New(0, -2), 1i, "UnitFrameAuraText");
    aura.stackText:SetAnchor(vec.New(1, 1));
    aura.stackText:SetRelativePoint(vec.New(1, 1));

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

local function GetAuraTable(buffTable : AuraTable, debuffTable : AuraTable, spellID : integer) : AuraTable
    local spellInfo = Spell.GetSpellInfo(spellID);
    if spellInfo and spellInfo.IsAura and spellInfo.AuraDisposition == AuraDispositionEnum.Harmful then
        return debuffTable;
    end

    return buffTable;
end

local function FindAuraTable(firstTable : AuraTable, secondTable : AuraTable, auraID : integer) : AuraTable?
    if firstTable.auraIDToIndex[auraID] then
        return firstTable;
    end
    if secondTable.auraIDToIndex[auraID] then
        return secondTable;
    end

    return nil;
end

local function RemoveAuraFromTable(auraTable : AuraTable, auraID : integer)
    local index = auraTable.auraIDToIndex[auraID];
    if not index then
        return; -- Aura not found
    end

    local aura = auraTable.auraList[index];
    UI.DestroyWidget(aura.background);

    table.remove(auraTable.auraList, index);
    auraTable.auraIDToIndex[auraID] = nil;

    -- Update indices for remaining auras
    for i = index, #auraTable.auraList do
        local currentAura = auraTable.auraList[i];
        auraTable.auraIDToIndex[currentAura.auraID] = i;

        -- Update position
        local paddingX = 2;
        local posX = (i - 1) * (32 + paddingX); -- 32px icon + padding
        currentAura.background:SetPos(vec.New(posX, 0));
    end
end

local function ClearAuraTable(auraTable : AuraTable)
    for _, aura in ipairs(auraTable.auraList) do
        UI.DestroyWidget(aura.background);
    end

    auraTable.auraList = {};
    auraTable.auraIDToIndex = {};
end

local function ClearTargetUnitFrame()
    Unit.ClearTarget();
    unitFrames.targetUnitFrame:SetEnabled(false);
    unitFrames.targetUnitFrame:SetTargetingPlayer(false);
    unitFrames.targetUnitFrame:SetUnitID(INVALID_UNIT_ID);
    unitFrames.targetBuffBar.widget:SetEnabled(false);
    unitFrames.targetDebuffBar.widget:SetEnabled(false);
    ClearAuraTable(unitFrames.targetBuffBar);
    ClearAuraTable(unitFrames.targetDebuffBar);
end

local function InitializePlayerUnitFrame(unitID : integer)
    if unitFrames.playerUnitFrame == nil then
        return;
    end

    if unitID == INVALID_UNIT_ID then
        unitFrames.playerUnitFrame:SetEnabled(false);
        unitFrames.playerUnitFrame:SetUnitID(INVALID_UNIT_ID);
        return;
    end

    unitFrames.playerUnitFrame:SetUnitID(unitID);
    unitFrames.playerUnitFrame:SetUnitName(Unit.GetName(unitID));
    unitFrames.playerUnitFrame:SetTargetingPlayer(false);
    RefreshUnitFrameReaction(unitFrames.playerUnitFrame);

    local resourceType = Unit.GetResourceType(unitID);
    unitFrames.playerUnitFrame:UpdateResourceFillColor(resourceType);
    UpdateUnitFrame(unitFrames.playerUnitFrame, 0, false);
    unitFrames.playerUnitFrame:SetEnabled(true);
end

local function OnGameLoaded(eventID : integer, data : any)
    local isInWorld = IsInWorld();

    RegisterTemplates();

    -- Ensure UnitFrames Canvas is created
    local canvas : Canvas = UI.GetCanvas("UnitFrames", vec.New(0, 0), vec.New(1920, 1080));
    unitFrames.playerUnitFrame = CreateUnitFrame(INVALID_UNIT_ID, 770, 500, 170, 50);
    unitFrames.playerUnitFrame.border:SetOnMouseUp(function(eventID, widget, button)
        if button == 0i then
            Unit.SetTarget(Unit.GetLocal());
        end
    end);
    unitFrames.playerUnitFrame:SetEnabled(false);

    unitFrames.targetUnitFrame = CreateUnitFrame(INVALID_UNIT_ID, 1150, 500, 170, 50);
    unitFrames.targetUnitFrame:SetEnabled(false);

    unitFrames.playerBuffBar = {};
    unitFrames.playerBuffBar.widget = canvas:NewWidget(vec.New(685, 530), 1i);
    unitFrames.playerBuffBar.auraList = {};
    unitFrames.playerBuffBar.auraIDToIndex = {};

    unitFrames.playerDebuffBar = {};
    unitFrames.playerDebuffBar.widget = canvas:NewWidget(vec.New(685, 565), 1i);
    unitFrames.playerDebuffBar.auraList = {};
    unitFrames.playerDebuffBar.auraIDToIndex = {};

    unitFrames.targetBuffBar = {};
    unitFrames.targetBuffBar.widget = canvas:NewWidget(vec.New(1065, 530), 1i);
    unitFrames.targetBuffBar.widget:SetEnabled(false);
    unitFrames.targetBuffBar.auraList = {};
    unitFrames.targetBuffBar.auraIDToIndex = {};

    unitFrames.targetDebuffBar = {};
    unitFrames.targetDebuffBar.widget = canvas:NewWidget(vec.New(1065, 565), 1i);
    unitFrames.targetDebuffBar.widget:SetEnabled(false);
    unitFrames.targetDebuffBar.auraList = {};
    unitFrames.targetDebuffBar.auraIDToIndex = {};

    local actionBarPos = vec.New(800, 360);
    local actionBarPadding = vec.New(0, 0);
    local actionBar = ActionBar.New(canvas, actionBarPos, {
        rows = 1,
        columns = 8,
        slotSize = 40,
        padding = actionBarPadding,
    });
    actionBar:SetEnabled(isInWorld);

    actionBar:SetPayload(1, SpellAction.New(22i));
    actionBar:SetKey(1, Input.GetKeyValue("1")); -- Key '1'

    actionBar:SetPayload(2, SpellAction.New(21i));
    actionBar:SetKey(2, Input.GetKeyValue("2")); -- Key '2'

    actionBar:SetPayload(3, SpellAction.New(23i));
    actionBar:SetKey(3, Input.GetKeyValue("3")); -- Key '3'

    local localUnitID = Unit.GetLocal();
    if not isInWorld or localUnitID == INVALID_UNIT_ID then
        return;
    end

    InitializePlayerUnitFrame(localUnitID);

    local localAuraList = Unit.GetAuras(localUnitID);
    for _, auraInfo in ipairs(localAuraList) do
        local auraTable = GetAuraTable(unitFrames.playerBuffBar, unitFrames.playerDebuffBar, auraInfo.spellID);
        AddAuraToTable(auraTable, localUnitID, auraInfo.auraID, auraInfo.spellID, auraInfo.duration, integer.tonumber(auraInfo.stacks))
    end
end

local function OnGameUpdated(eventID : integer, data : any)
    local isInWorld = IsInWorld();
    if not isInWorld then
        if unitFrames.playerUnitFrame ~= nil then
            unitFrames.playerUnitFrame:SetEnabled(false);
        end
        if unitFrames.targetUnitFrame ~= nil then
            unitFrames.targetUnitFrame:SetEnabled(false);
        end
        if unitFrames.targetBuffBar.widget ~= nil then
            unitFrames.targetBuffBar.widget:SetEnabled(false);
        end
        if unitFrames.targetDebuffBar.widget ~= nil then
            unitFrames.targetDebuffBar.widget:SetEnabled(false);
        end
        return;
    end

    if unitFrames.playerUnitFrame == nil or unitFrames.targetUnitFrame == nil then
        return;
    end

    if not unitFrames.playerUnitFrame:IsEnabled() then
        local localUnitID = Unit.GetLocal();
        if localUnitID ~= INVALID_UNIT_ID then
            InitializePlayerUnitFrame(localUnitID);
        end
    end

    local smoothBars = UI.IsSmoothUnitFrameBarsEnabled();

    if unitFrames.playerUnitFrame:IsEnabled() then
        UpdateUnitFrame(unitFrames.playerUnitFrame, data.deltaTime, smoothBars);

        -- Update buffs
        for _, aura in ipairs(unitFrames.playerBuffBar.auraList) do
            if aura.duration > 0 and aura.timerText:IsEnabled() then
                aura.duration = math.max(0, aura.duration - data.deltaTime);

                local durationText = tostring(math.floor(aura.duration));
                aura.timerText:SetText(durationText);
            end
        end
        for _, aura in ipairs(unitFrames.playerDebuffBar.auraList) do
            if aura.duration > 0 and aura.timerText:IsEnabled() then
                aura.duration = math.max(0, aura.duration - data.deltaTime);
                aura.timerText:SetText(tostring(math.floor(aura.duration)));
            end
        end
    end

    if unitFrames.targetUnitFrame:IsEnabled() then
        UpdateUnitFrame(unitFrames.targetUnitFrame, data.deltaTime, smoothBars);

        -- Update buffs
        for _, aura in ipairs(unitFrames.targetBuffBar.auraList) do
            if aura.duration > 0 and aura.timerText:IsEnabled() then
                aura.duration = math.max(0, aura.duration - data.deltaTime);

                local durationText = tostring(math.floor(aura.duration));
                aura.timerText:SetText(durationText);
            end
        end
        for _, aura in ipairs(unitFrames.targetDebuffBar.auraList) do
            if aura.duration > 0 and aura.timerText:IsEnabled() then
                aura.duration = math.max(0, aura.duration - data.deltaTime);
                aura.timerText:SetText(tostring(math.floor(aura.duration)));
            end
        end
    end
end

local function OnLocalMoverChanged(eventID : integer, data : any)
    if not IsInWorld() then
        if unitFrames.playerUnitFrame ~= nil then
            unitFrames.playerUnitFrame:SetEnabled(false);
        end
        return;
    end

    InitializePlayerUnitFrame(data.moverID);

    if unitFrames.targetUnitFrame ~= nil and unitFrames.targetUnitFrame:IsEnabled() then
        local targetUnitID = unitFrames.targetUnitFrame:GetUnitID();
        unitFrames.targetUnitFrame:SetTargetingPlayer(unitTargetIDs[targetUnitID] == data.moverID);
        RefreshUnitFrameReaction(unitFrames.targetUnitFrame);
    end
end

local function OnTargetChanged(eventID : integer, data : any)
    local isInWorld = IsInWorld();
    if not isInWorld then
        return;
    end

    unitTargetIDs[data.unitID] = data.targetID;

    local localUnitID = Unit.GetLocal();
    if unitFrames.targetUnitFrame ~= nil and data.unitID == unitFrames.targetUnitFrame:GetUnitID() then
        unitFrames.targetUnitFrame:SetTargetingPlayer(data.targetID == localUnitID);
    end

    if data.unitID ~= localUnitID then
        return;
    end

    if data.targetID == INVALID_UNIT_ID then
        ClearTargetUnitFrame();
    else
        ClearAuraTable(unitFrames.targetBuffBar);
        ClearAuraTable(unitFrames.targetDebuffBar);

        local resourceType = Unit.GetResourceType(data.targetID);
        unitFrames.targetUnitFrame:UpdateResourceFillColor(resourceType)

        unitFrames.targetUnitFrame:SetUnitID(data.targetID);
        unitFrames.targetUnitFrame:SetUnitName(Unit.GetName(data.targetID))
        unitFrames.targetUnitFrame:SetTargetingPlayer(unitTargetIDs[data.targetID] == localUnitID);
        RefreshUnitFrameReaction(unitFrames.targetUnitFrame);
        UpdateUnitFrame(unitFrames.targetUnitFrame, 0, false);

        unitFrames.targetUnitFrame:SetEnabled(true);
        unitFrames.targetBuffBar.widget:SetEnabled(true);
        unitFrames.targetDebuffBar.widget:SetEnabled(true);

        -- Add current target buffs
        local localAuraList = Unit.GetAuras(data.targetID);
        for _, auraInfo in ipairs(localAuraList) do
            local auraTable = GetAuraTable(unitFrames.targetBuffBar, unitFrames.targetDebuffBar, auraInfo.spellID);
            AddAuraToTable(auraTable, data.targetID, auraInfo.auraID, auraInfo.spellID, auraInfo.duration, integer.tonumber(auraInfo.stacks))
        end
    end
end

local function OnUnitRemove(eventID : integer, data : any)
    unitTargetIDs[data.unitID] = nil;

    if data.unitID == unitFrames.targetUnitFrame:GetUnitID() then
        ClearTargetUnitFrame();
    end
end

local function OnUnitReactionChanged(eventID : integer, data : any)
    if unitFrames.playerUnitFrame ~= nil and data.unitID == unitFrames.playerUnitFrame:GetUnitID() then
        RefreshUnitFrameReaction(unitFrames.playerUnitFrame);
    end

    if unitFrames.targetUnitFrame ~= nil and data.unitID == unitFrames.targetUnitFrame:GetUnitID() then
        RefreshUnitFrameReaction(unitFrames.targetUnitFrame);
    end
end

local function OnAuraAdd(eventID : integer, data : any)
    local localUnitID = Unit.GetLocal();
    if data.unitID == localUnitID then
        local auraTable = GetAuraTable(unitFrames.playerBuffBar, unitFrames.playerDebuffBar, data.spellID);
        AddAuraToTable(auraTable, data.unitID, data.auraID, data.spellID, data.duration, integer.tonumber(data.stacks))
    elseif data.unitID == unitFrames.targetUnitFrame:GetUnitID() then
        local auraTable = GetAuraTable(unitFrames.targetBuffBar, unitFrames.targetDebuffBar, data.spellID);
        AddAuraToTable(auraTable, data.unitID, data.auraID, data.spellID, data.duration, integer.tonumber(data.stacks))
    end
end

local function OnAuraUpdate(eventID : integer, data : any)
    local localUnitID = Unit.GetLocal();
    if data.unitID == localUnitID then
        local auraTable = FindAuraTable(unitFrames.playerBuffBar, unitFrames.playerDebuffBar, data.auraID);
        if not auraTable then return end
        local aura = auraTable.auraList[auraTable.auraIDToIndex[data.auraID]];
        AddAuraToTable(auraTable, data.unitID, data.auraID, aura.spellID, data.duration, integer.tonumber(data.stacks))
    elseif data.unitID == unitFrames.targetUnitFrame:GetUnitID() then
        local auraTable = FindAuraTable(unitFrames.targetBuffBar, unitFrames.targetDebuffBar, data.auraID);
        if not auraTable then return end
        local aura = auraTable.auraList[auraTable.auraIDToIndex[data.auraID]];
        AddAuraToTable(auraTable, data.unitID, data.auraID, aura.spellID, data.duration, integer.tonumber(data.stacks))
    end
end

local function OnAuraRemove(eventID : integer, data : any)
    local localUnitID = Unit.GetLocal();
    if data.unitID == localUnitID then
        RemoveAuraFromTable(unitFrames.playerBuffBar, data.auraID)
        RemoveAuraFromTable(unitFrames.playerDebuffBar, data.auraID)
    elseif data.unitID == unitFrames.targetUnitFrame:GetUnitID() then
        RemoveAuraFromTable(unitFrames.targetBuffBar, data.auraID)
        RemoveAuraFromTable(unitFrames.targetDebuffBar, data.auraID)
    end
end

RegisterEvent(GameEvent.Loaded, OnGameLoaded);
RegisterEvent(GameEvent.Updated, OnGameUpdated);
RegisterEvent(GameEvent.LocalMoverChanged, OnLocalMoverChanged);
RegisterEvent(UnitEvent.Remove, OnUnitRemove);
RegisterEvent(UnitEvent.TargetChanged, OnTargetChanged);
RegisterEvent(UnitEvent.ReactionChanged, OnUnitReactionChanged);
RegisterEvent(UnitEvent.AuraAdd, OnAuraAdd);
RegisterEvent(UnitEvent.AuraUpdate, OnAuraUpdate);
RegisterEvent(UnitEvent.AuraRemove, OnAuraRemove);
