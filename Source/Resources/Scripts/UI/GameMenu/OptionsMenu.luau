local UIStack = require("@src/API/UI/UIStack")
local UIButton = require("@src/API/UI/Button")
local UIInputBox = require("@src/API/UI/InputBox")
local UICheckbox = require("@src/API/UI/Checkbox")
local UIScrollBox = require("@src/API/UI/ScrollBox")
local InputAPI = require("@src/API/Input/Input")
local _ = require("@src/API/UI/WidgetExtensions")

local OPTIONS_WIDTH = 1024;
local OPTIONS_HEIGHT = 592;
local SIDEBAR_WIDTH = 160;
local FOOTER_HEIGHT = 38;
local CONTENT_PADDING = 14;
local CATEGORY_TOP = 12;
local CATEGORY_HEIGHT = 30;
local ACTION_ROW_HEIGHT = 28;
local BINDING_BUTTON_HEIGHT = 22;
local BINDING_CONTROL_CENTER_OFFSET = 1;
local TEXT_CENTER_OFFSET = 1;
local ACTION_TEXT_CENTER_OFFSET = TEXT_CENTER_OFFSET + BINDING_CONTROL_CENTER_OFFSET;
local DIALOG_MIN_WIDTH = 420;
local DIALOG_BUTTON_WIDTH = 130;
local DIALOG_BUTTON_HEIGHT = 26;
local DIALOG_BUTTON_GAP = 16;
local DIALOG_PADDING = 18;

local COLOR_WINDOW = vector.create(0.035, 0.042, 0.050);
local COLOR_SIDEBAR = vector.create(0.043, 0.052, 0.064);
local COLOR_PANEL = vector.create(0.055, 0.064, 0.074);
local COLOR_CONTROL = vector.create(0.070, 0.080, 0.092);
local COLOR_CONTROL_HOVER = vector.create(0.105, 0.125, 0.150);
local COLOR_SELECTED = vector.create(0.130, 0.155, 0.185);
local COLOR_BORDER = vector.create(0.225, 0.255, 0.290);
local COLOR_ACTION_WELL = vector.create(0.050, 0.062, 0.078);
local COLOR_ACTION_SECTION = vector.create(0.062, 0.078, 0.098);
local COLOR_ACTION_WELL_BORDER = vector.create(0.280, 0.340, 0.410);
local COLOR_ACCENT = vector.create(0.390, 0.650, 0.960);
local COLOR_DANGER = vector.create(0.820, 0.240, 0.240);
local COLOR_DANGER_HOVER = vector.create(1.000, 0.340, 0.340);
local COLOR_TEXT = vector.create(0.900, 0.915, 0.935);
local COLOR_MUTED = vector.create(0.570, 0.600, 0.640);

local function BindingChordsEqual(first : InputAPI.InputBinding?, second : InputAPI.InputBinding?) : boolean
    if first == nil or second == nil then
        return first == second;
    end

    -- Captures use Exact matching while existing defaults may use Any. The physical chord is
    -- still unchanged when its control and required modifiers match.
    return first.device == second.device
        and first.code == second.code
        and (first.modifiers or InputModifier.None) == (second.modifiers or InputModifier.None);
end

local function ConsumePointerInput()
end

local OPTIONS_CATEGORIES = {
    {
        name = "Gameplay",
        description = "Configure moment-to-moment gameplay preferences.",
        emptyTitle = "No gameplay options available",
        emptyDescription = "Gameplay settings will appear here as they are added.",
    },
    {
        name = "Controls",
        description = "Adjust camera, targeting, and control behavior.",
        emptyTitle = "No control options available",
        emptyDescription = "Control settings will appear here as they are added.",
    },
    {
        name = "Keybindings",
        description = "Review and customize keyboard and mouse bindings.",
    },
    {
        name = "Interface",
        description = "Customize the heads-up display and interface behavior.",
        emptyTitle = "No interface options available",
        emptyDescription = "Interface settings will appear here as they are added.",
    },
    {
        name = "Audio",
        description = "Adjust volume levels and audio behavior.",
        emptyTitle = "No audio options available",
        emptyDescription = "Audio settings will appear here as they are added.",
    },
};

local function CreateFooterButton(parent : Panel, text : string, width : number, emphasized : boolean)
    local button = UIButton.NewButton(parent, 0, 0, width, 26, 1, {
        text = text,
        panelTemplate = "DebugDarkGrey",
        textTemplate = "DefaultButtonText",
    });
    button.available = true;
    button.hovered = false;

    button.panel:SetColor(if emphasized then COLOR_SELECTED else COLOR_CONTROL);
    button.panel:SetAlpha(if emphasized then 0.95 else 0.78);
    button.panel:SetBorderColor(COLOR_BORDER);
    button.panel:SetBorderSize(1);
    button:SetFontSize(10);
    button.text:SetPosY(TEXT_CENTER_OFFSET);
    button.text:SetColor(COLOR_TEXT);

    local function RefreshVisuals()
        if (not button.available) then
            button.panel:SetColor(COLOR_CONTROL);
            button.panel:SetAlpha(0.42);
            button.panel:SetBorderColor(COLOR_BORDER);
            button.text:SetColor(COLOR_MUTED);
            button.text:SetAlpha(0.78);
        elseif (button.hovered) then
            button.panel:SetColor(COLOR_CONTROL_HOVER);
            button.panel:SetAlpha(1.0);
            button.panel:SetBorderColor(COLOR_ACCENT);
            button.text:SetColor(COLOR_TEXT);
            button.text:SetAlpha(1.0);
        else
            button.panel:SetColor(if emphasized then COLOR_SELECTED else COLOR_CONTROL);
            button.panel:SetAlpha(if emphasized then 0.95 else 0.78);
            button.panel:SetBorderColor(COLOR_BORDER);
            button.text:SetColor(COLOR_TEXT);
            button.text:SetAlpha(1.0);
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

