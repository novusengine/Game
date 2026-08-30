local FrameManager = require("@src/API/Game/FrameManager")
local UIButton = require("@src/API/UI/Button")
local InputAPI = require("@src/API/Input/Input")
local EditMode = require("@src/API/Game/EditMode")

local MENU_WIDTH = 250;
local MENU_HEIGHT = 419;
local MENU_PADDING = 16;
local BUTTON_WIDTH = MENU_WIDTH - (MENU_PADDING * 2);
local BUTTON_HEIGHT = 36;
local TEXT_CENTER_OFFSET = 1;

local COLOR_BACKDROP = vec.New(0.005, 0.007, 0.010);
local COLOR_WINDOW = vec.New(0.035, 0.042, 0.050);
local COLOR_CONTROL = vec.New(0.070, 0.080, 0.092);
local COLOR_CONTROL_HOVER = vec.New(0.105, 0.125, 0.150);
local COLOR_SELECTED = vec.New(0.130, 0.155, 0.185);
local COLOR_BORDER = vec.New(0.225, 0.255, 0.290);
local COLOR_ACCENT = vec.New(0.390, 0.650, 0.960);
local COLOR_TEXT = vec.New(0.900, 0.915, 0.935);
local COLOR_MUTED = vec.New(0.570, 0.600, 0.640);

local ICON_ROOT = "texture/ui/icons/50x50/white/";
local ICON_CHEVRON = "texture/ui/icons/100x100/white/arrow_right.png";

local function ConsumePointerInput()
end

local function ToggleEscapeMenu()
    return FrameManager.HandleEscape("GameMenu");
end

