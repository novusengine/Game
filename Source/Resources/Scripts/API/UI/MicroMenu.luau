local UIStack = require("@src/API/UI/UIStack");
local PlayerContainers = require("@src/API/Game/PlayerContainers");

-- Add, remove, or reorder micro-menu entries here. Status callbacks are only
-- evaluated by Refresh(), including the initial refresh after scripts load.
local MENU_ENTRIES : { MicroMenuEntry } =
{
    {
        name = "Character Panel",
        description = "Open the character panel",
        backgroundTexture = "texture/ui/icons/64x64/characterpaneltoggleicon.png",
        flags = { isImportant = false },

        isVisibleCallback = function()
            return true;
        end,

        isAvailableCallback = function()
            return PlayerContainers.GetEquipmentBag() ~= nil;
        end,

        onClickCallback = function()
            PlayerContainers.ToggleEquipmentBag();
        end,
    },

    {
        name = "Game Menu",
        description = "Open the game menu.",
        backgroundTexture = "texture/ui/icons/50x50/white/menu_grid.png",
        flags = { isImportant = false },

        isVisibleCallback = function()
            return true;
        end,

        isAvailableCallback = function()
            return true;
        end,

        onClickCallback = function()
            UIStack.Get("Menu"):Push("GameMenu");
        end,
    },
};

export type MicroMenuEntryFlags =
{
    isImportant : boolean,
};

export type MicroMenuEntry =
{
    name : string,
    description : string,
    backgroundTexture : string,
    flags : MicroMenuEntryFlags,
    isVisibleCallback : () -> boolean,
    isAvailableCallback : () -> boolean,
    onClickCallback : () -> (),
};

export type MicroMenuAPI =
{
    Refresh : () -> (),
};

type MenuItem =
{
    entry : MicroMenuEntry,
    panel : Panel,
    icon : Panel,
    hovered : boolean,
    pressed : boolean,
    available : boolean,
    hasBeenAvailable : boolean,
    importantHighlight : boolean,
};

local ITEM_SIZE = 32;
local ITEM_GAP = 4;
local MENU_PADDING = 5;
local MENU_HEIGHT = ITEM_SIZE + (MENU_PADDING * 2);
local TOOLTIP_MIN_WIDTH = 140;
local TOOLTIP_HORIZONTAL_PADDING = 20;

local COLOR_MENU = vector.create(0.025, 0.030, 0.037);
local COLOR_ITEM = vector.create(0.065, 0.075, 0.088);
local COLOR_ITEM_HOVER = vector.create(0.115, 0.140, 0.170);
local COLOR_ITEM_PRESSED = vector.create(0.035, 0.045, 0.058);
local COLOR_BORDER = vector.create(0.260, 0.290, 0.325);
local COLOR_HOVER_BORDER = vector.create(0.390, 0.650, 0.960);
local COLOR_IMPORTANT = vector.create(1.000, 0.720, 0.150);
local COLOR_TEXT = vector.create(0.920, 0.935, 0.955);
local COLOR_DESCRIPTION = vector.create(0.680, 0.710, 0.750);

UI.RegisterTextTemplate("MicroMenuDescriptionText",
{
    font = "Data/Fonts/OpenSans-Regular.ttf",
    size = 8.0,
    color = COLOR_DESCRIPTION,
    borderSize = 1.0,
    borderColor = vector.create(0.0, 0.0, 0.0),
    wrapWidth = 240,
});

local microMenuAPI : MicroMenuAPI = { };
local menuPanel : Panel? = nil;
local tooltipPanel : Panel? = nil;
local tooltipName : TextWidget? = nil;
local tooltipDescription : TextWidget? = nil;
local menuItems : { MenuItem } = { };

local function RefreshItemVisuals(item : MenuItem)
    if not item.available then
        item.panel:SetColor(COLOR_ITEM);
        item.panel:SetAlpha(0.48);
        item.panel:SetBorderColor(COLOR_BORDER);
        item.icon:SetColor(COLOR_DESCRIPTION);
        item.icon:SetAlpha(0.48);
        item.icon:SetPosY(0);
    elseif item.pressed then
        item.panel:SetColor(COLOR_ITEM_PRESSED);
        item.panel:SetAlpha(1.0);
        item.panel:SetBorderColor(if item.importantHighlight then COLOR_IMPORTANT else COLOR_HOVER_BORDER);
        item.icon:SetColor(COLOR_TEXT);
        item.icon:SetAlpha(0.90);
        item.icon:SetPosY(-1);
    elseif item.hovered then
        item.panel:SetColor(COLOR_ITEM_HOVER);
        item.panel:SetAlpha(1.0);
        item.panel:SetBorderColor(if item.importantHighlight then COLOR_IMPORTANT else COLOR_HOVER_BORDER);
        item.icon:SetColor(COLOR_TEXT);
        item.icon:SetAlpha(1.0);
        item.icon:SetPosY(0);
    else
        item.panel:SetColor(COLOR_ITEM);
        item.panel:SetAlpha(0.92);
        item.panel:SetBorderColor(if item.importantHighlight then COLOR_IMPORTANT else COLOR_BORDER);
        item.icon:SetColor(COLOR_TEXT);
        item.icon:SetAlpha(0.92);
        item.icon:SetPosY(0);
    end
end