local function CreateCategoryTab(parent : Panel, text : string, y : number)
    local button = UIButton.NewButton(parent, 0, y, SIDEBAR_WIDTH, CATEGORY_HEIGHT, 1, {
        text = text,
        panelTemplate = "DebugDarkGrey",
        textTemplate = "DefaultButtonText",
    });
    button.selected = false;
    button.hovered = false;

    button.panel:SetAnchor(0.0, 1.0);
    button.panel:SetRelativePoint(0.0, 1.0);
    button.panel:SetColor(COLOR_SELECTED);
    button.panel:SetAlpha(0.0);

    button.accent = button.panel:NewPanel(0, 0, 2, CATEGORY_HEIGHT, 2, "DebugDarkGrey");
    button.accent:SetAnchor(0.0, 1.0);
    button.accent:SetRelativePoint(0.0, 1.0);
    button.accent:SetColor(COLOR_ACCENT);
    button.accent:SetAlpha(0.0);

    button.text:SetAnchor(0.0, 0.5);
    button.text:SetRelativePoint(0.0, 0.5);
    button.text:SetPos(20, TEXT_CENTER_OFFSET);
    button:SetFontSize(12);
    button.text:SetColor(COLOR_TEXT);

    local function RefreshVisuals()
        if (button.selected) then
            button.panel:SetAlpha(0.80);
            button.accent:SetAlpha(1.0);
            button.text:SetAlpha(1.0);
        elseif (button.hovered) then
            button.panel:SetAlpha(0.38);
            button.accent:SetAlpha(0.0);
            button.text:SetAlpha(1.0);
        else
            button.panel:SetAlpha(0.0);
            button.accent:SetAlpha(0.0);
            button.text:SetAlpha(0.82);
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

    function button:SetSelected(selected : boolean)
        self.selected = selected;
        RefreshVisuals();
    end

    return button;
end

local function CreatePageHeader(page : Panel, titleText : string, descriptionText : string, width : number)
    local title = page:NewText(titleText, 0, 0, 1, "DefaultButtonText");
    title:SetAnchor(0.0, 1.0);
    title:SetRelativePoint(0.0, 1.0);
    title:SetFontSize(14);
    title:SetColor(COLOR_TEXT);

    local description = page:NewText(descriptionText, 0, -22, 1, "DefaultButtonText");
    description:SetAnchor(0.0, 1.0);
    description:SetRelativePoint(0.0, 1.0);
    description:SetFontSize(10);
    description:SetColor(COLOR_MUTED);

    local separator = page:NewPanel(0, -42, width, 1, 1, "DebugDarkGrey");
    separator:SetAnchor(0.0, 1.0);
    separator:SetRelativePoint(0.0, 1.0);
    separator:SetColor(COLOR_BORDER);
    separator:SetAlpha(0.72);
end

local function CreateEmptyCategoryPage(parent : Panel, category, width : number, height : number)
    local page = parent:NewPanel(0, 0, width, height, 1, "DebugDarkGrey");
    page:SetAnchor(0.0, 1.0);
    page:SetRelativePoint(0.0, 1.0);
    page:SetAlpha(0.0);

    CreatePageHeader(page, category.name, category.description, width);

    local sectionTitle = page:NewText("General", 0, -62, 1, "DefaultButtonText");
    sectionTitle:SetAnchor(0.0, 1.0);
    sectionTitle:SetRelativePoint(0.0, 1.0);
    sectionTitle:SetFontSize(11);
    sectionTitle:SetColor(COLOR_TEXT);

    local emptyPanel = page:NewPanel(0, -86, width, 120, 1, "DebugDarkGrey");
    emptyPanel:SetAnchor(0.0, 1.0);
    emptyPanel:SetRelativePoint(0.0, 1.0);
    emptyPanel:SetColor(COLOR_PANEL);
    emptyPanel:SetAlpha(0.55);
    emptyPanel:SetBorderColor(COLOR_BORDER);
    emptyPanel:SetBorderSize(1);

    local emptyTitle = emptyPanel:NewText(category.emptyTitle, 0, 10, 1, "DefaultButtonText");
    emptyTitle:SetAnchor(0.5, 0.5);
    emptyTitle:SetRelativePoint(0.5, 0.5);
    emptyTitle:SetFontSize(11);
    emptyTitle:SetColor(COLOR_TEXT);

    local emptyDescription = emptyPanel:NewText(category.emptyDescription, 0, -10, 1, "DefaultButtonText");
    emptyDescription:SetAnchor(0.5, 0.5);
    emptyDescription:SetRelativePoint(0.5, 0.5);
    emptyDescription:SetFontSize(9);
    emptyDescription:SetColor(COLOR_MUTED);

    return page;
end

