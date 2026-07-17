local UIStack = require("@src/API/UI/UIStack")
local UIButton = require("@src/API/UI/Button")
local InputAPI = require("@src/API/Input/Input")

local MENU_WIDTH = 250;
local MENU_HEIGHT = 378;
local MENU_PADDING = 16;
local BUTTON_WIDTH = MENU_WIDTH - (MENU_PADDING * 2);
local BUTTON_HEIGHT = 36;
local TEXT_CENTER_OFFSET = 1;

local COLOR_BACKDROP = vector.create(0.005, 0.007, 0.010);
local COLOR_WINDOW = vector.create(0.035, 0.042, 0.050);
local COLOR_CONTROL = vector.create(0.070, 0.080, 0.092);
local COLOR_CONTROL_HOVER = vector.create(0.105, 0.125, 0.150);
local COLOR_SELECTED = vector.create(0.130, 0.155, 0.185);
local COLOR_BORDER = vector.create(0.225, 0.255, 0.290);
local COLOR_ACCENT = vector.create(0.390, 0.650, 0.960);
local COLOR_TEXT = vector.create(0.900, 0.915, 0.935);
local COLOR_MUTED = vector.create(0.570, 0.600, 0.640);

local ICON_ROOT = "texture/ui/icons/50x50/white/";
local ICON_CHEVRON = "texture/ui/icons/100x100/white/arrow_right.png";

local function ConsumePointerInput()
end

local function ToggleEscapeMenu(panelWrapper)
    local stack = UIStack.Get("Menu");

    if (panelWrapper.isOpen) then
        stack:Pop();
    else
        stack:Push("GameMenu");
    end

    return true;
end

local function CreateMenuButton(parent : Panel, text : string, iconPath : string, y : number, emphasized : boolean)
    local button = UIButton.NewButton(parent, MENU_PADDING, y, BUTTON_WIDTH, BUTTON_HEIGHT, 1, {
        text = text,
        panelTemplate = "DebugDarkGrey",
        textTemplate = "DefaultButtonText",
    });
    button.hovered = false;
    button.available = true;

    button.panel:SetAnchor(0.0, 1.0);
    button.panel:SetRelativePoint(0.0, 1.0);
    button.panel:SetColor(if emphasized then COLOR_SELECTED else COLOR_CONTROL);
    button.panel:SetAlpha(if emphasized then 0.96 else 0.82);
    button.panel:SetBorderColor(if emphasized then COLOR_ACCENT else COLOR_BORDER);
    button.panel:SetBorderSize(1);

    button.text:SetAnchor(0.0, 0.5);
    button.text:SetRelativePoint(0.0, 0.5);
    button.text:SetPos(46, TEXT_CENTER_OFFSET);
    button.text:SetColor(COLOR_TEXT);
    button:SetFontSize(10);

    button.icon = button.panel:NewPanel(14, 0, 18, 18, 2, "DebugDarkGrey");
    button.icon:SetAnchor(0.0, 0.5);
    button.icon:SetRelativePoint(0.0, 0.5);
    button.icon:SetBackground(iconPath);
    button.icon:SetColor(if emphasized then COLOR_ACCENT else COLOR_TEXT);
    button.icon:SetAlpha(0.92);

    button.accessory = button.panel:NewPanel(-12, 0, 16, 16, 2, "DebugDarkGrey");
    button.accessory:SetAnchor(1.0, 0.5);
    button.accessory:SetRelativePoint(1.0, 0.5);
    button.accessory:SetBackground(ICON_CHEVRON);
    button.accessory:SetColor(COLOR_MUTED);
    button.accessory:SetAlpha(0.88);

    local function RefreshVisuals()
        if not button.available then
            button.panel:SetColor(COLOR_CONTROL);
            button.panel:SetAlpha(0.42);
            button.panel:SetBorderColor(COLOR_BORDER);
            button.text:SetColor(COLOR_MUTED);
            button.text:SetAlpha(0.72);
            button.icon:SetColor(COLOR_MUTED);
            button.icon:SetAlpha(0.52);
            button.accessory:SetColor(COLOR_MUTED);
            button.accessory:SetAlpha(0.42);
        elseif button.hovered then
            button.panel:SetColor(COLOR_CONTROL_HOVER);
            button.panel:SetAlpha(1.0);
            button.panel:SetBorderColor(COLOR_ACCENT);
            button.text:SetColor(COLOR_TEXT);
            button.text:SetAlpha(1.0);
            button.icon:SetColor(COLOR_ACCENT);
            button.icon:SetAlpha(1.0);
            button.accessory:SetColor(COLOR_ACCENT);
            button.accessory:SetAlpha(1.0);
        else
            button.panel:SetColor(if emphasized then COLOR_SELECTED else COLOR_CONTROL);
            button.panel:SetAlpha(if emphasized then 0.96 else 0.82);
            button.panel:SetBorderColor(if emphasized then COLOR_ACCENT else COLOR_BORDER);
            button.text:SetColor(COLOR_TEXT);
            button.text:SetAlpha(1.0);
            button.icon:SetColor(if emphasized then COLOR_ACCENT else COLOR_TEXT);
            button.icon:SetAlpha(0.92);
            button.accessory:SetColor(if emphasized then COLOR_ACCENT else COLOR_MUTED);
            button.accessory:SetAlpha(0.88);
        end
    end

    button.panel:SetOnHoverBegin(function()
        button.hovered = true;
        RefreshVisuals();
    end);
    button.panel:SetOnHoverEnd(function()
        button.hovered = false;
        RefreshVisuals();
    end);

    function button:SetAvailable(available : boolean)
        self.available = available;
        self.hovered = false;
        self:SetInteractable(available);
        RefreshVisuals();
    end

    return button;
