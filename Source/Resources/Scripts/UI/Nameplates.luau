--!native
local UINameplate = require("@src/API/UI/Nameplate");
local OptionsContext = require("@src/API/OptionsContext")

local nameplates = {};

local function RegisterTemplates()
    UI.RegisterTextTemplate("PlayerName",
    {
        font = "Data/Fonts/Ubuntu-Regular.ttf", -- Obviously we want a different font
        size = 14.0,
        color = vector.create(0.0/255.0, 112.0/255.0, 221.0/255.0),
        borderSize = 0.5,
        borderColor = vector.create(0.0, 0.0, 0.0),
    });

    UI.RegisterTextTemplate("NameplateName",
    {
        font = "Data/Fonts/Ubuntu-Regular.ttf", -- Obviously we want a different font
        size = 12.0,
        color = vector.create(255.0/255.0, 255.0/255.0, 255.0/255.0),
        borderSize = 1.0,
        borderColor = vector.create(0.0, 0.0, 0.0),
    });

    UI.RegisterTextTemplate("NameplateHealth",
    {
        font = "Data/Fonts/Ubuntu-Regular.ttf", -- Obviously we want a different font
        size = 10.0,
        color = vector.create(255.0/255.0, 255.0/255.0, 255.0/255.0),
        borderSize = 1.0,
        borderColor = vector.create(0.0, 0.0, 0.0),
    });
end

local function SetEnableNameplates(enabled : boolean)
    for i, nameplate in nameplates do
        nameplate:SetEnabled(enabled);
    end
end

-- Setup options
local optionCategory = OptionsContext:GetCategory(OptionCategoryType.Gameplay);
local optionGroup = optionCategory:Get("Nameplates");
local optionSection = optionGroup:Get("General");

-- Enabled option
local enabledOption = optionSection:RegisterOption("Enabled", true, OptionType.Checkbox);
local enabledCancelCallback = enabledOption:RegisterOnValueChanged(function(self, value)
    SetEnableNameplates(value);
    return true;
end);
--enabledCancelCallback(); -- Call this to cancel the callback

-- Increase speed option SLIDER
local increaseSpeedOption = optionSection:RegisterOption("Increase Speed", 10, OptionType.Slider, { Min = 3, Max = 30, Step = 1});
local speedCancelCallback = increaseSpeedOption:RegisterOnValueChanged(function(self, value)
    local number = tonumber(value);
    if (number == nil) then
        return false;
    end

    return true;
end);

-- Text display mode option
local textDisplayModeOption = optionSection:RegisterOption("Text Display Mode", 3, OptionType.ComboBox, {options = { "None", "Val", "Val (%)" }});
local textDisplayModeCancelCallback = textDisplayModeOption:RegisterOnValueChanged(function(self, value)
    local number = tonumber(value);
    if (number == nil) then
        return false;
    end

    return true;
end);

local playerNameTextWidget = nil

local function OnGameLoaded(eventID : number, motd : string)
    RegisterTemplates();

    -- Ensure Nameplates Canvas is created
    local canvas : Canvas = UI.GetCanvas("Nameplates", 0, 0, 1920, 1080);

    -- TODO : Iterate Existing UnitIDs and create nameplates
end

local function OnGameUpdated(eventID : number, deltaTime : number)
    local enabled = enabledOption:Get();
    if (not enabled) then
        return;
    end
    
    for i, nameplate in nameplates do
        local health = nameplate:GetHealth();
        local maxHealth = nameplate:GetMaxHealth();

        if health < maxHealth then
            health += deltaTime * increaseSpeedOption:Get();
            health = math.min(health, maxHealth);

            nameplate:SetHealth(health);
        end

        local unitID = nameplate:GetUnitID();
        nameplate:SetPosToUnit(unitID);
    end
end

local function OnUnitCreated(eventID : number, unitID : number)
    local canvas : Canvas = UI.GetCanvas("Nameplates", 0, 0, 1920, 1080);

    local nameplate = UINameplate.NewNameplate(canvas, 0, 0, 125, 20, 0, {
        panelTemplate = "DialogBox",
        textTemplate = "DefaultButtonText",

        unitID = unitID,
        unitName = Unit.GetName(unitID)
    });
    nameplate:SetMaxHealth(50);
    nameplates[unitID] = nameplate;
end

local function OnUnitDestroyed(eventID : number, unitID : number)
    local nameplate = nameplates[unitID];
    if (nameplate == nil) then
        return
    end

    nameplate:Destroy();
    nameplates[unitID] = nil;
end

RegisterGameEvent(GameEvent.Loaded, OnGameLoaded)
RegisterGameEvent(GameEvent.Updated, OnGameUpdated)
RegisterPlayerEvent(PlayerEvent.Created, OnUnitCreated)
RegisterPlayerEvent(PlayerEvent.Destroyed, OnUnitDestroyed)