local function CreateKeybindingsPage(parent : Panel, category, width : number, height : number)
    local page = parent:NewPanel(0, 0, width, height, 1, "DebugDarkGrey");
    page:SetAnchor(0.0, 1.0);
    page:SetRelativePoint(0.0, 1.0);
    page:SetAlpha(0.0);

    CreatePageHeader(page, category.name, category.description, width);

    local tableHeight = height - 106;
    local bindingsTable = page:NewPanel(0, -94, width, tableHeight, 1, "DebugDarkGrey");
    bindingsTable:SetAnchor(0.0, 1.0);
    bindingsTable:SetRelativePoint(0.0, 1.0);
    bindingsTable:SetColor(COLOR_ACTION_WELL);
    bindingsTable:SetAlpha(1.0);
    bindingsTable:SetBorderColor(COLOR_ACTION_WELL_BORDER);
    bindingsTable:SetBorderSize(1);

    local toolbar = page:NewPanel(0, -58, width, 26, 1, "DebugDarkGrey");
    toolbar:SetAnchor(0.0, 1.0);
    toolbar:SetRelativePoint(0.0, 1.0);
    toolbar:SetAlpha(0.0);

    local searchInput = UIInputBox.NewInputBox(toolbar, 0, 0, 230, 26, 1, {
        panelTemplate = "DebugDarkGrey",
        textTemplate = "DefaultButtonText",
        placeholderText = "Search bindings",
    });
    searchInput.button.panel:SetColor(COLOR_CONTROL);
    searchInput.button.panel:SetAlpha(0.65);
    searchInput.button.panel:SetBorderColor(COLOR_BORDER);
    searchInput.button.panel:SetBorderSize(1);
    searchInput.button:SetFontSize(10);
    searchInput.button.text:SetColor(COLOR_MUTED);
    searchInput:SetTextVerticallyCentered();
    searchInput.button.text:SetPosX(10);
    searchInput.button.text:SetPosY(searchInput.button.text:GetPosY() + TEXT_CENTER_OFFSET);

    local searchHovered = false;
    local function RefreshSearchVisuals()
        if searchInput.isFocused then
            searchInput.button.panel:SetColor(COLOR_SELECTED);
            searchInput.button.panel:SetAlpha(0.92);
            searchInput.button.panel:SetBorderColor(COLOR_ACCENT);
            searchInput.button.text:SetColor(COLOR_TEXT);
        elseif searchHovered then
            searchInput.button.panel:SetColor(COLOR_CONTROL_HOVER);
            searchInput.button.panel:SetAlpha(0.88);
            searchInput.button.panel:SetBorderColor(COLOR_BORDER);
            searchInput.button.text:SetColor(COLOR_TEXT);
        else
            searchInput.button.panel:SetColor(COLOR_CONTROL);
            searchInput.button.panel:SetAlpha(0.65);
            searchInput.button.panel:SetBorderColor(COLOR_BORDER);
            searchInput.button.text:SetColor(COLOR_MUTED);
        end
    end
    searchInput.button.panel:SetOnHoverBegin(function()
        searchHovered = true;
        RefreshSearchVisuals();
    end);
    searchInput.button.panel:SetOnHoverEnd(function()
        searchHovered = false;
        RefreshSearchVisuals();
    end);
    searchInput:SetOnFocusBegin(RefreshSearchVisuals);
    searchInput:SetOnFocusEnd(RefreshSearchVisuals);

    local presetPanel = toolbar:NewPanel(width - 170, 0, 170, 26, 1, "DebugDarkGrey");
    presetPanel:SetColor(COLOR_CONTROL);
    presetPanel:SetAlpha(0.65);
    presetPanel:SetBorderColor(COLOR_BORDER);
    presetPanel:SetBorderSize(1);

    local presetText = presetPanel:NewText("Preset: Default", 10, TEXT_CENTER_OFFSET, 1, "DefaultButtonText");
    presetText:SetAnchor(0.0, 0.5);
    presetText:SetRelativePoint(0.0, 0.5);
    presetText:SetFontSize(10);
    presetText:SetColor(COLOR_MUTED);

    local conflictsCheckbox = UICheckbox.NewCheckbox(toolbar, 248, 0, 14, 14, 1, {
        backgroundTemplate = "DebugDarkGrey",
        fillTemplate = "DebugDarkGrey",
    });
    conflictsCheckbox.checkbox:SetAnchor(0.0, 0.5);
    conflictsCheckbox.checkbox:SetRelativePoint(0.0, 0.5);
    conflictsCheckbox.background:SetAnchor(0.0, 0.5);
    conflictsCheckbox.background:SetRelativePoint(0.0, 0.5);
    conflictsCheckbox.background:SetBackground("texture/ui/adventure/2x/checkbox_grey_empty.png");
    conflictsCheckbox.fill:SetPos(0, 0);
    conflictsCheckbox.fill:SetBackground("texture/ui/adventure/2x/checkbox_grey_checked.png");

    local conflictsText = toolbar:NewText("Show conflicts only", 270, TEXT_CENTER_OFFSET, 1, "DefaultButtonText");
    conflictsText:SetAnchor(0.0, 0.5);
    conflictsText:SetRelativePoint(0.0, 0.5);
    conflictsText:SetFontSize(10);
    conflictsText:SetColor(COLOR_MUTED);
    conflictsText:SetOnMouseUp(function()
        conflictsCheckbox:Click();
    end);

    local function SetConflictsHover(hovered : boolean)
        conflictsCheckbox.background:SetColor(if hovered then COLOR_ACCENT else COLOR_TEXT);
        conflictsCheckbox.background:SetAlpha(if hovered then 1.0 else 0.82);
        conflictsCheckbox.fill:SetColor(if hovered then COLOR_ACCENT else COLOR_TEXT);
        conflictsCheckbox.fill:SetAlpha(if hovered then 1.0 else 0.92);
        conflictsText:SetColor(if hovered then COLOR_TEXT else COLOR_MUTED);
    end
    conflictsCheckbox.background:SetOnHoverBegin(function()
        SetConflictsHover(true);
    end);
    conflictsCheckbox.background:SetOnHoverEnd(function()
        SetConflictsHover(false);
    end);
    conflictsCheckbox.fill:SetOnHoverBegin(function()
        SetConflictsHover(true);
    end);
    conflictsCheckbox.fill:SetOnHoverEnd(function()
        SetConflictsHover(false);
    end);
    conflictsText:SetOnHoverBegin(function()
        SetConflictsHover(true);
    end);
    conflictsText:SetOnHoverEnd(function()
        SetConflictsHover(false);
    end);
    SetConflictsHover(false);

    local tableHeader = bindingsTable:NewPanel(0, 0, width, 26, 1, "DebugDarkGrey");
    tableHeader:SetAnchor(0.0, 1.0);
    tableHeader:SetRelativePoint(0.0, 1.0);
    tableHeader:SetColor(COLOR_ACTION_SECTION);
    tableHeader:SetAlpha(1.0);

    local primaryX = math.floor(width * 0.29);
    local secondaryX = math.floor(width * 0.62);
    local primaryColumnWidth = secondaryX - primaryX - 8;
    local secondaryColumnWidth = width - secondaryX - 18;
    local primaryButtonWidth = math.min(primaryColumnWidth - 34, 250);
    local secondaryButtonWidth = math.min(secondaryColumnWidth - 34, 250);

    local actionHeader = tableHeader:NewText("Action", 12, TEXT_CENTER_OFFSET, 1, "DefaultButtonText");
    actionHeader:SetAnchor(0.0, 0.5);
    actionHeader:SetRelativePoint(0.0, 0.5);
    actionHeader:SetFontSize(10);
    actionHeader:SetColor(COLOR_TEXT);

    local primaryHeaderCell = tableHeader:NewPanel(primaryX, 0, primaryButtonWidth, 26, 1, "DebugDarkGrey");
    primaryHeaderCell:SetAnchor(0.0, 1.0);
    primaryHeaderCell:SetRelativePoint(0.0, 1.0);
    primaryHeaderCell:SetAlpha(0.0);

    local primaryHeader = primaryHeaderCell:NewText("Primary Binding", 0, TEXT_CENTER_OFFSET, 1, "DefaultButtonText");
    primaryHeader:SetAnchor(0.5, 0.5);
    primaryHeader:SetRelativePoint(0.5, 0.5);
    primaryHeader:SetFontSize(10);
    primaryHeader:SetColor(COLOR_TEXT);

    local secondaryHeaderCell = tableHeader:NewPanel(secondaryX, 0, secondaryButtonWidth, 26, 1, "DebugDarkGrey");
    secondaryHeaderCell:SetAnchor(0.0, 1.0);
    secondaryHeaderCell:SetRelativePoint(0.0, 1.0);
    secondaryHeaderCell:SetAlpha(0.0);

    local secondaryHeader = secondaryHeaderCell:NewText("Secondary Binding", 0, TEXT_CENTER_OFFSET, 1, "DefaultButtonText");
    secondaryHeader:SetAnchor(0.5, 0.5);
    secondaryHeader:SetRelativePoint(0.5, 0.5);
    secondaryHeader:SetFontSize(10);
    secondaryHeader:SetColor(COLOR_TEXT);

    local emptyTitle = bindingsTable:NewText("No bindings loaded", 0, 9, 1, "DefaultButtonText");
    emptyTitle:SetAnchor(0.5, 0.5);
    emptyTitle:SetRelativePoint(0.5, 0.5);
    emptyTitle:SetFontSize(11);
    emptyTitle:SetColor(COLOR_TEXT);

    local emptyDescription = bindingsTable:NewText("Input-system integration will populate this table.", 0, -9, 1, "DefaultButtonText");
    emptyDescription:SetAnchor(0.5, 0.5);
    emptyDescription:SetRelativePoint(0.5, 0.5);
    emptyDescription:SetFontSize(9);
    emptyDescription:SetColor(COLOR_MUTED);

    local scrollBox = UIScrollBox.NewScrollBox(bindingsTable, 1, -26, width - 2, tableHeight - 27, 1, {
        panelTemplate = "DebugDarkGrey",
        contentSizeX = width - 18,
        contentSizeY = tableHeight - 27,
        verticalScrollBar = true,
        scrollBarWidth = 10,
        alpha = 0.0,
    });
    scrollBox:SetAnchor(0.0, 1.0);
    scrollBox:SetRelativePoint(0.0, 1.0);

    local dialog = page:NewPanel(0, 0, width, height, 20, "DebugDarkGrey");
    dialog:SetAnchor(0.0, 1.0);
    dialog:SetRelativePoint(0.0, 1.0);
    dialog:SetColor(COLOR_WINDOW);
    dialog:SetAlpha(0.92);
    dialog:SetOnMouseDown(ConsumePointerInput);
    dialog:SetOnMouseUp(ConsumePointerInput);
    dialog:SetOnMouseHeld(ConsumePointerInput);
    dialog:SetOnMouseScroll(ConsumePointerInput);
    dialog:SetOnHoverBegin(ConsumePointerInput);
    dialog:SetOnHoverEnd(ConsumePointerInput);
    dialog:SetOnHoverHeld(ConsumePointerInput);
    dialog:SetVisible(false);

    local dialogBox = dialog:NewPanel(0, 0, DIALOG_MIN_WIDTH, 140, 1, "DebugDarkGrey");
    dialogBox:SetAnchor(0.5, 0.5);
    dialogBox:SetRelativePoint(0.5, 0.5);
    dialogBox:SetColor(COLOR_PANEL);
    dialogBox:SetAlpha(1.0);
    dialogBox:SetBorderColor(COLOR_BORDER);
    dialogBox:SetBorderSize(1);

    local dialogTitle = dialogBox:NewText("Binding conflict", 18, -18, 1, "DefaultButtonText");
    dialogTitle:SetAnchor(0.0, 1.0);
    dialogTitle:SetRelativePoint(0.0, 1.0);
    dialogTitle:SetFontSize(13);
    dialogTitle:SetColor(COLOR_TEXT);

    local dialogDescription = dialogBox:NewText("", 18, -48, 1, "DefaultButtonText");
    dialogDescription:SetAnchor(0.0, 1.0);
    dialogDescription:SetRelativePoint(0.0, 1.0);
    dialogDescription:SetFontSize(9);
    dialogDescription:SetColor(COLOR_MUTED);
    dialogDescription:SetWrapWidth(DIALOG_MIN_WIDTH - (DIALOG_PADDING * 2));

    local dialogButtons = {};
    for index = 1, 4 do
        local button = CreateFooterButton(dialogBox, "", DIALOG_BUTTON_WIDTH, index == 1);
        button:SetAnchor(0.0, 0.0);
        button:SetRelativePoint(0.0, 0.0);
        button:SetPos(DIALOG_PADDING + ((index - 1) * (DIALOG_BUTTON_WIDTH + DIALOG_BUTTON_GAP)), DIALOG_PADDING);
        button:SetVisible(false);
        dialogButtons[index] = button;
    end

    local controller = {
        panel = page,
        actions = {},
        baseline = {},
        rowWidgets = {},
        dirty = false,
        activeCapture = nil,
        conflictsOnly = false,
        onDirtyChanged = nil,
    };

    local function SetDirty(dirty : boolean)
        controller.dirty = dirty;
        if controller.onDirtyChanged ~= nil then
            controller.onDirtyChanged(dirty);
        end
    end

    local function CancelCapture()
        local ownedCapture = controller.activeCapture ~= nil;
        controller.activeCapture = nil;
        if ownedCapture and InputAPI.IsBindingCaptureActive() then
            InputAPI.CancelBindingCapture();
        end
    end

    local function HideDialog()
        dialog:SetVisible(false);
        for _, button in dialogButtons do
            button:SetVisible(false);
        end
    end

    local function ApplyBinding(actionName : string, slot : number, binding : InputAPI.InputBinding, policy : number)
        local result = InputAPI.SetBinding(actionName, slot, binding, policy);
        if result.succeeded and #result.mutations > 0 then
            SetDirty(true);
        end
        HideDialog();
        controller:Refresh();
    end

    local function CancelConflictDialog()
        HideDialog();
        if controller.panel:IsVisible() then
            controller:Refresh();
        end
    end

    local function LayoutDialog(description : string, buttonCount : number)
        local buttonsWidth = (buttonCount * DIALOG_BUTTON_WIDTH) + ((buttonCount - 1) * DIALOG_BUTTON_GAP);
        local dialogWidth = math.max(DIALOG_MIN_WIDTH, buttonsWidth + (DIALOG_PADDING * 2));
        local buttonStartX = math.floor((dialogWidth - buttonsWidth) * 0.5);

        dialogBox:SetWidth(dialogWidth);
        dialogDescription:SetWrapWidth(dialogWidth - (DIALOG_PADDING * 2));
        dialogDescription:SetText(description);

        local descriptionHeight = dialogDescription:GetHeight();
        if descriptionHeight <= 0 then
            descriptionHeight = 12;
        end
        local dialogHeight = 48 + descriptionHeight + DIALOG_PADDING + DIALOG_BUTTON_HEIGHT + DIALOG_PADDING;
        dialogBox:SetHeight(math.max(132, dialogHeight));

        for index, button in dialogButtons do
            button:SetPos(buttonStartX + ((index - 1) * (DIALOG_BUTTON_WIDTH + DIALOG_BUTTON_GAP)), DIALOG_PADDING);
        end
    end

    local function ConfigureDialogButton(index : number, text : string, callback)
        local button = dialogButtons[index];
        button:SetText(text);
        button:SetVisible(true);
        button:SetOnMouseUp(callback);
    end

    local function ShowConflictDialog(action, slot : number, binding : InputAPI.InputBinding, conflicts)
        local direct = {};
        local shadows = {};
        for _, conflict in conflicts do
            local other = controller.actions[conflict.action];
            local otherName = if other ~= nil then other.displayName else conflict.action;
            if conflict.kind == InputBindingConflictKind.Direct then
                table.insert(direct, otherName);
            elseif conflict.kind == InputBindingConflictKind.ShadowsRequested then
                table.insert(shadows, `{otherName} has context priority and may shadow this binding.`);
            else
                table.insert(shadows, `{action.displayName} has context priority and may shadow {otherName}.`);
            end
        end

        local lines = {};
        if #direct > 0 then
            table.insert(lines, `Direct conflict: {table.concat(direct, ", ")}.`);
        end
        if #shadows > 0 then
            table.insert(lines, `Shadow warning: {table.concat(shadows, " ")}`);
        end
        local buttonCount = if #direct == 1 then 4 elseif #direct > 1 then 3 else 2;
        LayoutDialog(table.concat(lines, "\n"), buttonCount);
        dialog:SetVisible(true);

        if #direct == 1 then
            ConfigureDialogButton(1, "Swap", function()
                ApplyBinding(action.name, slot, binding, InputBindingConflictPolicy.Swap);
            end);
            ConfigureDialogButton(2, "Replace", function()
                ApplyBinding(action.name, slot, binding, InputBindingConflictPolicy.Replace);
            end);
            ConfigureDialogButton(3, "Keep Both", function()
                ApplyBinding(action.name, slot, binding, InputBindingConflictPolicy.Allow);
            end);
            ConfigureDialogButton(4, "Cancel", CancelConflictDialog);
        elseif #direct > 1 then
            ConfigureDialogButton(1, "Replace conflicts", function()
                ApplyBinding(action.name, slot, binding, InputBindingConflictPolicy.Replace);
            end);
            ConfigureDialogButton(2, "Keep Both", function()
                ApplyBinding(action.name, slot, binding, InputBindingConflictPolicy.Allow);
            end);
            ConfigureDialogButton(3, "Cancel", CancelConflictDialog);
        else
            ConfigureDialogButton(1, "Keep Both", function()
                ApplyBinding(action.name, slot, binding, InputBindingConflictPolicy.Allow);
            end);
            ConfigureDialogButton(2, "Cancel", CancelConflictDialog);
        end
    end

    local function BeginCapture(action, slot : number, button)
        CancelCapture();
        HideDialog();
        controller.activeCapture = { action = action.name, slot = slot };
        button:SetText("Press a key...");

        local started = InputAPI.BeginBindingCapture(function(binding)
            controller.activeCapture = nil;
            if binding == nil then
                if controller.panel:IsVisible() then
                    controller:Refresh();
                end
                return;
            end

            if BindingChordsEqual(InputAPI.GetBinding(action.name, slot), binding) then
                if controller.panel:IsVisible() then
                    controller:Refresh();
                end
                return;
            end

            local preview = InputAPI.FindBindingConflicts(action.name, slot, binding);
            if #preview.conflicts == 0 then
                ApplyBinding(action.name, slot, binding, InputBindingConflictPolicy.Allow);
            else
                ShowConflictDialog(action, slot, binding, preview.conflicts);
            end
        end);

        if not started then
            controller.activeCapture = nil;
            controller:Refresh();
        end
    end

    local function HasDirectConflict(action) : boolean
        for slot = 1, 2 do
            local binding = action.bindings[slot];
            if binding ~= nil then
                local result = InputAPI.FindBindingConflicts(action.name, slot, binding);
                for _, conflict in result.conflicts do
                    if conflict.kind == InputBindingConflictKind.Direct then
                        return true;
                    end
                end
            end
        end
        return false;
    end

    local function MatchesSearch(action, query : string) : boolean
        if query == "" then
            return true;
        end

        local searchable = table.concat({
            action.displayName,
            action.name,
            action.category,
            action.context,
            InputAPI.FormatBinding(action.bindings[1]),
            InputAPI.FormatBinding(action.bindings[2]),
        }, " ");
        return string.find(string.lower(searchable), query, 1, true) ~= nil;
    end

    local function CreateBindingSlot(row : Panel, action, slot : number, x : number, buttonWidth : number)
        local binding = action.bindings[slot];
        local button = UIButton.NewButton(row, 0, 0, buttonWidth, BINDING_BUTTON_HEIGHT, 1, {
            text = InputAPI.FormatBinding(action.bindings[slot]),
            panelTemplate = "DebugDarkGrey",
            textTemplate = "DefaultButtonText",
        });
        button.panel:SetAnchor(0.0, 0.5);
        button.panel:SetRelativePoint(0.0, 0.5);
        button.panel:SetPos(x, BINDING_CONTROL_CENTER_OFFSET);
        button.panel:SetColor(COLOR_CONTROL);
        button.panel:SetAlpha(0.72);
        button.panel:SetBorderColor(COLOR_BORDER);
        button.panel:SetBorderSize(1);
        button:SetFontSize(9);
        button.text:SetPosY(TEXT_CENTER_OFFSET);
        button.panel:SetOnHoverBegin(function()
            button.panel:SetColor(COLOR_CONTROL_HOVER);
            button.panel:SetAlpha(1.0);
            button.panel:SetBorderColor(COLOR_ACCENT);
            button.text:SetColor(COLOR_TEXT);
        end);
        button.panel:SetOnHoverEnd(function()
            button.panel:SetColor(COLOR_CONTROL);
            button.panel:SetAlpha(0.72);
            button.panel:SetBorderColor(COLOR_BORDER);
            button.text:SetColor(COLOR_TEXT);
        end);
        button:SetOnMouseUp(function()
            BeginCapture(action, slot, button);
        end);

        local clearIcon = row:NewPanel(0, 0, 14, 14, 1, "DebugDarkGrey");
        clearIcon:SetAnchor(0.0, 0.5);
        clearIcon:SetRelativePoint(0.0, 0.5);
        clearIcon:SetPos(x + buttonWidth + 8, BINDING_CONTROL_CENTER_OFFSET);
        clearIcon:SetBackground("texture/cursor/32x32/plain/disabled.png");
        clearIcon:SetColor(if binding ~= nil then COLOR_DANGER else COLOR_MUTED);
        clearIcon:SetAlpha(if binding ~= nil then 0.88 else 0.42);
        clearIcon:SetInteractable(binding ~= nil);

        if binding ~= nil then
            clearIcon:SetOnHoverBegin(function()
                clearIcon:SetColor(COLOR_DANGER_HOVER);
                clearIcon:SetAlpha(1.0);
            end);
            clearIcon:SetOnHoverEnd(function()
                clearIcon:SetColor(COLOR_DANGER);
                clearIcon:SetAlpha(0.88);
            end);
            clearIcon:SetOnMouseUp(function()
                local result = InputAPI.ClearBinding(action.name, slot, InputBindingConflictPolicy.Allow);
                if result.succeeded and #result.mutations > 0 then
                    SetDirty(true);
                end
                controller:Refresh();
            end);
        end
    end

    function controller:Refresh()
        CancelCapture();
        local previousScrollPosY = scrollBox.scrollPosY or 0;
        for _, widget in self.rowWidgets do
            UI.DestroyWidget(widget);
        end
        table.clear(self.rowWidgets);
        table.clear(self.actions);

        local actions = InputAPI.GetActions();
        for _, action in actions do
            self.actions[action.name] = action;
        end

        local query = string.lower(searchInput.text or "");
        local conflictsOnly = self.conflictsOnly;
        local y = -4;
        local visibleCount = 0;
        local currentCategory = nil;
        for _, action in actions do
            if action.rebindable and MatchesSearch(action, query) and (not conflictsOnly or HasDirectConflict(action)) then
                if currentCategory ~= action.category then
                    if currentCategory ~= nil then
                        y -= 6;
                    end
                    currentCategory = action.category;
                    local categoryHeader = scrollBox.content:NewPanel(2, y, width - 22, 24, 0, "DebugDarkGrey");
                    categoryHeader:SetAnchor(0.0, 1.0);
                    categoryHeader:SetRelativePoint(0.0, 1.0);
                    categoryHeader:SetColor(COLOR_ACTION_SECTION);
                    categoryHeader:SetAlpha(1.0);
                    table.insert(self.rowWidgets, categoryHeader);

                    local categoryText = categoryHeader:NewText(currentCategory, 10, TEXT_CENTER_OFFSET, 1, "DefaultButtonText");
                    categoryText:SetAnchor(0.0, 0.5);
                    categoryText:SetRelativePoint(0.0, 0.5);
                    categoryText:SetFontSize(10);
                    categoryText:SetColor(COLOR_ACCENT);

                    local categoryDivider = scrollBox.content:NewPanel(2, y - 23, width - 22, 1, 1, "DebugDarkGrey");
                    categoryDivider:SetAnchor(0.0, 1.0);
                    categoryDivider:SetRelativePoint(0.0, 1.0);
                    categoryDivider:SetColor(COLOR_BORDER);
                    categoryDivider:SetAlpha(0.72);
                    table.insert(self.rowWidgets, categoryDivider);
                    y -= 26;
                end

                local row = scrollBox.content:NewPanel(2, y, width - 22, ACTION_ROW_HEIGHT, 0, "DebugDarkGrey");
                row:SetAnchor(0.0, 1.0);
                row:SetRelativePoint(0.0, 1.0);
                row:SetColor(COLOR_ACTION_WELL);
                row:SetAlpha(1.0);
                table.insert(self.rowWidgets, row);

                local actionText = row:NewText(action.displayName, 24, ACTION_TEXT_CENTER_OFFSET, 1, "DefaultButtonText");
                actionText:SetAnchor(0.0, 0.5);
                actionText:SetRelativePoint(0.0, 0.5);
                actionText:SetFontSize(9);
                actionText:SetColor(COLOR_TEXT);

                local rowDivider = scrollBox.content:NewPanel(2, y - ACTION_ROW_HEIGHT + 1, width - 22, 1, 1, "DebugDarkGrey");
                rowDivider:SetAnchor(0.0, 1.0);
                rowDivider:SetRelativePoint(0.0, 1.0);
                rowDivider:SetColor(COLOR_BORDER);
                rowDivider:SetAlpha(0.64);
                table.insert(self.rowWidgets, rowDivider);

                CreateBindingSlot(row, action, 1, primaryX - 2, primaryButtonWidth);
                CreateBindingSlot(row, action, 2, secondaryX - 2, secondaryButtonWidth);
                y -= ACTION_ROW_HEIGHT;
                visibleCount += 1;
            end
        end

        emptyTitle:SetVisible(visibleCount == 0);
        emptyDescription:SetVisible(visibleCount == 0);
        if visibleCount == 0 then
            emptyTitle:SetText(if conflictsOnly then "No binding conflicts" else "No matching bindings");
            emptyDescription:SetText(if conflictsOnly
                then "All current bindings are conflict-free."
                else "Change the search to show more actions.");
        end

        local contentHeight = math.max(tableHeight - 27, -y + 4);
        scrollBox:SetContentHeight(contentHeight);
        scrollBox:SetVerticalScrollBarVisible(contentHeight > tableHeight - 27);

        local maxScrollPosY = math.max(0, scrollBox.content:GetHeight() - scrollBox.scrollBox:GetHeight());
        scrollBox.scrollPosY = math.clamp(previousScrollPosY, 0, maxScrollPosY);
        if maxScrollPosY > 0 then
            scrollBox:Scroll(nil, 0, 0);
        else
            scrollBox:ScrollToStart();
        end
    end

    function controller:Open()
        CancelCapture();
        HideDialog();
        table.clear(self.baseline);
        for _, action in InputAPI.GetActions() do
            if action.rebindable then
                self.baseline[action.name] = {
                    action.bindings[1] or false,
                    action.bindings[2] or false,
                };
            end
        end
        SetDirty(false);
        self:Refresh();
    end

    function controller:Apply() : boolean
        CancelCapture();
        HideDialog();
        if not self.dirty then
            return true;
        end
        if not InputAPI.SaveBindings() then
            return false;
        end

        self:Open();
        return true;
    end

    function controller:Cancel()
        CancelCapture();
        HideDialog();
        if self.dirty then
            for actionName, bindings in self.baseline do
                for slot = 1, 2 do
                    if bindings[slot] == false then
                        InputAPI.ClearBinding(actionName, slot, InputBindingConflictPolicy.Allow);
                    else
                        InputAPI.SetBinding(actionName, slot, bindings[slot], InputBindingConflictPolicy.Allow);
                    end
                end
            end
        end
        SetDirty(false);
        self:Refresh();
    end

    function controller:Reset()
        CancelCapture();
        HideDialog();
        for _, action in InputAPI.GetActions() do
            if action.rebindable then
                for slot = 1, 2 do
                    InputAPI.ResetBinding(action.name, slot, InputBindingConflictPolicy.Allow);
                end
            end
        end
        SetDirty(true);
        self:Refresh();
    end

    function controller:IsDirty() : boolean
        return self.dirty;
    end

    function controller:Deactivate()
        CancelCapture();
        HideDialog();
    end

    searchInput:SetOnTextChanged(function()
        controller:Refresh();
    end);
    conflictsCheckbox:SetOnValueChanged(function(_, checked : boolean)
        controller.conflictsOnly = checked;
        controller:Refresh();
    end);

    return controller;
