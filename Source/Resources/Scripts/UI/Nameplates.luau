local UINameplate = require("@src/API/UI/Nameplate");
local OptionsContext = require("@src/API/OptionsContext")

local nameplates = {};

local function RegisterTemplates()
    UI.RegisterTextTemplate("NameplateName",
    {
        font = "Data/Fonts/Ubuntu-Regular.ttf", -- Obviously we want a different font
        size = 30.0,
        color = vector.create(255.0/255.0, 255.0/255.0, 255.0/255.0),
        borderSize = 1.0,
        borderColor = vector.create(0.0, 0.0, 0.0),
    });

    UI.RegisterTextTemplate("NameplateHealth",
    {
        font = "Data/Fonts/Ubuntu-Regular.ttf", -- Obviously we want a different font
        size = 22.0,
        color = vector.create(255.0/255.0, 255.0/255.0, 255.0/255.0),
        borderSize = 1.0,
        borderColor = vector.create(0.0, 0.0, 0.0),
    });
end

-- Setup options
local optionCategory = OptionsContext:GetCategory(OptionCategoryType.Gameplay);
local optionGroup = optionCategory:Get("Nameplates");
local optionSection = optionGroup:Get("General");
--local enabledOption = optionSection:GetOption("Enabled");
local enabledOption = optionSection:RegisterOption("Enabled", true, OptionType.Checkbox);
local isEnabled = enabledOption:Get();

local function OnGameLoaded(eventID : number, motd : string)
    RegisterTemplates();

    -- Create debug nameplate
    local canvas : Canvas = UI.GetCanvas("Nameplates", 0, 0, 1920, 1080);

    local nameplate = UINameplate.NewNameplate(canvas, 0, 0, 300, 30, 0, {
        panelTemplate = "DialogBox",
        textTemplate = "DefaultButtonText",
    });
    nameplate:SetMaxHealth(1337);

    table.insert(nameplates, nameplate);
end
RegisterGameEvent(GameEvent.Loaded, OnGameLoaded)

local function OnGameUpdated(eventID : number, deltaTime : number)
    local enabled = enabledOption:Get();
    if (not enabled) then
        return;
    end

    for i, nameplate in nameplates do

        local health = nameplate:GetHealth();
        local maxHealth = nameplate:GetMaxHealth();

        health += deltaTime * 100;
        if (health > maxHealth) then
            health = 0;
        end

        nameplate:SetHealth(health);

        -- Local unit nameplates (change this when we have a better way of getting unitIDs)
        if nameplate.unitID == -1 then
            local unitID = Unit.GetLocal();
            local pos = Unit.GetNamePosition(unitID);
            nameplate:SetPos3D(pos);
        end
    end
end
RegisterGameEvent(GameEvent.Updated, OnGameUpdated)