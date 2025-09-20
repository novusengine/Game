local UIUnitFrame = require("@src/API/UI/UnitFrame");

type PlayerUnitFramesAPI = 
{
    playerUnitFrame : UIUnitFrame.UnitFrame
}

local unitFrames : PlayerUnitFramesAPI =
{
    playerUnitFrame = nil,
    targetUnitFrame = nil
};

local function RegisterTemplates()
    UI.RegisterTextTemplate("UnitFrameName",
    {
        font = "Data/Fonts/Ubuntu-Regular.ttf", -- Obviously we want a different font
        size = 9.0,
        --color = vector.create(0.0/255.0, 132.0/255.0, 241.0/255.0),
        color = vector.create(240.0/255.0, 240.0/255.0, 240.0/255.0),
        borderSize = 1.0,
        borderColor = vector.create(0.0, 0.0, 0.0),
    });

    UI.RegisterTextTemplate("UnitFrameHealth",
    {
        font = "Data/Fonts/Ubuntu-Regular.ttf", -- Obviously we want a different font
        size = 10.0,
        color = vector.create(240.0/240.0, 240.0/255.0, 240.0/255.0),
        borderSize = 1.0,
        borderColor = vector.create(0.0, 0.0, 0.0),
    });

    UI.RegisterTextTemplate("UnitFrameResource",
    {
        font = "Data/Fonts/Ubuntu-Regular.ttf", -- Obviously we want a different font
        size = 8.0,
        color = vector.create(240.0/240.0, 240.0/255.0, 240.0/255.0),
        borderSize = 1.0,
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
    unitFrame:SetHealth(health);
    unitFrame:SetMaxHealth(maxHealth);
    unitFrame:UpdateHealth();

    local resource, maxResource = Unit.GetResource(unitID);
    unitFrame:SetResource(resource);
    unitFrame:SetMaxResource(maxResource);
    unitFrame:UpdateResource();
end

local function OnGameLoaded(eventID : number, data : any)
    RegisterTemplates();

    -- Ensure UnitFrames Canvas is created
    local _ : Canvas = UI.GetCanvas("UnitFrames", 0, 0, 1920, 1080);
    unitFrames.playerUnitFrame = CreateUnitFrame(INVALID_UNIT_ID, 750, 550, 150, 50);
    unitFrames.playerUnitFrame:SetEnabled(false);

    unitFrames.targetUnitFrame = CreateUnitFrame(INVALID_UNIT_ID, 1170, 550, 150, 50);
    unitFrames.targetUnitFrame:SetEnabled(false);
end

local function OnGameUpdated(eventID : number, data : any)
    if unitFrames.playerUnitFrame:IsEnabled() then
        UpdateUnitFrame(unitFrames.playerUnitFrame);
    end

    if unitFrames.targetUnitFrame:IsEnabled() then
        UpdateUnitFrame(unitFrames.targetUnitFrame);
    end
end
        
local function OnLocalMoverChanged(eventID : number, data : any)
    if data.moverID == INVALID_UNIT_ID then
        unitFrames.playerUnitFrame:SetEnabled(false);
    else
        unitFrames.playerUnitFrame:SetUnitID(data.moverID);
        unitFrames.playerUnitFrame:SetUnitName(Unit.GetName(data.moverID))
        unitFrames.playerUnitFrame:SetEnabled(true);
    end
end

local function OnTargetChanged(eventID : number, data : any)
    local localUnitID = Unit.GetLocal();
    if data.unitID ~= localUnitID then
        return;
    end

    if data.targetID == INVALID_UNIT_ID then
        unitFrames.targetUnitFrame:SetEnabled(false);
    else
        unitFrames.targetUnitFrame:SetUnitID(data.targetID);
        unitFrames.targetUnitFrame:SetUnitName(Unit.GetName(data.targetID))
        UpdateUnitFrame(unitFrames.targetUnitFrame);

        unitFrames.targetUnitFrame:SetEnabled(true);
    end
end

RegisterEvent(GameEvent.Loaded, OnGameLoaded);
RegisterEvent(GameEvent.Updated, OnGameUpdated);
RegisterEvent(GameEvent.LocalMoverChanged, OnLocalMoverChanged);
RegisterEvent(UnitEvent.TargetChanged, OnTargetChanged);