end

local function CreateOptionsMenu(stack)
    local canvas = UI.GetCanvas("OptionsMenu", 0, 0, 1920, 1080);
    local panel = canvas:NewPanel(0, 0, OPTIONS_WIDTH, OPTIONS_HEIGHT, 0, "DebugDarkGrey");
    panel:SetEnabled(false);
    panel:SetAnchor(0.5, 0.5);
    panel:SetRelativePoint(0.5, 0.5);
    panel:SetColor(COLOR_WINDOW);
    panel:SetAlpha(1.0);
    panel:SetBorderColor(COLOR_BORDER);
    panel:SetBorderSize(1);

    local dragHandle = panel:NewPanel(SIDEBAR_WIDTH, 0, OPTIONS_WIDTH - SIDEBAR_WIDTH, 50, 3, "DebugDarkGrey");
    dragHandle:SetAnchor(0.0, 1.0);
    dragHandle:SetRelativePoint(0.0, 1.0);
    dragHandle:SetAlpha(0.0);
    panel:MakeDraggable(dragHandle);

    local bodyHeight = OPTIONS_HEIGHT - FOOTER_HEIGHT;
    local sidebar = panel:NewPanel(0, 0, SIDEBAR_WIDTH, OPTIONS_HEIGHT, 1, "DebugDarkGrey");
    sidebar:SetAnchor(0.0, 1.0);
    sidebar:SetRelativePoint(0.0, 1.0);
    sidebar:SetColor(COLOR_SIDEBAR);
    sidebar:SetAlpha(1.0);
    sidebar:SetBorderColor(COLOR_BORDER);
    sidebar:SetBorderSize(1);

    local contentWidth = OPTIONS_WIDTH - SIDEBAR_WIDTH - (CONTENT_PADDING * 2);
    local contentHeight = bodyHeight - CONTENT_PADDING;
    local content = panel:NewPanel(SIDEBAR_WIDTH + CONTENT_PADDING, -CONTENT_PADDING, contentWidth, contentHeight, 1, "DebugDarkGrey");
    content:SetAnchor(0.0, 1.0);
    content:SetRelativePoint(0.0, 1.0);
    content:SetAlpha(0.0);

    local pages = {};
    local categoryButtons = {};
    local keybindingsController = nil;
    local applyButton = nil;
    local resetAllButton = nil;

    for index, category in OPTIONS_CATEGORIES do
        if (category.name == "Keybindings") then
            keybindingsController = CreateKeybindingsPage(content, category, contentWidth, contentHeight);
            pages[index] = keybindingsController.panel;
        else
            pages[index] = CreateEmptyCategoryPage(content, category, contentWidth, contentHeight);
        end
        pages[index]:SetVisible(false);

        local buttonY = -(CATEGORY_TOP + ((index - 1) * CATEGORY_HEIGHT));
        categoryButtons[index] = CreateCategoryTab(sidebar, category.name, buttonY);
    end

    local function SelectCategory(selectedIndex : number)
        local isKeybindings = OPTIONS_CATEGORIES[selectedIndex].name == "Keybindings";
        for index, page in pages do
            local isSelected = index == selectedIndex;
            page:SetVisible(isSelected);
            categoryButtons[index]:SetSelected(isSelected);
        end
        if isKeybindings then
            keybindingsController:Refresh();
        else
            keybindingsController:Deactivate();
        end
        if resetAllButton ~= nil then
            resetAllButton:SetAvailable(isKeybindings);
        end
    end

    for index, button in categoryButtons do
        button:SetOnMouseUp(function()
            SelectCategory(index);
        end);
    end

    SelectCategory(1);

    local footer = panel:NewPanel(SIDEBAR_WIDTH, 0, OPTIONS_WIDTH - SIDEBAR_WIDTH, FOOTER_HEIGHT, 2, "DebugDarkGrey");
    footer:SetAnchor(0.0, 0.0);
    footer:SetRelativePoint(0.0, 0.0);
    footer:SetAlpha(0.0);

    resetAllButton = CreateFooterButton(footer, "Reset to Default", 150, false);
    resetAllButton:SetAnchor(0.0, 0.5);
    resetAllButton:SetRelativePoint(0.0, 0.5);
    resetAllButton:SetPos(CONTENT_PADDING, 0);
    resetAllButton:SetAvailable(OPTIONS_CATEGORIES[1].name == "Keybindings");
    resetAllButton:SetOnMouseUp(function()
        keybindingsController:Reset();
    end);

    local cancelButton = CreateFooterButton(footer, "Cancel", 100, false);
    cancelButton:SetAnchor(1.0, 0.5);
    cancelButton:SetRelativePoint(1.0, 0.5);
    cancelButton:SetPos(-(CONTENT_PADDING + 108), 0);
    cancelButton:SetOnMouseUp(function()
        UIStack.Get("Menu"):Push("GameMenu");
    end);

    applyButton = CreateFooterButton(footer, "Apply", 100, true);
    applyButton:SetAnchor(1.0, 0.5);
    applyButton:SetRelativePoint(1.0, 0.5);
    applyButton:SetPos(-CONTENT_PADDING, 0);
    applyButton:SetAvailable(false);
    applyButton:SetOnMouseUp(function()
        keybindingsController:Apply();
    end);

    keybindingsController.onDirtyChanged = function(dirty : boolean)
        applyButton:SetAvailable(dirty);
    end;

    stack:Register("OptionsMenu", panel, 0, function()
        panel:SetEnabled(true);
        keybindingsController:Open();
    end, function()
        keybindingsController:Cancel();
        panel:SetEnabled(false);
    end);
end

local function OnGameLoaded(eventID : number, data : any)
    CreateOptionsMenu(UIStack.Get("Menu"));
end
RegisterEvent(GameEvent.Loaded, OnGameLoaded)