local function ShowTooltip(item : MenuItem)
    if not tooltipPanel or not tooltipName or not tooltipDescription then
        return;
    end

    tooltipName:SetText(item.entry.name);
    tooltipDescription:SetText(item.entry.description);
    tooltipName:ForceRefresh();
    tooltipDescription:ForceRefresh();

    local nameWidth, nameHeight = tooltipName:GetSize();
    local descriptionWidth, descriptionHeight = tooltipDescription:GetSize();
    local contentWidth = math.max(nameWidth, descriptionWidth) + TOOLTIP_HORIZONTAL_PADDING;
    tooltipPanel:SetSize(math.max(contentWidth, TOOLTIP_MIN_WIDTH), nameHeight + descriptionHeight + 24);
    tooltipPanel:SetEnabled(true);
end

local function HideTooltip()
    if tooltipPanel then
        tooltipPanel:SetEnabled(false);
    end
end

local function CreateMenuItem(parent : Panel, entry : MicroMenuEntry) : MenuItem
    local panel = parent:NewPanel(0, MENU_PADDING, ITEM_SIZE, ITEM_SIZE, 1, "DebugDarkGrey");
    panel:SetAnchor(0.0, 0.0);
    panel:SetRelativePoint(0.0, 0.0);
    panel:SetBorderSize(1);

    local icon = panel:NewPanel(0, 0, ITEM_SIZE - 8, ITEM_SIZE - 8, 2, "DebugDarkGrey");
    icon:SetAnchor(0.5, 0.5);
    icon:SetRelativePoint(0.5, 0.5);
    icon:SetBackground(entry.backgroundTexture);

    local item : MenuItem =
    {
        entry = entry,
        panel = panel,
        icon = icon,
        hovered = false,
        pressed = false,
        available = false,
        hasBeenAvailable = false,
        importantHighlight = false,
    };

    panel:SetOnHoverBegin(function()
        item.hovered = true;
        ShowTooltip(item);
        RefreshItemVisuals(item);
    end);
    panel:SetOnHoverEnd(function()
        item.hovered = false;
        item.pressed = false;
        HideTooltip();
        RefreshItemVisuals(item);
    end);
    panel:SetOnMouseDown(function()
        if not item.available then
            return;
        end

        item.pressed = true;
        RefreshItemVisuals(item);
    end);
    panel:SetOnMouseUp(function()
        local shouldActivate = item.available and item.pressed;
        item.pressed = false;

        if shouldActivate then
            item.importantHighlight = false;
            item.entry.onClickCallback();
        end

        RefreshItemVisuals(item);
    end);

    RefreshItemVisuals(item);
    return item;
end

local function CreateMicroMenu()
    local canvas = UI.GetCanvas("MicroMenu", 0, 0, 1920, 1080);

    menuPanel = canvas:NewPanel(-24, 24, MENU_PADDING * 2, MENU_HEIGHT, 100, "DebugDarkGrey");
    menuPanel:SetAnchor(1.0, 0.0);
    menuPanel:SetRelativePoint(1.0, 0.0);
    menuPanel:SetColor(COLOR_MENU);
    menuPanel:SetAlpha(0.96);
    menuPanel:SetBorderColor(COLOR_BORDER);
    menuPanel:SetBorderSize(1);

    tooltipPanel = canvas:NewPanel(-24, 84, 220, 58, 101, "DebugDarkGrey");
    tooltipPanel:SetEnabled(false);
    tooltipPanel:SetAnchor(1.0, 0.0);
    tooltipPanel:SetRelativePoint(1.0, 0.0);
    tooltipPanel:SetColor(COLOR_MENU);
    tooltipPanel:SetAlpha(0.98);
    tooltipPanel:SetBorderColor(COLOR_BORDER);
    tooltipPanel:SetBorderSize(1);

    tooltipName = tooltipPanel:NewText("", 10, -8, 1, "DefaultButtonText");
    tooltipName:SetAnchor(0.0, 1.0);
    tooltipName:SetRelativePoint(0.0, 1.0);
    tooltipName:SetFontSize(10);
    tooltipName:SetColor(COLOR_TEXT);

    tooltipDescription = tooltipPanel:NewText("", 10, -24, 1, "MicroMenuDescriptionText");
    tooltipDescription:SetAnchor(0.0, 1.0);
    tooltipDescription:SetRelativePoint(0.0, 1.0);

    for _, entry in MENU_ENTRIES do
        table.insert(menuItems, CreateMenuItem(menuPanel, entry));
    end

    microMenuAPI.Refresh();
end

microMenuAPI.Refresh = function()
    if not menuPanel then
        return;
    end

    HideTooltip();

    local visibleItemCount = 0;
    for _, item in menuItems do
        local isVisible = item.entry.isVisibleCallback();
        local isAvailable = item.entry.isAvailableCallback();

        item.panel:SetVisible(isVisible);
        item.available = isAvailable;
        item.hovered = false;
        item.pressed = false;

        if isVisible then
            item.panel:SetPosX(MENU_PADDING + (visibleItemCount * (ITEM_SIZE + ITEM_GAP)));
            item.panel:SetInteractable(true);
            visibleItemCount += 1;

            if isAvailable and not item.hasBeenAvailable then
                item.hasBeenAvailable = true;
                item.importantHighlight = item.entry.flags.isImportant;
            end
        end

        RefreshItemVisuals(item);
    end

    local contentWidth = (visibleItemCount * ITEM_SIZE) + (math.max(visibleItemCount - 1, 0) * ITEM_GAP);
    menuPanel:SetSize(contentWidth + (MENU_PADDING * 2), MENU_HEIGHT);
    menuPanel:SetVisible(visibleItemCount > 0);
end

RegisterEvent(GameEvent.Loaded, function()
    CreateMicroMenu();
end);

return microMenuAPI;