end

local function CreateGameMenu(stack)
    local canvas = UI.GetCanvas("EscapeMenu", 0, 0, 1920, 1080);

    local backdrop = canvas:NewPanel(0, 0, 1920, 1080, 0, "DebugDarkGrey");
    backdrop:SetEnabled(false);
    backdrop:SetColor(COLOR_BACKDROP);
    backdrop:SetAlpha(0.58);
    backdrop:SetOnMouseDown(ConsumePointerInput);
    backdrop:SetOnMouseUp(ConsumePointerInput);
    backdrop:SetOnMouseHeld(ConsumePointerInput);
    backdrop:SetOnMouseScroll(ConsumePointerInput);

    local panel = backdrop:NewPanel(0, 0, MENU_WIDTH, MENU_HEIGHT, 1, "DebugDarkGrey");
    panel:SetAnchor(0.5, 0.5);
    panel:SetRelativePoint(0.5, 0.5);
    panel:SetColor(COLOR_WINDOW);
    panel:SetAlpha(0.99);
    panel:SetBorderColor(COLOR_BORDER);
    panel:SetBorderSize(1);

    local headerText = panel:NewText("Game Menu", 0, -24, 1, "DefaultButtonText");
    headerText:SetAnchor(0.5, 1.0);
    headerText:SetRelativePoint(0.5, 0.5);
    headerText:SetFontSize(16);
    headerText:SetColor(COLOR_TEXT);

    local headerDivider = panel:NewPanel(MENU_PADDING, -50, BUTTON_WIDTH, 1, 1, "DebugDarkGrey");
    headerDivider:SetAnchor(0.0, 1.0);
    headerDivider:SetRelativePoint(0.0, 1.0);
    headerDivider:SetColor(COLOR_BORDER);
    headerDivider:SetAlpha(0.72);

    local headerAccent = panel:NewPanel(0, -50, 28, 2, 2, "DebugDarkGrey");
    headerAccent:SetAnchor(0.5, 1.0);
    headerAccent:SetRelativePoint(0.5, 1.0);
    headerAccent:SetColor(COLOR_ACCENT);
    headerAccent:SetAlpha(1.0);

    local returnButton = CreateMenuButton(panel, "Return to Game", `{ICON_ROOT}return.png`, -62, false);
    returnButton.accessory:SetVisible(false);
    returnButton.shortcut = returnButton.panel:NewText("ESC", -12, TEXT_CENTER_OFFSET, 2, "DefaultButtonText");
    returnButton.shortcut:SetAnchor(1.0, 0.5);
    returnButton.shortcut:SetRelativePoint(1.0, 0.5);
    returnButton.shortcut:SetFontSize(8);
    returnButton.shortcut:SetColor(COLOR_ACCENT);
    returnButton:SetOnMouseUp(function()
        UIStack.Get("Menu"):Pop();
    end);

    local optionsButton = CreateMenuButton(panel, "Options", `{ICON_ROOT}gear.png`, -103, false);
    optionsButton:SetOnMouseUp(function()
        UIStack.Get("Menu"):Push("OptionsMenu");
    end);

    local macrosButton = CreateMenuButton(panel, "Macros", `{ICON_ROOT}menu_grid.png`, -144, false);
    macrosButton:SetAvailable(false);

    local shopButton = CreateMenuButton(panel, "Shop", `{ICON_ROOT}cart.png`, -185, false);
    shopButton:SetAvailable(false);

    local supportButton = CreateMenuButton(panel, "Support", `{ICON_ROOT}question.png`, -226, false);
    supportButton:SetAvailable(false);

    local sectionDivider = panel:NewPanel(MENU_PADDING, -272, BUTTON_WIDTH, 1, 1, "DebugDarkGrey");
    sectionDivider:SetAnchor(0.0, 1.0);
    sectionDivider:SetRelativePoint(0.0, 1.0);
    sectionDivider:SetColor(COLOR_BORDER);
    sectionDivider:SetAlpha(0.72);

    local logoutButton = CreateMenuButton(panel, "Logout", `{ICON_ROOT}door.png`, -284, false);
    logoutButton:SetOnMouseUp(function()
        Logout();
    end);

    local exitButton = CreateMenuButton(panel, "Exit Game", `{ICON_ROOT}power.png`, -325, false);
    exitButton:SetOnMouseUp(function()
        ExitGame();
    end);

    local panelWrapper = {
        isOpen = false,
    }

    stack:Register("GameMenu", backdrop, 0, function()
        panelWrapper.isOpen = true;
        backdrop:SetEnabled(true);
    end, function()
        panelWrapper.isOpen = false;
        backdrop:SetEnabled(false);
    end);

    local binding = InputAPI.Keyboard(InputAPI.KeyValues.ESCAPE, InputModifier.None, InputModifierMatch.Any);
    InputAPI.Bind("ToggleGameMenu", "Toggle Game Menu", "Interface", binding, function(phase)
        if phase ~= InputPhase.Pressed then
            return InputReply.Ignored;
        end

        return if ToggleEscapeMenu(panelWrapper) then InputReply.Consumed else InputReply.Ignored;
    end, { defaultReply = InputReply.Ignored });
end

local function OnGameLoaded(eventID : number, data : any)
    CreateGameMenu(UIStack.Get("Menu"));
end
RegisterEvent(GameEvent.Loaded, OnGameLoaded)