local function CreateMenuButton(parent : Panel, text : string, iconPath : string, y : number, emphasized : boolean)
    local button = UIButton.New(parent, vec.New(MENU_PADDING, y), vec.New(BUTTON_WIDTH, BUTTON_HEIGHT), 1i, {
        text = text,
        panelTemplate = "DebugDarkGrey",
        textTemplate = "DefaultButtonText",
    });
    button.hovered = false;
    button.available = true;

    button.root:SetAnchor(vec.New(0.0, 1.0));
    button.root:SetRelativePoint(vec.New(0.0, 1.0));
    button.root:SetColor(if emphasized then COLOR_SELECTED else COLOR_CONTROL);
    button.root:SetAlpha(if emphasized then 0.96 else 0.82);
    button.root:SetBorderColor(if emphasized then COLOR_ACCENT else COLOR_BORDER);
    button.root:SetBorderSize(1);

    button.text:SetAnchor(vec.New(0.0, 0.5));
    button.text:SetRelativePoint(vec.New(0.0, 0.5));
    button.text:SetPos(vec.New(46, TEXT_CENTER_OFFSET));
    button.text:SetColor(COLOR_TEXT);
    button:SetFontSize(10);

    button.icon = button.root:NewPanel(vec.New(14, 0), vec.New(18, 18), 2i, "DebugDarkGrey");
    button.icon:SetAnchor(vec.New(0.0, 0.5));
    button.icon:SetRelativePoint(vec.New(0.0, 0.5));
    button.icon:SetBackground(iconPath);
    button.icon:SetColor(if emphasized then COLOR_ACCENT else COLOR_TEXT);
    button.icon:SetAlpha(0.92);

    button.accessory = button.root:NewPanel(vec.New(-12, 0), vec.New(16, 16), 2i, "DebugDarkGrey");
    button.accessory:SetAnchor(vec.New(1.0, 0.5));
    button.accessory:SetRelativePoint(vec.New(1.0, 0.5));
    button.accessory:SetBackground(ICON_CHEVRON);
    button.accessory:SetColor(COLOR_MUTED);
    button.accessory:SetAlpha(0.88);

    local function RefreshVisuals()
        if not button.available then
            button.root:SetColor(COLOR_CONTROL);
            button.root:SetAlpha(0.42);
            button.root:SetBorderColor(COLOR_BORDER);
            button.text:SetColor(COLOR_MUTED);
            button.text:SetAlpha(0.72);
            button.icon:SetColor(COLOR_MUTED);
            button.icon:SetAlpha(0.52);
            button.accessory:SetColor(COLOR_MUTED);
            button.accessory:SetAlpha(0.42);
        elseif button.hovered then
            button.root:SetColor(COLOR_CONTROL_HOVER);
            button.root:SetAlpha(1.0);
            button.root:SetBorderColor(COLOR_ACCENT);
            button.text:SetColor(COLOR_TEXT);
            button.text:SetAlpha(1.0);
            button.icon:SetColor(COLOR_ACCENT);
            button.icon:SetAlpha(1.0);
            button.accessory:SetColor(COLOR_ACCENT);
            button.accessory:SetAlpha(1.0);
        else
            button.root:SetColor(if emphasized then COLOR_SELECTED else COLOR_CONTROL);
            button.root:SetAlpha(if emphasized then 0.96 else 0.82);
            button.root:SetBorderColor(if emphasized then COLOR_ACCENT else COLOR_BORDER);
            button.text:SetColor(COLOR_TEXT);
            button.text:SetAlpha(1.0);
            button.icon:SetColor(if emphasized then COLOR_ACCENT else COLOR_TEXT);
            button.icon:SetAlpha(0.92);
            button.accessory:SetColor(if emphasized then COLOR_ACCENT else COLOR_MUTED);
            button.accessory:SetAlpha(0.88);
        end
    end

    button.root:SetOnHoverBegin(function()
        button.hovered = true;
        RefreshVisuals();
    end);
    button.root:SetOnHoverEnd(function()
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

local function CreateGameMenu()
    local canvas = UI.GetCanvas("EscapeMenu", vec.New(0, 0), vec.New(1920, 1080));

    local backdrop = canvas:NewPanel(vec.New(0, 0), vec.New(1920, 1080), 0i, "DebugDarkGrey");
    backdrop:SetEnabled(false);
    backdrop:SetColor(COLOR_BACKDROP);
    backdrop:SetAlpha(0.58);
    backdrop:SetOnMouseDown(ConsumePointerInput);
    backdrop:SetOnMouseUp(ConsumePointerInput);
    backdrop:SetOnMouseHeld(ConsumePointerInput);
    backdrop:SetOnMouseScroll(ConsumePointerInput);

    local panel = backdrop:NewPanel(vec.New(0, 0), vec.New(MENU_WIDTH, MENU_HEIGHT), 1i, "DebugDarkGrey");
    panel:SetAnchor(vec.New(0.5, 0.5));
    panel:SetRelativePoint(vec.New(0.5, 0.5));
    panel:SetColor(COLOR_WINDOW);
    panel:SetAlpha(0.99);
    panel:SetBorderColor(COLOR_BORDER);
    panel:SetBorderSize(1);

    local headerText = panel:NewText("Game Menu", vec.New(0, -24), 1i, "DefaultButtonText");
    headerText:SetAnchor(vec.New(0.5, 1.0));
    headerText:SetRelativePoint(vec.New(0.5, 0.5));
    headerText:SetFontSize(16);
    headerText:SetColor(COLOR_TEXT);

    local headerDivider = panel:NewPanel(vec.New(MENU_PADDING, -50), vec.New(BUTTON_WIDTH, 1), 1i, "DebugDarkGrey");
    headerDivider:SetAnchor(vec.New(0.0, 1.0));
    headerDivider:SetRelativePoint(vec.New(0.0, 1.0));
    headerDivider:SetColor(COLOR_BORDER);
    headerDivider:SetAlpha(0.72);

    local headerAccent = panel:NewPanel(vec.New(0, -50), vec.New(28, 2), 2i, "DebugDarkGrey");
    headerAccent:SetAnchor(vec.New(0.5, 1.0));
    headerAccent:SetRelativePoint(vec.New(0.5, 1.0));
    headerAccent:SetColor(COLOR_ACCENT);
    headerAccent:SetAlpha(1.0);

    local returnButton = CreateMenuButton(panel, "Return to Game", `{ICON_ROOT}return.png`, -62, false);
    returnButton.accessory:SetVisible(false);
    returnButton.shortcut = returnButton.root:NewText("ESC", vec.New(-12, TEXT_CENTER_OFFSET), 2i, "DefaultButtonText");
    returnButton.shortcut:SetAnchor(vec.New(1.0, 0.5));
    returnButton.shortcut:SetRelativePoint(vec.New(1.0, 0.5));
    returnButton.shortcut:SetFontSize(8);
    returnButton.shortcut:SetColor(COLOR_ACCENT);
    returnButton:SetOnMouseUp(function()
        FrameManager.Close("GameMenu");
    end);

    local optionsButton = CreateMenuButton(panel, "Options", `{ICON_ROOT}gear.png`, -103, false);
    optionsButton:SetOnMouseUp(function()
        FrameManager.Open("OptionsMenu");
    end);

    local editModeButton = CreateMenuButton(panel, "Edit Mode", `{ICON_ROOT}gear.png`, -144, false);
    editModeButton:SetOnMouseUp(function()
        FrameManager.Close("GameMenu");
        EditMode.Enter();
    end);

    local macrosButton = CreateMenuButton(panel, "Macros", `{ICON_ROOT}menu_grid.png`, -185, false);
    macrosButton:SetAvailable(false);

    local shopButton = CreateMenuButton(panel, "Shop", `{ICON_ROOT}cart.png`, -226, false);
    shopButton:SetAvailable(false);

    local supportButton = CreateMenuButton(panel, "Support", `{ICON_ROOT}question.png`, -267, false);
    supportButton:SetAvailable(false);

    local sectionDivider = panel:NewPanel(vec.New(MENU_PADDING, -313), vec.New(BUTTON_WIDTH, 1), 1i, "DebugDarkGrey");
    sectionDivider:SetAnchor(vec.New(0.0, 1.0));
    sectionDivider:SetRelativePoint(vec.New(0.0, 1.0));
    sectionDivider:SetColor(COLOR_BORDER);
    sectionDivider:SetAlpha(0.72);

    local logoutButton = CreateMenuButton(panel, "Logout", `{ICON_ROOT}door.png`, -325, false);
    logoutButton:SetOnMouseUp(function()
        Logout();
    end);

    local exitButton = CreateMenuButton(panel, "Exit Game", `{ICON_ROOT}power.png`, -366, false);
    exitButton:SetOnMouseUp(function()
        ExitGame();
    end);

    FrameManager.Register("GameMenu", {
        root = backdrop,
        role = "System",
        onOpen = function()
        backdrop:SetEnabled(true);
        end,
        onClose = function()
            backdrop:SetEnabled(false);
        end,
    });

    local binding = InputAPI.Keyboard(InputAPI.KeyValues.ESCAPE, InputModifier.None, InputModifierMatch.Any);
    InputAPI.Bind("ToggleGameMenu", "Toggle Game Menu", "Interface", binding, function(phase)
        if phase ~= InputPhase.Pressed then
            return InputReply.Ignored;
        end

        return if ToggleEscapeMenu() then InputReply.Consumed else InputReply.Ignored;
    end, { defaultReply = InputReply.Ignored });
end

local function OnGameLoaded(eventID : integer, data : any)
    CreateGameMenu();
end
RegisterEvent(GameEvent.Loaded, OnGameLoaded);
