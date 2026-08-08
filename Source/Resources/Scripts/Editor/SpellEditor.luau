--[[
Spell Editor - Luau presentation for the authoritative native spell-authoring backend.

The script owns layout, navigation, draft visualization, confirmations, and diagnostics.
Stable IDs, ClientDB relationships, validation, serialization, networking, and mutation
correlation remain in the developer-only SpellEditor native API.
]]

local Components = {
    Checkbox = require("@src/API/UI/Checkbox"),
    Dropdown = require("@src/API/UI/Dropdown"),
    Button = require("@src/API/UI/Button"),
    EditorRegistry = require("@src/API/Editor/Registry"),
    InputBox = require("@src/API/UI/InputBox"),
    Modal = require("@src/API/UI/Modal"),
    SelectionGroup = require("@src/API/UI/SelectionGroup"),
    ScrollBox = require("@src/API/UI/ScrollBox"),
    TextUtil = require("@src/API/UI/TextUtil"),
    VirtualList = require("@src/API/UI/VirtualList"),
    EditorWindow = require("@src/API/Editor/UI/Window"),
};

local Layout = {
    WindowWidth = 1344,
    WindowHeight = 810,
    TitleHeight = 30,
    HeaderHeight = 56,
    TabHeight = 36,
    SummaryHeight = 96,
    BrowserWidth = 280,
    BrowserRowHeight = 36,
    BrowserRowStride = 37,
    InspectorWidth = 270,
    Layer = 1120i,
    Padding = 8,
    ControlHeight = 32,
    FieldLabelWidth = 150,
    RowHeight = 38,
    InputHeight = 32,
    PanelSpacing = 8,
    SectionTitleY = -12,
    SectionTitleTemplate = "SpellEditorTitle",
};
Layout.BodyWidth = Layout.WindowWidth - Layout.BrowserWidth - Layout.InspectorWidth;
Layout.BodyHeight = Layout.WindowHeight - Layout.TitleHeight - Layout.HeaderHeight - Layout.TabHeight - Layout.SummaryHeight;

local FONT_SIZE = {
    Caption = 8,
    Row = 10,
    Body = 10,
    Header = 12,
    Title = 14,
};

local TEXT = "SpellEditorText";

local PARAMETER_KIND = {
    Unused = 0i,
    Integer = 1i,
    Boolean = 2i,
    Enum = 3i,
    Flags = 4i,
    SpellID = 5i,
    FactionID = 6i,
    Standing = 7i,
    DurationMilliseconds = 8i,
    IntervalMilliseconds = 9i,
};

local Colors = {
    Window = vec.New(0.035, 0.052, 0.067),
    Title = vec.New(0.018, 0.030, 0.041),
    Panel = vec.New(0.055, 0.075, 0.092),
    PanelAlt = vec.New(0.072, 0.094, 0.112),
    Border = vec.New(0.13, 0.20, 0.25),
    Text = vec.New(0.86, 0.89, 0.92),
    Muted = vec.New(0.56, 0.62, 0.68),
    Accent = vec.New(0.04, 0.47, 0.91),
    Success = vec.New(0.25, 0.80, 0.35),
    Warning = vec.New(1.00, 0.63, 0.05),
    Error = vec.New(0.98, 0.25, 0.25),
};

local TABS = { "Overview", "Targeting", "Effects", "Aura", "Constraints", "Procs" };

UI.RegisterPanelTemplate("SpellEditorFlatPanel",
{
    color = Colors.Panel,
    cornerRadius = 0.0,
    borderColor = Colors.Panel,
    borderSize = 0.0,
});
UI.RegisterPanelTemplate("SpellEditorBrowserRow",
{
    color = Colors.PanelAlt,
    alpha = 0.75,
    cornerRadius = 0.0,
    borderColor = Colors.PanelAlt,
    borderSize = 0.0,
    onHoverTemplate = "SpellEditorBrowserRowHover",
});
UI.RegisterPanelTemplate("SpellEditorBrowserRowHover",
{
    color = vec.New(0.16, 0.31, 0.42),
    alpha = 1.0,
    borderColor = vec.New(0.16, 0.31, 0.42),
    borderSize = 0.0,
});
UI.RegisterPanelTemplate("SpellEditorInput",
{
    color = vec.New(0.035, 0.055, 0.070),
    cornerRadius = 2.0,
});
UI.RegisterPanelTemplate("SpellEditorCheckbox",
{
    color = vec.New(0.055, 0.080, 0.100),
    cornerRadius = 2.0,
});
UI.RegisterPanelTemplate("SpellEditorCheckboxFill",
{
    color = Colors.Accent,
    cornerRadius = 2.0,
});
UI.RegisterTextTemplate("SpellEditorText",
{
    font = "Data/Fonts/OpenSans-Regular.ttf",
    size = FONT_SIZE.Body,
    color = Colors.Text,
});
UI.RegisterTextTemplate("SpellEditorTitle",
{
    font = "Data/Fonts/OpenSans-Regular.ttf",
    size = FONT_SIZE.Title,
    color = Colors.Text,
});
UI.RegisterTextTemplate("SpellEditorHeaderTitle",
{
    font = "Data/Fonts/OpenSans-Regular.ttf",
    size = FONT_SIZE.Header,
    color = Colors.Text,
});
UI.RegisterTextTemplate("SpellEditorEffectName",
{
    font = "Data/Fonts/OpenSans-Regular.ttf",
    size = FONT_SIZE.Row,
    color = Colors.Text,
});
UI.RegisterTextTemplate("SpellEditorMuted",
{
    font = "Data/Fonts/OpenSans-Regular.ttf",
    size = FONT_SIZE.Caption,
    color = Colors.Muted,
});

local BrowserLayout = {
    PrimaryTextHeight = math.ceil(UI.CalculateTextSize("Ag", "SpellEditorEffectName").y),
    TagTextHeight = math.ceil(UI.CalculateTextSize("Ag", "SpellEditorMuted").y),
    TextGap = 1,
};
BrowserLayout.PrimaryTextY = (BrowserLayout.TagTextHeight + BrowserLayout.TextGap) * 0.5;
BrowserLayout.TagY = -(BrowserLayout.PrimaryTextHeight + BrowserLayout.TextGap) * 0.5;
BrowserLayout.IDX = Layout.BrowserWidth - 70;
BrowserLayout.NameWidth = BrowserLayout.IDX - 50;

local Widgets = {};
local DataLookup = {};
local Dialogs = {};
local Actions = {};
local ViewBuilders = {};

function Widgets.NewPanel(parent, x, y, width, height, color, alpha)
    local panel = parent:NewPanel(vec.New(x, y), vec.New(width, height), 1i, "DebugDarkGrey");
    panel:SetAnchor(vec.New(0.0, 1.0));
    panel:SetRelativePoint(vec.New(0.0, 1.0));
    panel:SetColor(color or Colors.Panel);
    panel:SetAlpha(alpha or 1.0);
    panel:SetBorderColor(Colors.Border);
    panel:SetBorderSize(1);
    return panel;
end

function Widgets.NewText(parent, value, x, y, template, color)
    local text = parent:NewText(value, vec.New(x, y), 2i, template or "SpellEditorText");
    text:SetAnchor(vec.New(0.0, 1.0));
    text:SetRelativePoint(vec.New(0.0, 1.0));
    text:SetInteractable(false);
    if color then
        text:SetColor(color);
    end
    return text;
end

function Widgets.NewWrappedText(parent, value, x, y, width, template, color)
    local text = Widgets.NewText(parent, value, x, y, template, color);
    text:SetWrapWidth(width);
    return text;
end

function Widgets.NewSingleLineText(parent, value, x, y, width, template, color)
    local textTemplate = template or "SpellEditorText";
    return Widgets.NewText(parent, Components.TextUtil.TruncateToWidth(tostring(value), textTemplate, math.max(0, width)), x, y, textTemplate, color);
end

function Widgets.NewVerticallyCenteredText(parent, value, x, y, template, color)
    local text = Widgets.NewText(parent, value, x, y, template, color);
    text:SetAnchor(vec.New(0.0, 0.5));
    text:SetRelativePoint(vec.New(0.0, 0.5));
    return text;
end

function Widgets.NewVerticallyCenteredSingleLineText(parent, value, x, y, width, template, color)
    local textTemplate = template or "SpellEditorText";
    local displayedValue = Components.TextUtil.TruncateToWidth(tostring(value), textTemplate, math.max(0, width));
    return Widgets.NewVerticallyCenteredText(parent, displayedValue, x, y, textTemplate, color);
end

function Widgets.NewButton(parent, value, x, y, width, height, callback, accented, textAlign, textPadX)
    local displayValue = Components.TextUtil.TruncateToWidth(tostring(value), TEXT, math.max(0, width - 16));
    local button = Components.Button.New(parent, vec.New(x, y), vec.New(width, height), 2i, {
        text = displayValue,
        textTemplate = TEXT,
        textAlign = textAlign,
        textPadX = textPadX,
        color = accented and vec.New(0.02, 0.31, 0.68) or Colors.PanelAlt,
        hoverColor = accented and Colors.Accent or vec.New(0.16, 0.31, 0.42),
        alpha = accented and 0.85 or 0.75,
        hoverAlpha = 1.0,
        borderColor = Colors.Border,
        borderSize = 1,
    });
    button:SetAnchor(vec.New(0.0, 1.0));
    button:SetRelativePoint(vec.New(0.0, 1.0));
    if callback then
        button:SetOnMouseUp(callback);
    end
    return button;
end

function Widgets.NewIconButton(parent, texture, x, y, size, callback, color, hoverColor)
    local iconColor = color or vec.New(1, 1, 1);
    local button = Components.Button.New(parent, vec.New(x, y), vec.New(size, size), 2i, {
        text = "",
        panelTemplate = "DebugDarkGrey",
        color = iconColor,
        hoverColor = hoverColor or iconColor,
        alpha = 0.72,
        hoverAlpha = 1.0,
        disabledAlpha = 0.22,
        borderSize = 0,
    });
    button:SetAnchor(vec.New(0.0, 1.0));
    button:SetRelativePoint(vec.New(0.0, 1.0));
    button.root:SetBackground(texture);
    if callback then
        button:SetOnMouseUp(callback);
    end
    return button;
end

local canvas = UI.GetCanvas("SpellEditor", vec.New(0, 0), vec.New(1920, 1080), false);
local editorWindow = Components.EditorWindow.New(canvas, vec.New(0, 0), vec.New(Layout.WindowWidth, Layout.WindowHeight), Layout.Layer, {
    title = "Spell Editor",
    titleHeight = Layout.TitleHeight,
    padding = Layout.Padding,
    windowColor = Colors.Window,
    titleColor = Colors.Title,
    alpha = 0.985,
    closeSize = vec.New(26, 22),
});
local window = editorWindow.root;
window:SetAnchor(vec.New(0.5, 0.5));
window:SetRelativePoint(vec.New(0.5, 0.5));
local titleBar = editorWindow.titleBar;
local closeButton = editorWindow.closeButton;

local browserPanel = Widgets.NewPanel(window, 0, -Layout.TitleHeight, Layout.BrowserWidth, Layout.WindowHeight - Layout.TitleHeight, Colors.Panel, 1.0);
local headerPanel = Widgets.NewPanel(window, Layout.BrowserWidth, -Layout.TitleHeight, Layout.BodyWidth + Layout.InspectorWidth, Layout.HeaderHeight, Colors.Title, 0.96);
local tabsPanel = Widgets.NewPanel(window, Layout.BrowserWidth, -(Layout.TitleHeight + Layout.HeaderHeight), Layout.BodyWidth, Layout.TabHeight, Colors.Panel, 1.0);
local workspacePanel = Widgets.NewPanel(window, Layout.BrowserWidth, -(Layout.TitleHeight + Layout.HeaderHeight + Layout.TabHeight), Layout.BodyWidth, Layout.BodyHeight, Colors.Window, 1.0);
local inspectorPanel = Widgets.NewPanel(window, Layout.BrowserWidth + Layout.BodyWidth, -(Layout.TitleHeight + Layout.HeaderHeight), Layout.InspectorWidth, Layout.TabHeight + Layout.BodyHeight, Colors.Panel, 1.0);
local summaryPanel = Widgets.NewPanel(window, Layout.BrowserWidth, -(Layout.TitleHeight + Layout.HeaderHeight + Layout.TabHeight + Layout.BodyHeight), Layout.BodyWidth + Layout.InspectorWidth, Layout.SummaryHeight, Colors.Title, 0.98);
summaryPanel:SetClipChildren(true);
Layout.InspectorViewportHeight = Layout.TabHeight + Layout.BodyHeight - Layout.Padding * 2;
Layout.InspectorViewportWidth = Layout.InspectorWidth - Layout.Padding * 2;
Layout.InspectorScrollBarWidth = 9;
local inspectorScroll = Components.ScrollBox.New(inspectorPanel, vec.New(Layout.Padding, -Layout.Padding), vec.New(Layout.InspectorViewportWidth, Layout.InspectorViewportHeight), 2i, {
    panelTemplate = "DebugDarkGrey",
    contentSizeX = Layout.InspectorViewportWidth,
    contentSizeY = Layout.InspectorViewportHeight,
    verticalScrollBar = true,
    scrollBarWidth = Layout.InspectorScrollBarWidth,
    alpha = 0.0,
});
inspectorScroll:SetAnchor(vec.New(0.0, 1.0));
inspectorScroll:SetRelativePoint(vec.New(0.0, 1.0));

local browserTitleText = Widgets.NewText(browserPanel, "Spell Browser (0 Total)", Layout.Padding, -8, "SpellEditorTitle", Colors.Text);
local searchInput = Components.InputBox.New(browserPanel, vec.New(Layout.Padding, -42), vec.New(Layout.BrowserWidth - 98, Layout.ControlHeight), 2i, {
    panelTemplate = "SpellEditorInput",
    textTemplate = TEXT,
    placeholderText = "Search spells...",
    defaultText = "",
});
searchInput:SetAnchor(vec.New(0.0, 1.0));
searchInput:SetRelativePoint(vec.New(0.0, 1.0));

local filterMode = "All";
local filterDropdown = nil;
BrowserLayout.ViewportHeight = Layout.WindowHeight - Layout.TitleHeight - 154;
local browserList = nil;

BrowserLayout.FooterHeight = 72;
BrowserLayout.FooterButtonGap = 12;
BrowserLayout.FooterButtonWidth = (Layout.BrowserWidth - Layout.Padding * 2 - BrowserLayout.FooterButtonGap) * 0.5;
BrowserLayout.FooterButtonY = -(BrowserLayout.FooterHeight - 34) * 0.5;
local browserFooter = browserPanel:NewPanel(vec.New(0, -(Layout.WindowHeight - Layout.TitleHeight - BrowserLayout.FooterHeight)), vec.New(Layout.BrowserWidth, BrowserLayout.FooterHeight), 20i, "DebugDarkGrey");
browserFooter:SetAnchor(vec.New(0.0, 1.0));
browserFooter:SetRelativePoint(vec.New(0.0, 1.0));
browserFooter:SetColor(Colors.Panel);
browserFooter:SetAlpha(1.0);
browserFooter:SetBorderColor(Colors.Border);
browserFooter:SetBorderSize(1);
local createButton = Widgets.NewButton(browserFooter, "+  Create Spell", Layout.Padding, BrowserLayout.FooterButtonY, BrowserLayout.FooterButtonWidth, 34, nil, true);
local duplicateButton = Widgets.NewButton(browserFooter, "Duplicate",
    Layout.Padding + BrowserLayout.FooterButtonWidth + BrowserLayout.FooterButtonGap,
    BrowserLayout.FooterButtonY, BrowserLayout.FooterButtonWidth, 34, nil, false);

local bodyScroll = Components.ScrollBox.New(workspacePanel, vec.New(Layout.Padding, -Layout.Padding), vec.New(Layout.BodyWidth - Layout.Padding * 2, Layout.BodyHeight - Layout.Padding * 2), 2i, {
    panelTemplate = "DebugDarkGrey",
    contentPanelTemplate = "SpellEditorFlatPanel",
    contentSizeX = Layout.BodyWidth - Layout.Padding * 2,
    contentSizeY = Layout.BodyHeight - Layout.Padding * 2,
    verticalScrollBar = true,
    scrollBarWidth = 9,
    alpha = 0.0,
});
bodyScroll:SetAnchor(vec.New(0.0, 1.0));
bodyScroll:SetRelativePoint(vec.New(0.0, 1.0));

local snapshot = nil;
local draft = nil;
local catalog = nil;
local diagnostics = {};
local selectedSpellID = nil;
local selectedEffectID = nil;
local selectedConstraintID = nil;
local selectedConstraintReadOnly = false;
local selectedProcDataID = nil;
local selectedProcLinkID = nil;
local effectCatalogOpen = false;
local constraintViewState = {
    search = "",
};
local procDataViewState = {
    search = "",
};
local pendingProcDataChanges = {};
local tabSelection = Components.SelectionGroup.New("Overview");
local tabButtons = {};
local lastStatusKey = "";
local successMessage = "";
local headerContent = nil;
local bodyContent = nil;
local inspectorContent = nil;
local summaryContent = nil;

browserList = Components.VirtualList.New(browserPanel, vec.New(Layout.Padding, -82), vec.New(Layout.BrowserWidth - Layout.Padding * 2, BrowserLayout.ViewportHeight), 2i, {
    rowHeight = Layout.BrowserRowStride,
    scrollBox = {
        panelTemplate = "DebugDarkGrey",
        contentSizeX = Layout.BrowserWidth - Layout.Padding * 2,
        contentSizeY = BrowserLayout.ViewportHeight,
        verticalScrollBar = true,
        scrollBarWidth = 9,
        alpha = 0.0,
    },
    createRow = function(parent)
        local row = Components.Button.New(parent, vec.New(0, 0), vec.New(Layout.BrowserWidth - Layout.Padding * 2 - 12, Layout.BrowserRowHeight), 2i, {
            text = "",
            textTemplate = TEXT,
            panelTemplate = "SpellEditorBrowserRow",
            color = Colors.PanelAlt,
            hoverColor = vec.New(0.16, 0.31, 0.42),
            highlightColor = vec.New(0.16, 0.31, 0.42),
            alpha = 0.75,
            hoverAlpha = 1.0,
            highlightAlpha = 1.0,
        });
        row:SetAnchor(vec.New(0.0, 1.0));
        row:SetRelativePoint(vec.New(0.0, 1.0));
        row.currentSpellID = nil;
        row.icon = row.root:NewPanel(vec.New(3, 0), vec.New(24, 24), 2i, "DebugDarkGrey");
        row.icon:SetAnchor(vec.New(0.0, 0.5));
        row.icon:SetRelativePoint(vec.New(0.0, 0.5));
        row.icon:SetColor(vec.New(1, 1, 1));
        row.nameText = Widgets.NewVerticallyCenteredText(row.root, "", 34, BrowserLayout.PrimaryTextY, "SpellEditorEffectName", Colors.Text);
        row.idText = Widgets.NewVerticallyCenteredText(row.root, "", BrowserLayout.IDX, BrowserLayout.PrimaryTextY, "SpellEditorEffectName", Colors.Muted);
        row.kindText = Widgets.NewVerticallyCenteredText(row.root, "", 34, BrowserLayout.TagY, "SpellEditorMuted", Colors.Muted);
        row:SetOnMouseUp(function()
            if row.currentSpellID ~= nil then
                Actions.OpenSpell(row.currentSpellID);
            end
        end);
        return row;
    end,
    bindRow = function(row, _, spell)
        row.currentSpellID = spell.id;
        row:SetHighlighted(selectedSpellID == spell.id);
        row.nameText:SetText(Components.TextUtil.TruncateToWidth(spell.name, "SpellEditorEffectName", BrowserLayout.NameWidth));
        row.idText:SetText(`#{spell.id}`);
        row.kindText:SetText(spell.hasAura and "Aura" or "Spell");
        row.kindText:SetColor(spell.hasAura and vec.New(0.78, 0.50, 0.95) or Colors.Muted);

        local displayedIconID = draft and draft.spellID == spell.id and draft.iconID or spell.iconID;
        local iconInfo = Spell.GetIconInfo(displayedIconID);
        row.icon:SetBackground(iconInfo and iconInfo.Texture or "texture/ui/icons/50x50/white/question.png");
    end,
});
browserList:SetAnchor(vec.New(0.0, 1.0));
browserList:SetRelativePoint(vec.New(0.0, 1.0));

local SPELL_FILTERS = {
    { id = "All", label = "All", predicate = function() return true end },
    { id = "Spell", label = "Spell", predicate = function(spell) return not spell.hasAura end },
    { id = "Aura", label = "Aura", predicate = function(spell) return spell.hasAura end },
    { id = "Invalid", label = "Invalid", predicate = function(spell)
        return draft ~= nil and spell.id == draft.spellID and #diagnostics > 0;
    end },
};

function DataLookup.GetSpellFilter(filterID)
    for _, filter in SPELL_FILTERS do
        if filter.id == filterID then return filter end;
    end
    return SPELL_FILTERS[1];
end

local spellFilterOptions = { };
for _, filter in SPELL_FILTERS do
    table.insert(spellFilterOptions, { label = filter.label, value = filter.id });
end
filterDropdown = Components.Dropdown.New(browserPanel, canvas, vec.New(Layout.BrowserWidth - 82, -42), vec.New(74, Layout.ControlHeight), 2i, {
    options = spellFilterOptions,
    value = filterMode,
    menuLayer = integer.add(Layout.Layer, 20i),
    menuWidth = 120,
    itemHeight = 28,
    button = {
        textTemplate = TEXT,
        color = Colors.PanelAlt,
        hoverColor = vec.New(0.16, 0.31, 0.42),
        alpha = 0.75,
        hoverAlpha = 1.0,
        borderColor = Colors.Border,
        borderSize = 1,
    },
    onValueChanged = function(value)
        filterMode = value;
        Actions.RefreshBrowser();
    end,
});
function Widgets.DestroyContent(content)
    if content then
        UI.DestroyWidget(content);
    end
end

function DataLookup.FindSpell(spellID)
    if not snapshot then
        return nil;
    end
    for _, spell in snapshot.spells do
        if spell.id == spellID then
            return spell;
        end
    end
    return nil;
end

function DataLookup.FindCatalogEffect(effectType)
    if not catalog then
        return nil;
    end
    for _, effect in catalog.effects do
        if effect.value == effectType then
            return effect;
        end
    end
    return nil;
end

function DataLookup.FormatIdentifier(value)
    local result = tostring(value or "");
    result = string.gsub(result, "([a-z0-9])([A-Z])", "%1 %2");
    result = string.gsub(result, "([A-Z])([A-Z][a-z])", "%1 %2");
    result = string.gsub(result, "Id$", "ID");
    result = string.gsub(result, " Id ", " ID ");
    if #result > 0 then
        result = string.upper(string.sub(result, 1, 1)) .. string.sub(result, 2);
    end
    return result;
end

function DataLookup.ToInteger(value)
    local truncated = if value < 0 then math.ceil(value) else math.floor(value);
    return integer.create(truncated);
end

function DataLookup.BuildSpellOptions(parameter)
    local options = {};
    if integer.le(parameter.minimumValue, -1i) then
        table.insert(options, { name = "Current Spell (-1)", value = -1i });
    end
    if snapshot and snapshot.spells then
        for _, spell in snapshot.spells do
            if integer.gt(spell.id, 0i)
                and integer.ge(spell.id, parameter.minimumValue)
                and integer.le(spell.id, parameter.maximumValue)
            then
                table.insert(options, { name = `{spell.name} (Spell {spell.id})`, value = spell.id });
            end
        end
    end
    return options;
end

function DataLookup.BuildDisplayOptions(options)
    local result = {};
    for _, option in options do
        table.insert(result, {
            name = DataLookup.FormatIdentifier(option.name),
            value = option.value,
        });
    end
    return result;
end

function DataLookup.GetParameterHelp(parameter)
    local unit = parameter.kind == PARAMETER_KIND.DurationMilliseconds and "Duration in milliseconds."
        or parameter.kind == PARAMETER_KIND.IntervalMilliseconds and "Interval in milliseconds."
        or parameter.kind == PARAMETER_KIND.FactionID and "Faction ID."
        or parameter.kind == PARAMETER_KIND.Standing and "Standing value."
        or nil;
    return unit;
end

function DataLookup.GetEffectDiagnostics(effectID)
    local result = { };
    local effectPath = `effects.{effectID}`;
    for _, diagnostic in diagnostics do
        if diagnostic.path == effectPath or string.find(diagnostic.path, effectPath .. ".", 1, true) == 1 then
            table.insert(result, diagnostic);
        end
    end
    return result;
end

function DataLookup.FindOption(options, value)
    for index, option in options do
        if option.value == value then
            return option, index;
        end
    end
    return nil, 1;
end

function DataLookup.FindConstraintGroup(groupID)
    if not snapshot then
        return nil;
    end
    for _, group in snapshot.constraintGroups do
        if group.id == groupID then
            return group;
        end
    end
    return nil;
end

function DataLookup.FindProcData(procDataID)
    if not snapshot then
        return nil;
    end
    for _, procData in snapshot.procDataDefinitions or {} do
        if procData.id == procDataID then
            return procData;
        end
    end
    return nil;
end

function DataLookup.IsProcDataVisible(procData)
    return draft
        and procData.id ~= 0i
        and (procData.ownerSpellID == 0i or procData.ownerSpellID == draft.spellID);
end

function DataLookup.GetProcDataName(procData)
    if procData.ownerSpellID == 0i then
        return procData.name;
    end
    return procData.name ~= "" and procData.name or `Private ProcData {procData.id}`;
end

function DataLookup.GetDefaultMask(options)
    for _, option in options or {} do
        if option.value ~= 0i and option.name ~= "All" then
            return option.value;
        end
    end
    for _, option in options or {} do
        if option.value ~= 0i then
            return option.value;
        end
    end
    return 0i;
end

local confirmModal = Components.Modal.New(window, vec.New(0, 0), vec.New(470, 180), 101i, {
    name = "SpellEditorConfirm",
    title = "Confirm",
    message = "",
    shieldSize = vec.New(Layout.WindowWidth, Layout.WindowHeight),
    shieldColor = vec.New(0.01, 0.015, 0.02),
    shieldAlpha = 0.92,
    dialogColor = Colors.Panel,
    dialogAlpha = 1.0,
    dialogBorderColor = Colors.Border,
    dialogBorderSize = 1.0,
    titleTemplate = "SpellEditorTitle",
    titlePosition = vec.New(18, -18),
    messageTemplate = "SpellEditorText",
    messagePosition = vec.New(18, -60),
    messageWidth = 434,
    buttonSize = vec.New(98, 30),
    confirmButtonOptions = {
        textTemplate = TEXT,
        color = vec.New(0.02, 0.31, 0.68),
        hoverColor = Colors.Accent,
        alpha = 0.85,
        hoverAlpha = 1.0,
        borderColor = Colors.Border,
        borderSize = 1,
    },
    cancelButtonOptions = {
        textTemplate = TEXT,
        color = Colors.PanelAlt,
        hoverColor = vec.New(0.16, 0.31, 0.42),
        alpha = 0.75,
        hoverAlpha = 1.0,
        borderColor = Colors.Border,
        borderSize = 1,
    },
});

function Dialogs.Confirm(message, action)
    confirmModal:Open({
        message = message,
        onConfirm = action,
    });
end

local ICON_PAGE_SIZE = 80;
local ICON_COLUMNS = 10;
local iconCatalogOffset = 0;
local iconCatalogRows = nil;
local iconCatalogTotalText = nil;

local iconCatalogModal = Components.Modal.New(window, vec.New(0, 0), vec.New(720, 520), 100i, {
    name = "SpellEditorIconCatalog",
    showActions = false,
    shieldSize = vec.New(Layout.WindowWidth, Layout.WindowHeight),
    shieldColor = vec.New(0.01, 0.015, 0.02),
    shieldAlpha = 0.92,
    dialogColor = Colors.Panel,
    dialogAlpha = 1.0,
    dialogBorderColor = Colors.Border,
    dialogBorderSize = 1.0,
});
local iconCatalogDialog = iconCatalogModal.dialog;
Widgets.NewText(iconCatalogDialog, "Choose Spell Icon", 16, -14, "SpellEditorTitle", Colors.Text);
local closeIconCatalogButton = Widgets.NewButton(iconCatalogDialog, "Close", 632, -12, 72, 28, nil, false);
local iconCatalogSearch = Components.InputBox.New(iconCatalogDialog, vec.New(16, -52), vec.New(688, Layout.ControlHeight), 2i, {
    panelTemplate = "SpellEditorInput",
    textTemplate = TEXT,
    placeholderText = "Search icon paths...",
    defaultText = "",
});
iconCatalogSearch:SetAnchor(vec.New(0.0, 1.0));
iconCatalogSearch:SetRelativePoint(vec.New(0.0, 1.0));
local iconCatalogScroll = Components.ScrollBox.New(iconCatalogDialog, vec.New(16, -94), vec.New(688, 374), 2i, {
    panelTemplate = "DebugDarkGrey",
    contentSizeX = 688,
    contentSizeY = 374,
    verticalScrollBar = true,
    scrollBarWidth = 9,
    alpha = 0.0,
});
iconCatalogScroll:SetAnchor(vec.New(0.0, 1.0));
iconCatalogScroll:SetRelativePoint(vec.New(0.0, 1.0));
local previousIconPage = Widgets.NewButton(iconCatalogDialog, "Previous", 16, -478, 84, 28, nil, false);
local nextIconPage = Widgets.NewButton(iconCatalogDialog, "Next", 620, -478, 84, 28, nil, false);
iconCatalogTotalText = Widgets.NewText(iconCatalogDialog, "", 112, -486, "SpellEditorMuted", Colors.Muted);

function Dialogs.CloseIconCatalog()
    iconCatalogModal:Close();
end
iconCatalogModal:SetOnCancel(function() iconCatalogSearch:Unfocus(); end);

Dialogs.RefreshIconCatalog = function()
    Widgets.DestroyContent(iconCatalogRows);
    local results = SpellEditor.SearchIcons(
        iconCatalogSearch.text or "",
        integer.create(iconCatalogOffset),
        integer.create(ICON_PAGE_SIZE)
    );
    local icons = results and results.icons or {};
    local rowCount = math.ceil(#icons / ICON_COLUMNS);
    local contentHeight = math.max(374, rowCount * 66 + 8);
    iconCatalogScroll:SetContentHeight(contentHeight);
    iconCatalogScroll:ScrollToStart();
    iconCatalogRows = iconCatalogScroll.content:NewPanel(vec.New(0, 0), vec.New(676, contentHeight), 2i, "DebugDarkGrey");
    iconCatalogRows:SetAnchor(vec.New(0.0, 1.0));
    iconCatalogRows:SetRelativePoint(vec.New(0.0, 1.0));
    iconCatalogRows:SetAlpha(0.0);
    for index, icon in icons do
        local column = (index - 1) % ICON_COLUMNS;
        local row = math.floor((index - 1) / ICON_COLUMNS);
        local iconID = icon.id;
        local iconButton = Components.Button.New(iconCatalogRows, vec.New(4 + column * 66, -(4 + row * 66)), vec.New(58, 58), 2i, {
            text = "",
            panelTemplate = "DebugDarkGrey",
            textTemplate = TEXT,
            alpha = 1.0,
            borderColor = Colors.Border,
            borderSize = 1,
        });
        iconButton:SetAnchor(vec.New(0.0, 1.0));
        iconButton:SetRelativePoint(vec.New(0.0, 1.0));
        iconButton:SetOnMouseUp(function()
            if SpellEditor.SetField("iconID", iconID) then
                Dialogs.CloseIconCatalog();
                Actions.RefreshAll();
            end
        end);
        iconButton:SetOnHoverBegin(function()
            iconButton.root:SetBorderColor(Colors.Accent);
        end);
        iconButton:SetOnHoverEnd(function()
            iconButton.root:SetBorderColor(Colors.Border);
        end);
        local iconInfo = Spell.GetIconInfo(iconID);
        if iconInfo then
            iconButton.root:SetBackground(iconInfo.Texture);
            iconButton.root:SetColor(vec.New(1, 1, 1));
        end
    end
    local total = results and integer.tonumber(results.total) or 0;
    local firstVisible = total == 0 and 0 or iconCatalogOffset + 1;
    local lastVisible = math.min(total, iconCatalogOffset + #icons);
    iconCatalogTotalText:SetText(`Showing {firstVisible}-{lastVisible} of {total}`);
    previousIconPage:SetVisualEnabled(iconCatalogOffset > 0);
    previousIconPage:SetInteractable(iconCatalogOffset > 0);
    nextIconPage:SetVisualEnabled(iconCatalogOffset + ICON_PAGE_SIZE < total);
    nextIconPage:SetInteractable(iconCatalogOffset + ICON_PAGE_SIZE < total);
end

function Dialogs.OpenIconCatalog()
    iconCatalogOffset = 0;
    iconCatalogModal:Open();
    Dialogs.RefreshIconCatalog();
    iconCatalogSearch:Focus();
end

closeIconCatalogButton:SetOnMouseUp(Dialogs.CloseIconCatalog);
iconCatalogSearch:SetOnTextChanged(function()
    iconCatalogOffset = 0;
    Dialogs.RefreshIconCatalog();
end);
previousIconPage:SetOnMouseUp(function()
    iconCatalogOffset = math.max(0, iconCatalogOffset - ICON_PAGE_SIZE);
    Dialogs.RefreshIconCatalog();
end);
nextIconPage:SetOnMouseUp(function()
    iconCatalogOffset += ICON_PAGE_SIZE;
    Dialogs.RefreshIconCatalog();
end);

local spellPickerParameter = nil;
local spellPickerValue = nil;
local spellPickerOnSelect = nil;

local spellPickerModal = Components.Modal.New(window, vec.New(0, 0), vec.New(560, 520), 100i, {
    name = "SpellEditorSpellPicker",
    showActions = false,
    shieldSize = vec.New(Layout.WindowWidth, Layout.WindowHeight),
    shieldColor = vec.New(0.01, 0.015, 0.02),
    shieldAlpha = 0.92,
    dialogColor = Colors.Panel,
    dialogAlpha = 1.0,
    dialogBorderColor = Colors.Border,
    dialogBorderSize = 1.0,
});
local spellPickerDialog = spellPickerModal.dialog;
Widgets.NewText(spellPickerDialog, "Choose Referenced Spell", 16, -14, "SpellEditorTitle", Colors.Text);
local closeSpellPickerButton = Widgets.NewButton(spellPickerDialog, "Close", 472, -12, 72, 28, nil, false);
Widgets.NewText(spellPickerDialog, "Search", 16, -50, "SpellEditorMuted", Colors.Text);
local spellPickerSearch = Components.InputBox.New(spellPickerDialog, vec.New(16, -70), vec.New(528, Layout.ControlHeight), 2i, {
    panelTemplate = "SpellEditorInput",
    textTemplate = TEXT,
    placeholderText = "Search spells by name or ID...",
    defaultText = "",
});
spellPickerSearch:SetAnchor(vec.New(0.0, 1.0));
spellPickerSearch:SetRelativePoint(vec.New(0.0, 1.0));
local spellPickerList = Components.VirtualList.New(spellPickerDialog, vec.New(16, -112), vec.New(528, 396), 2i, {
    rowHeight = 44,
    scrollBox = {
        panelTemplate = "DebugDarkGrey",
        contentSizeX = 528,
        contentSizeY = 396,
        verticalScrollBar = true,
        scrollBarWidth = 9,
        alpha = 0.0,
    },
    createRow = function(parent)
        local row = Widgets.NewButton(parent, "", 0, 0, 512, 42, nil, false);
        row.currentValue = nil;
        row.nameText = Widgets.NewText(row.root, "", 12, -6, "SpellEditorText", Colors.Text);
        row.idText = Widgets.NewText(row.root, "", 12, -24, "SpellEditorMuted", Colors.Muted);
        row:SetOnMouseUp(function()
            if row.currentValue ~= nil and spellPickerOnSelect then
                local callback = spellPickerOnSelect;
                spellPickerModal:Close();
                callback(row.currentValue);
                Actions.RefreshAll();
            end
        end);
        return row;
    end,
    bindRow = function(row, _, option)
        row.currentValue = option.value;
        row:SetHighlighted(option.value == spellPickerValue);
        if option.value == -1i then
            row.nameText:SetText("Current Spell");
            row.idText:SetText("Uses the spell that owns this effect");
        else
            local spell = DataLookup.FindSpell(option.value);
            row.nameText:SetText(Components.TextUtil.TruncateToWidth(spell and spell.name or "Unknown Spell", "SpellEditorText", 476));
            row.idText:SetText(`Spell {option.value}`);
        end
    end,
});
spellPickerList:SetAnchor(vec.New(0.0, 1.0));
spellPickerList:SetRelativePoint(vec.New(0.0, 1.0));

Dialogs.RefreshSpellPicker = function()
    local visibleOptions = {};
    if spellPickerParameter then
        local search = spellPickerSearch.text or "";
        for _, option in DataLookup.BuildSpellOptions(spellPickerParameter) do
            if search == "" or Components.TextUtil.ContainsIgnoreCase(option.name, search)
                or string.find(tostring(option.value), search, 1, true) ~= nil then
                table.insert(visibleOptions, option);
            end
        end
    end
    spellPickerList:SetItems(visibleOptions);
end

function Dialogs.OpenSpellPicker(parameter, value, onSelect)
    spellPickerParameter = parameter;
    spellPickerValue = value;
    spellPickerOnSelect = onSelect;
    spellPickerSearch:SetText("");
    spellPickerModal:Open();
    Dialogs.RefreshSpellPicker();
    spellPickerSearch:Focus();
end

closeSpellPickerButton:SetOnMouseUp(function()
    spellPickerModal:Close();
end);
spellPickerSearch:SetOnTextChanged(Dialogs.RefreshSpellPicker);
spellPickerModal:SetOnCancel(function()
    spellPickerSearch:Unfocus();
    spellPickerOnSelect = nil;
end);

function Actions.IsDirty()
    return draft and (draft.state == "Dirty" or draft.state == "MutationFailed");
end

Actions.OpenSpell = function(spellID)
    local function Open()
        if SpellEditor.OpenDraft(spellID) then
            selectedSpellID = spellID;
            selectedEffectID = nil;
            selectedConstraintID = nil;
            selectedConstraintReadOnly = false;
            selectedProcDataID = nil;
            selectedProcLinkID = nil;
            effectCatalogOpen = false;
            Actions.RefreshAll();
        end
    end
    if Actions.IsDirty() and selectedSpellID ~= spellID then
        Dialogs.Confirm("Discard the current unsynchronized draft?", Open);
    else
        Open();
    end
end

function Actions.NavigateToPath(path)
    local tab = path:match("^([^.]+)") or "overview";
    local tabNames = {
        overview = "Overview",
        targeting = "Targeting",
        effects = "Effects",
        aura = "Aura",
        constraints = "Constraints",
        procs = "Procs",
    };
    local effectID = tonumber(path:match("^effects%.(%d+)"));
    local constraintID = tonumber(path:match("^constraints%.(%d+)"));
    selectedEffectID = effectID and integer.create(effectID) or selectedEffectID;
    selectedConstraintID = constraintID and integer.create(constraintID) or selectedConstraintID;
    if path:match("^constraints%.") then selectedConstraintReadOnly = false end;
    local procDataID = tonumber(path:match("^procs%.data%.(%d+)"));
    local procLinkID = tonumber(path:match("^procs%.links%.(%d+)"));
    if procDataID then
        selectedProcDataID = integer.create(procDataID);
        selectedProcLinkID = nil;
    elseif procLinkID then
        selectedProcDataID = nil;
        selectedProcLinkID = integer.create(procLinkID);
    end
    Actions.SetActiveTab(tabNames[tab] or "Overview");
end

function Actions.UpdateProcDataFields(procData, changes)
    procData = Actions.GetEffectiveProcData(procData);
    local updated = {
        ownerSpellID = procData.ownerSpellID,
        name = procData.name,
        phaseMask = procData.phaseMask,
        typeMask = procData.typeMask,
        hitMask = procData.hitMask,
        flags = procData.flags,
        procsPerMinute = procData.procsPerMinute,
        chanceToProc = procData.chanceToProc,
        internalCooldownMS = procData.internalCooldownMS,
        charges = procData.charges,
    };
    for field, value in changes do
        updated[field] = value;
    end
    local accepted = SpellEditor.UpdateProcData(
        procData.id,
        updated.ownerSpellID,
        updated.name,
        updated.phaseMask,
        updated.typeMask,
        updated.hitMask,
        updated.flags,
        updated.procsPerMinute,
        updated.chanceToProc,
        updated.internalCooldownMS,
        updated.charges);
    if accepted then
        local pending = pendingProcDataChanges[procData.id] or {};
        for field, value in changes do
            pending[field] = value;
        end
        pendingProcDataChanges[procData.id] = pending;
    end
    return accepted;
end

function Actions.UpdateProcData(procData, field, value)
    return Actions.UpdateProcDataFields(procData, { [field] = value });
end

function Actions.GetEffectiveProcData(procData)
    local pending = pendingProcDataChanges[procData.id];
    if not pending then
        return procData;
    end

    local authoritativeMatches = true;
    for field, value in pending do
        if procData[field] ~= value then
            authoritativeMatches = false;
            break;
        end
    end
    if authoritativeMatches then
        pendingProcDataChanges[procData.id] = nil;
        return procData;
    end

    local mutationInProgress = draft and
        (draft.state == "Synchronizing" or draft.state == "Refreshing");
    if not mutationInProgress and draft and draft.serverDiagnostic ~= "" then
        pendingProcDataChanges[procData.id] = nil;
        return procData;
    end

    local effective = {};
    for field, value in procData do
        effective[field] = value;
    end
    for field, value in pending do
        effective[field] = value;
    end
    return effective;
end

function Actions.CreatePrivateProcData()
    if not draft or draft.isCreate or
        (draft.state ~= "Clean" and draft.state ~= "Dirty")
    then
        return 0i;
    end
    return SpellEditor.CreateProcData(
        draft.spellID,
        "",
        DataLookup.GetDefaultMask(catalog.procPhaseMask),
        DataLookup.GetDefaultMask(catalog.procTypeMask),
        DataLookup.GetDefaultMask(catalog.procHitMask),
        0i,
        0,
        1,
        0i,
        -1i);
end

local diagnosticsModal = Components.Modal.New(window, vec.New(0, 0), vec.New(680, 520), 100i, {
    name = "SpellEditorDiagnostics",
    showActions = false,
    shieldSize = vec.New(Layout.WindowWidth, Layout.WindowHeight),
    shieldColor = vec.New(0.01, 0.015, 0.02),
    shieldAlpha = 0.92,
    dialogColor = Colors.Panel,
    dialogAlpha = 1.0,
    dialogBorderColor = Colors.Border,
    dialogBorderSize = 1.0,
});
local diagnosticsDialog = diagnosticsModal.dialog;
Widgets.NewText(diagnosticsDialog, "Validation Errors", 16, -14, "SpellEditorTitle", Colors.Text);
Widgets.NewText(diagnosticsDialog, "Select an error to navigate to its authored field or definition.", 16, -44, "SpellEditorMuted", Colors.Muted);
local closeDiagnosticsButton = Widgets.NewButton(diagnosticsDialog, "Close", 592, -12, 72, 28, nil, false);
local diagnosticsList = Components.VirtualList.New(diagnosticsDialog, vec.New(16, -76), vec.New(648, 428), 2i, {
    rowHeight = 74,
    scrollBox = {
        panelTemplate = "DebugDarkGrey",
        contentSizeX = 648,
        contentSizeY = 428,
        verticalScrollBar = true,
        scrollBarWidth = 9,
        alpha = 0.0,
    },
    createRow = function(parent)
        local row = Widgets.NewButton(parent, "", 0, 0, 632, 72, nil, false);
        row.currentPath = nil;
        row.messageText = Widgets.NewWrappedText(row.root, "", 12, -8, 608, "SpellEditorText", Colors.Error);
        row.pathText = Widgets.NewText(row.root, "", 12, -50, "SpellEditorMuted", Colors.Muted);
        row:SetOnMouseUp(function()
            if row.currentPath ~= nil then
                diagnosticsModal:Close();
                Actions.NavigateToPath(row.currentPath);
            end
        end);
        return row;
    end,
    bindRow = function(row, _, diagnostic)
        row.currentPath = diagnostic.path;
        row.messageText:SetText(diagnostic.message);
        row.pathText:SetText(Components.TextUtil.TruncateToWidth(diagnostic.path, "SpellEditorMuted", 608));
    end,
});
diagnosticsList:SetAnchor(vec.New(0.0, 1.0));
diagnosticsList:SetRelativePoint(vec.New(0.0, 1.0));
closeDiagnosticsButton:SetOnMouseUp(function()
    diagnosticsModal:Close();
end);

Dialogs.OpenDiagnostics = function()
    if #diagnostics == 0 then
        return;
    end
    diagnosticsList:SetItems(diagnostics);
    diagnosticsModal:Open();
end

function Widgets.CommitNumber(value, callback)
    local numberValue = tonumber(value);
    if numberValue ~= nil then
        callback(numberValue);
        Actions.RefreshAll();
    end
end

function Widgets.NewControlLabel(parent, label, x, y, color)
    local labelPanel = Widgets.NewPanel(parent, x, y, Layout.FieldLabelWidth, Layout.ControlHeight, Colors.Panel, 0.0);
    labelPanel:SetBorderSize(0);
    labelPanel:SetInteractable(false);
    return Widgets.NewVerticallyCenteredText(labelPanel, label, 0, 0, "SpellEditorText", color or Colors.Text);
end

function Widgets.NewFieldInput(parent, label, value, x, y, width, callback, enabled)
    local isEnabled = enabled ~= false;
    Widgets.NewControlLabel(parent, label, x, y, isEnabled and Colors.Text or Colors.Muted);
    local input = Components.InputBox.New(parent, vec.New(x + Layout.FieldLabelWidth, y), vec.New(width - Layout.FieldLabelWidth, Layout.ControlHeight), 2i, {
        panelTemplate = "SpellEditorInput",
        textTemplate = TEXT,
        defaultText = tostring(value),
    });
    input:SetAnchor(vec.New(0.0, 1.0));
    input:SetRelativePoint(vec.New(0.0, 1.0));
    input.button:SetInteractable(isEnabled);
    input.button.root:SetAlpha(isEnabled and 1.0 or 0.45);
    input.button.text:SetAlpha(isEnabled and 1.0 or 0.45);
    input:SetOnSubmit(function(box)
        if isEnabled then callback(box.text) end;
        return true;
    end);
    return input;
end

function Widgets.NewReadOnlyField(parent, label, value, x, y, width)
    Widgets.NewControlLabel(parent, label, x, y, Colors.Text);
    local valuePanel = Widgets.NewPanel(parent, x + Layout.FieldLabelWidth, y, width - Layout.FieldLabelWidth, Layout.ControlHeight, Colors.Title, 0.72);
    Widgets.NewVerticallyCenteredSingleLineText(valuePanel, value, 10, 0,
        width - Layout.FieldLabelWidth - 20, "SpellEditorMuted", Colors.Muted);
    return valuePanel;
end

function Widgets.NewTextAreaField(parent, label, value, x, y, width, height, callback)
    Widgets.NewText(parent, label, x, y, "SpellEditorText", Colors.Text);
    local input = Components.InputBox.New(parent, vec.New(x, y - 22), vec.New(width, height), 2i, {
        panelTemplate = "SpellEditorInput",
        textTemplate = TEXT,
        defaultText = value or "",
        placeholderText = `Enter {string.lower(label)}...`,
        multiline = true,
    });
    input:SetAnchor(vec.New(0.0, 1.0));
    input:SetRelativePoint(vec.New(0.0, 1.0));
    local committedValue = value;
    local function Commit(box)
        if box.text ~= committedValue then
            committedValue = box.text;
            callback(box.text);
            Actions.RefreshAll();
        end
    end
    input:SetOnFocusEnd(Commit);
    input:SetOnSubmit(function(box)
        Commit(box);
        return true;
    end);
    return input;
end

function Widgets.NewDropdownField(parent, label, value, options, x, y, width, callback, previewMaxCharacters)
    Widgets.NewControlLabel(parent, label, x, y, #options > 0 and Colors.Text or Colors.Muted);
    local selected = DataLookup.FindOption(options, value);
    local buttonWidth = width - Layout.FieldLabelWidth;
    local dropdown = Components.Dropdown.New(parent, canvas, vec.New(x + Layout.FieldLabelWidth, y), vec.New(buttonWidth, Layout.ControlHeight), 2i, {
        options = options,
        value = value,
        placeholder = selected and selected.name or `Unknown ({value})`,
        menuLayer = integer.add(Layout.Layer, 20i),
        menuWidth = buttonWidth,
        itemHeight = 28,
        previewMaxCharacters = previewMaxCharacters,
        button = {
            textTemplate = TEXT,
            color = Colors.PanelAlt,
            hoverColor = vec.New(0.16, 0.31, 0.42),
            alpha = 0.75,
            hoverAlpha = 1.0,
            borderColor = Colors.Border,
            borderSize = 1,
        },
        onValueChanged = function(newValue)
            if callback(newValue) == false then
                return false;
            end
            Actions.RefreshAll();
            return true;
        end,
    });
    dropdown:SetInteractable(#options > 0);
    return dropdown;
end

function Widgets.NewStackedDropdownField(parent, label, value, options, x, y, width, callback, enabled, refreshAfterChange)
    local isEnabled = enabled ~= false and #options > 0;
    Widgets.NewText(parent, label, x, y, "SpellEditorMuted", isEnabled and Colors.Text or Colors.Muted);
    local selected = DataLookup.FindOption(options, value);
    local dropdown = Components.Dropdown.New(parent, canvas, vec.New(x, y - 20), vec.New(width, Layout.ControlHeight), 2i, {
        options = options,
        value = value,
        placeholder = selected and selected.name or `Unknown ({value})`,
        menuLayer = integer.add(Layout.Layer, 20i),
        menuWidth = width,
        itemHeight = 28,
        button = {
            textTemplate = TEXT,
            color = Colors.PanelAlt,
            hoverColor = vec.New(0.16, 0.31, 0.42),
            alpha = 0.75,
            hoverAlpha = 1.0,
            borderColor = Colors.Border,
            borderSize = 1,
        },
        onValueChanged = function(newValue)
            if callback(newValue) == false then
                return false;
            end
            if refreshAfterChange ~= false then
                Actions.RefreshAll();
            end
            return true;
        end,
    });
    dropdown:SetInteractable(isEnabled);
    return dropdown;
end

function Widgets.NewStackedFieldInput(parent, label, value, x, y, width, callback, enabled)
    local isEnabled = enabled ~= false;
    Widgets.NewText(parent, label, x, y, "SpellEditorText", isEnabled and Colors.Text or Colors.Muted);
    local input = Components.InputBox.New(parent, vec.New(x, y - 20), vec.New(width, Layout.ControlHeight), 2i, {
        panelTemplate = "SpellEditorInput",
        textTemplate = TEXT,
        defaultText = tostring(value),
    });
    input:SetAnchor(vec.New(0.0, 1.0));
    input:SetRelativePoint(vec.New(0.0, 1.0));
    input.button:SetInteractable(isEnabled);
    input.button.root:SetAlpha(isEnabled and 1.0 or 0.45);
    input.button.text:SetAlpha(isEnabled and 1.0 or 0.45);
    input:SetOnSubmit(function(box)
        if isEnabled then callback(box.text) end;
        return true;
    end);
    return input;
end

function Widgets.NewStackedBooleanField(parent, label, value, x, y, callback)
    Widgets.NewText(parent, label, x + 28, y - 1, "SpellEditorText", Colors.Text);
    local checkbox = Components.Checkbox.New(parent, vec.New(x, y), vec.New(20, 20), 2i, {
        backgroundTemplate = "SpellEditorCheckbox",
        fillTemplate = "SpellEditorCheckboxFill",
        fillInset = 4,
        isChecked = value ~= 0i,
    });
    checkbox:SetAnchor(vec.New(0.0, 1.0));
    checkbox:SetRelativePoint(vec.New(0.0, 1.0));
    checkbox.root:SetAnchor(vec.New(0.0, 1.0));
    checkbox.root:SetRelativePoint(vec.New(0.0, 1.0));
    checkbox.root:SetBorderColor(Colors.Border);
    checkbox.root:SetBorderSize(1);
    checkbox:SetOnValueChanged(function(_, checked)
        callback(checked and 1i or 0i);
        Actions.RefreshAll();
    end);
    return checkbox;
end

function Widgets.NewStackedFlagsField(parent, label, value, options, x, y, width, callback, enabled, refreshAfterChange)
    local isEnabled = enabled ~= false;
    Widgets.NewText(parent, label, x, y, "SpellEditorMuted", isEnabled and Colors.Text or Colors.Muted);
    local optionY = y - 24;
    for _, option in options do
        local checked = option.value == 0i
            and value == 0i
            or option.value ~= 0i and integer.band(value, option.value) == option.value;
        local checkbox = Components.Checkbox.New(parent, vec.New(x, optionY), vec.New(18, 18), 2i, {
            backgroundTemplate = "SpellEditorCheckbox",
            fillTemplate = "SpellEditorCheckboxFill",
            fillInset = 4,
            isChecked = checked,
        });
        checkbox:SetAnchor(vec.New(0.0, 1.0));
        checkbox:SetRelativePoint(vec.New(0.0, 1.0));
        checkbox.root:SetAnchor(vec.New(0.0, 1.0));
        checkbox.root:SetRelativePoint(vec.New(0.0, 1.0));
        checkbox.root:SetBorderColor(Colors.Border);
        checkbox.root:SetBorderSize(1);
        checkbox.root:SetInteractable(isEnabled);
        checkbox.root:SetAlpha(isEnabled and 1.0 or 0.45);
        checkbox.fill:SetAlpha(isEnabled and 1.0 or 0.45);
        Widgets.NewSingleLineText(parent, DataLookup.FormatIdentifier(option.name), x + 26, optionY,
            width - 26, "SpellEditorMuted", isEnabled and Colors.Text or Colors.Muted);
        checkbox:SetOnValueChanged(function(_, enabled)
            if not isEnabled then
                return;
            end
            local newValue = if option.value == 0i
                then (enabled and 0i or value)
                else (enabled
                    and integer.bor(value, option.value)
                    or integer.band(value, integer.bnot(option.value)));
            if callback(newValue) == false then
                return false;
            end
            if refreshAfterChange ~= false then
                Actions.RefreshAll();
            end
            return true;
        end);
        optionY -= 24;
    end
    return 28 + #options * 24;
end

function Widgets.NewStackedSpellField(parent, label, parameter, value, x, y, width, callback)
    Widgets.NewText(parent, label, x, y, "SpellEditorMuted", Colors.Text);
    local referencedSpell = integer.gt(value, 0i) and DataLookup.FindSpell(value) or nil;
    local preview = value == -1i and "Current Spell"
        or referencedSpell and `{referencedSpell.name} (Spell {value})`
        or `Unknown Spell ({value})`;
    local button = Widgets.NewButton(parent, preview, x, y - 20, width, Layout.ControlHeight, function()
        Dialogs.OpenSpellPicker(parameter, value, callback);
    end, false);
    button.text:SetText(Components.TextUtil.TruncateToWidth(preview, TEXT, width - 34) .. "  ...");
    return button;
end

function Widgets.NewSection(parent, title, y, height, width)
    local panel = parent:NewPanel(vec.New(0, y), vec.New(width or Layout.BodyWidth - Layout.Padding * 2, height), 1i, "SpellEditorFlatPanel");
    panel:SetAnchor(vec.New(0.0, 1.0));
    panel:SetRelativePoint(vec.New(0.0, 1.0));
    Widgets.NewText(panel, title, 14, Layout.SectionTitleY, Layout.SectionTitleTemplate, Colors.Text);
    return panel;
end

function Widgets.GetSectionDescriptionY()
    local titleHeight = math.ceil(UI.CalculateTextSize("Ag", Layout.SectionTitleTemplate).y);
    return Layout.SectionTitleY - titleHeight - 1;
end

function Widgets.NewStackedReadOnlyField(parent, label, value, x, y, width)
    Widgets.NewText(parent, label, x, y, "SpellEditorMuted", Colors.Text);
    local valuePanel = Widgets.NewPanel(parent, x, y - 20, width, Layout.ControlHeight, Colors.Title, 0.72);
    Widgets.NewVerticallyCenteredSingleLineText(valuePanel, value, 10, 0, width - 20, "SpellEditorMuted", Colors.Muted);
    return valuePanel;
end

local constraintGroupModal = Components.Modal.New(window, vec.New(0, 0), vec.New(420, 360), 101i, {
    name = "SpellEditorConstraintGroup",
    showActions = false,
    shieldSize = vec.New(Layout.WindowWidth, Layout.WindowHeight),
    shieldColor = vec.New(0.01, 0.015, 0.02),
    shieldAlpha = 0.92,
    dialogColor = Colors.Panel,
    dialogAlpha = 1.0,
    dialogBorderColor = Colors.Border,
    dialogBorderSize = 1.0,
});
local constraintGroupDialog = constraintGroupModal.dialog;
local constraintGroupModalTitle = Widgets.NewText(constraintGroupDialog, "Create Constraint Group", 16, -14, "SpellEditorTitle", Colors.Text);
Widgets.NewText(constraintGroupDialog, "Name", 16, -48, "SpellEditorMuted", Colors.Text);
local constraintGroupNameInput = Components.InputBox.New(constraintGroupDialog, vec.New(16, -68), vec.New(388, Layout.ControlHeight), 2i, {
    panelTemplate = "SpellEditorInput",
    textTemplate = TEXT,
    placeholderText = "Group name...",
    defaultText = "",
});
constraintGroupNameInput:SetAnchor(vec.New(0.0, 1.0));
constraintGroupNameInput:SetRelativePoint(vec.New(0.0, 1.0));
local constraintGroupModalState = {
    defaultScope = 0i,
    defaultMaximumApplications = 1i,
    defaultOverflowBehavior = 0i,
};
local constraintGroupScopeDropdown = Widgets.NewStackedDropdownField(
    constraintGroupDialog, "Default Scope", 0i, {}, 16, -114, 388, function(value)
        constraintGroupModalState.defaultScope = value;
    end);
local constraintGroupMaximumInput = Widgets.NewStackedFieldInput(
    constraintGroupDialog, "Default Maximum Applications", 1, 16, -180, 388, function(value)
        local parsed = tonumber(value);
        if not parsed or parsed < 1 or parsed > 65535 then
            return false;
        end
        constraintGroupModalState.defaultMaximumApplications = DataLookup.ToInteger(parsed);
    end);
local constraintGroupOverflowDropdown = Widgets.NewStackedDropdownField(
    constraintGroupDialog, "Default Overflow Behavior", 0i, {}, 16, -246, 388, function(value)
        constraintGroupModalState.defaultOverflowBehavior = value;
    end);
local cancelConstraintGroupButton = Widgets.NewButton(constraintGroupDialog, "Cancel", 204, -310, 94, 30, nil, false);
local saveConstraintGroupButton = Widgets.NewButton(constraintGroupDialog, "Create", 310, -310, 94, 30, nil, true);
local constraintGroupEditingID = nil;

function Dialogs.CloseConstraintGroupModal()
    constraintGroupNameInput:Unfocus();
    constraintGroupModal:Close();
end

function Dialogs.OpenConstraintGroupModal(group)
    constraintGroupEditingID = group and group.id or nil;
    constraintGroupModalTitle:SetText(group and "Edit Constraint Group" or "Create Constraint Group");
    saveConstraintGroupButton.text:SetText(group and "Save" or "Create");
    constraintGroupNameInput:SetText(group and group.name or "");
    constraintGroupModalState.defaultScope = group and group.defaultScope or 0i;
    constraintGroupModalState.defaultMaximumApplications = group and group.defaultMaximumApplications or 1i;
    constraintGroupModalState.defaultOverflowBehavior = group and group.defaultOverflowBehavior or 0i;
    constraintGroupScopeDropdown:SetOptions(catalog.constraintScopes);
    constraintGroupScopeDropdown:SetInteractable(#catalog.constraintScopes > 0);
    constraintGroupScopeDropdown:SetValue(constraintGroupModalState.defaultScope, false);
    constraintGroupMaximumInput:SetText(tostring(constraintGroupModalState.defaultMaximumApplications));
    constraintGroupOverflowDropdown:SetOptions(catalog.constraintOverflowBehaviors);
    constraintGroupOverflowDropdown:SetInteractable(#catalog.constraintOverflowBehaviors > 0);
    constraintGroupOverflowDropdown:SetValue(constraintGroupModalState.defaultOverflowBehavior, false);
    constraintGroupModal:Open();
    constraintGroupNameInput:Focus();
end

cancelConstraintGroupButton:SetOnMouseUp(Dialogs.CloseConstraintGroupModal);
saveConstraintGroupButton:SetOnMouseUp(function()
    local name = constraintGroupNameInput.text or "";
    local accepted = false;
    if constraintGroupEditingID then
        accepted = SpellEditor.UpdateConstraintGroup(
            constraintGroupEditingID,
            name,
            constraintGroupModalState.defaultScope,
            constraintGroupModalState.defaultMaximumApplications,
            constraintGroupModalState.defaultOverflowBehavior);
    else
        local groupID = SpellEditor.CreateConstraintGroup(
            name,
            constraintGroupModalState.defaultScope,
            constraintGroupModalState.defaultMaximumApplications,
            constraintGroupModalState.defaultOverflowBehavior);
        accepted = groupID ~= 0i;
    end
    if accepted then
        Dialogs.CloseConstraintGroupModal();
        Actions.RefreshAll();
    end
end);
constraintGroupModal:SetOnCancel(function()
    constraintGroupEditingID = nil;
    constraintGroupNameInput:Unfocus();
end);

function Actions.RefreshHeader()
    Widgets.DestroyContent(headerContent);
    headerContent = headerPanel:NewPanel(vec.New(0, 0), vec.New(Layout.BodyWidth + Layout.InspectorWidth, Layout.HeaderHeight), 2i, "DebugDarkGrey");
    headerContent:SetAlpha(0.0);
    if not draft then
        Widgets.NewText(headerContent, "No spell selected", 16, -18, "SpellEditorTitle", Colors.Text);
        Widgets.NewText(headerContent, "Choose a spell from the browser or create a new draft.", 16, -38, "SpellEditorMuted", Colors.Muted);
        return;
    end

    local icon = headerContent:NewPanel(vec.New(12, -8), vec.New(40, 40), 2i, "DebugDarkGrey");
    icon:SetAnchor(vec.New(0.0, 1.0));
    icon:SetRelativePoint(vec.New(0.0, 1.0));
    icon:SetColor(vec.New(1, 1, 1));
    local iconInfo = Spell.GetIconInfo(draft.iconID);
    if iconInfo then
        icon:SetBackground(iconInfo.Texture);
    end

    local displayedName = Components.TextUtil.TruncateToWidth(draft.name, "SpellEditorHeaderTitle", 210);
    local titleSize = UI.CalculateTextSize(displayedName, "SpellEditorHeaderTitle");
    local headerTagsX = math.clamp(64 + math.ceil(titleSize.x) + 16, 260, 320);
    local headerActionsX = Layout.BodyWidth + Layout.InspectorWidth - 296;
    local headerControlHeight = 32;
    local headerControlY = -((Layout.HeaderHeight - headerControlHeight) / 2);
    local headerStateY = -((Layout.HeaderHeight - 14) / 2) + 2;
    Widgets.NewText(headerContent, displayedName, 64, -8, "SpellEditorHeaderTitle", Colors.Text);
    Widgets.NewText(headerContent, `ID #{draft.spellID}`, 64, -32, "SpellEditorMuted", Colors.Muted);

    local kind = draft.aura and "Aura" or "Spell";
    Widgets.NewButton(headerContent, kind, headerTagsX, headerControlY, 88, headerControlHeight, nil, false):SetInteractable(false);
    local effectCountLabel = #draft.effects == 1 and "1 Effect" or `{#draft.effects} Effects`;
    Widgets.NewButton(headerContent, effectCountLabel, headerTagsX + 98, headerControlY, 106, headerControlHeight, nil, false):SetInteractable(false);

    local valid = #diagnostics == 0;
    local validationButton = Widgets.NewButton(headerContent, valid and "Valid" or `Invalid ({#diagnostics})`, headerTagsX + 214, headerControlY, 116, headerControlHeight, nil, false);
    validationButton.text:SetColor(valid and Colors.Success or Colors.Error);
    validationButton:SetInteractable(not valid);
    if not valid then
        validationButton:SetOnMouseUp(Dialogs.OpenDiagnostics);
    end
    local stateName = draft.state == "MutationFailed" and "Mutation failed"
        or draft.state == "Synchronizing" and "Synchronizing"
        or draft.state == "Refreshing" and "Refreshing"
        or Actions.IsDirty() and "Dirty"
        or "Clean";
    local stateColor = draft.state == "MutationFailed" and Colors.Error
        or (draft.state == "Synchronizing" or draft.state == "Refreshing") and Colors.Accent
        or Actions.IsDirty() and Colors.Warning
        or Colors.Success;
    local validationEndX = headerTagsX + 330;
    local headerStateWidth = math.ceil(UI.CalculateTextSize(stateName, "SpellEditorText").x);
    local headerStateX = validationEndX + math.max(0, (headerActionsX - validationEndX - headerStateWidth) / 2);
    Widgets.NewSingleLineText(headerContent, stateName, headerStateX, headerStateY, headerStateWidth, "SpellEditorText", stateColor);

    local revertButton = Widgets.NewButton(headerContent, "Revert", headerActionsX, headerControlY, 106, headerControlHeight, nil, false);
    revertButton:SetVisualEnabled(Actions.IsDirty());
    revertButton:SetInteractable(Actions.IsDirty());
    revertButton:SetOnMouseUp(function()
        if not Actions.IsDirty() then
            return;
        end
        Dialogs.Confirm("Discard all local changes to this spell?", function()
            if draft.isCreate then
                SpellEditor.DiscardDraft();
                selectedSpellID = nil;
            else
                SpellEditor.OpenDraft(draft.spellID);
            end
            Actions.RefreshAll();
        end);
    end);

    local canSync = valid and Actions.IsDirty() and draft.state ~= "Synchronizing" and draft.state ~= "Refreshing";
    local syncLabel = draft.state == "Synchronizing" and "Synchronizing..."
        or draft.state == "Refreshing" and "Refreshing..."
        or draft.isCreate and "Create on Server"
        or "Sync to Server";
    local syncButton = Widgets.NewButton(headerContent, syncLabel, Layout.BodyWidth + Layout.InspectorWidth - 180, headerControlY, 164, headerControlHeight, nil, true);
    syncButton:SetVisualEnabled(canSync);
    syncButton:SetInteractable(canSync);
    syncButton:SetOnMouseUp(function()
        if canSync and SpellEditor.Submit() then
            Actions.RefreshAll();
        end
    end);

end

Actions.RefreshBrowser = function(preserveScroll)
    local visibleSpells = {};
    if snapshot and snapshot.spells then
        local search = searchInput.text or "";
        for _, spell in snapshot.spells do
            local matchesSearch = search == "" or Components.TextUtil.ContainsIgnoreCase(spell.name, search)
                or string.find(tostring(spell.id), search, 1, true) ~= nil;
            local matchesFilter = DataLookup.GetSpellFilter(filterMode).predicate(spell);
            if matchesSearch and matchesFilter then
                table.insert(visibleSpells, spell);
            end
        end
    end
    browserList:SetItems(visibleSpells, preserveScroll);
    local totalSpells = snapshot and snapshot.spells and #snapshot.spells or 0;
    browserTitleText:SetText(`Spell Browser ({totalSpells} Total)`);
end

function ViewBuilders.Overview(parent)
    local section = Widgets.NewSection(parent, "Spell Definition", 0, 540);
    local contentWidth = Layout.BodyWidth - 64;
    local columnGap = 16;
    local columnWidth = (contentWidth - columnGap) * 0.5;
    local leftX = 16;
    local rightX = leftX + columnWidth + columnGap;
    local firstRowY = -46;
    local firstRowControlY = firstRowY - 20;
    local secondRowY = -116;

    Widgets.NewStackedFieldInput(section, "Name", draft.name, leftX, firstRowY, columnWidth, function(value)
        SpellEditor.SetField("name", value);
        Actions.RefreshAll();
    end);

    Widgets.NewText(section, "Icon", rightX, firstRowY, "SpellEditorText", Colors.Text);
    local chooseIconButton = Widgets.NewButton(section, "Choose Icon...", rightX, firstRowControlY,
        columnWidth, Layout.ControlHeight, Dialogs.OpenIconCatalog, false, "left", 40);
    local iconPreview = chooseIconButton:NewPanel(vec.New(3, 0), vec.New(Layout.ControlHeight - 6, Layout.ControlHeight - 6), 1i, "DebugDarkGrey");
    iconPreview:SetAnchor(vec.New(0.0, 0.5));
    iconPreview:SetRelativePoint(vec.New(0.0, 0.5));
    iconPreview:SetColor(vec.New(1, 1, 1));
    iconPreview:SetBorderColor(Colors.Border);
    iconPreview:SetBorderSize(1);
    local iconInfo = Spell.GetIconInfo(draft.iconID);
    if iconInfo then
        iconPreview:SetBackground(iconInfo.Texture);
    end

    Widgets.NewStackedFieldInput(section, "Cast Time (seconds)", draft.castTime, leftX, secondRowY, columnWidth, function(value)
        Widgets.CommitNumber(value, function(number) SpellEditor.SetField("castTime", number) end);
    end);
    Widgets.NewStackedFieldInput(section, "Cooldown (seconds)", draft.cooldown, rightX, secondRowY, columnWidth, function(value)
        Widgets.CommitNumber(value, function(number) SpellEditor.SetField("cooldown", number) end);
    end);
    Widgets.NewTextAreaField(section, "Description", draft.description, leftX, -186, contentWidth, 88, function(value)
        SpellEditor.SetField("description", value);
    end);
    Widgets.NewTextAreaField(section, "Aura Description", draft.auraDescription, leftX, -320, contentWidth, 88, function(value)
        SpellEditor.SetField("auraDescription", value);
    end);

    local dangerSeparator = Widgets.NewPanel(section, leftX, -448, contentWidth, 1, Colors.Error, 0.55);
    dangerSeparator:SetBorderSize(0);
    Widgets.NewText(section, "Danger Zone", leftX, -466, "SpellEditorText", Colors.Error);
    Widgets.NewText(section, "Deletion may be rejected when another spell references this definition.", leftX, -490, "SpellEditorMuted", Colors.Muted);
    local deleteButton = Widgets.NewButton(section, draft.isCreate and "Discard New Draft" or "Delete Spell", Layout.BodyWidth - 224, -455, 176, 32, nil, false);
    deleteButton.text:SetColor(Colors.Error);
    deleteButton:SetOnMouseUp(function()
        if draft.isCreate then
            Dialogs.Confirm("Discard this unsynchronized new spell?", function()
                SpellEditor.DiscardDraft();
                selectedSpellID = nil;
                Actions.RefreshAll();
            end);
        else
            Dialogs.Confirm(`Delete Spell {draft.spellID}? The server may reject deletion because another spell references it.`, function()
                SpellEditor.Delete(draft.spellID);
                Actions.RefreshAll();
            end);
        end
    end);
    return 556;
end

function ViewBuilders.Targeting(parent)
    local section = Widgets.NewSection(parent, "Authoring Targeting", 0, 390);
    Widgets.NewText(section, "The anchor is validated once; the shape and recipient mask resolve effect targets at cast completion.",
        14, Widgets.GetSectionDescriptionY(), "SpellEditorMuted", Colors.Muted);
    Widgets.NewStackedDropdownField(section, "Target Anchor", draft.targetSelector, catalog.targetSelectors, 14, -62, 220, function(value)
        local accepted = SpellEditor.SetField("targetSelector", value);
        local selector = DataLookup.FindOption(catalog.targetSelectors, value);
        if accepted and selector and selector.name == "GroundPosition" then
            local anyRelation = DataLookup.FindOption(catalog.targetRelations, 0);
            SpellEditor.SetField("targetRelation", anyRelation and anyRelation.value or 0i);
            for _, option in catalog.rangePolicies do
                if option.name == "Distance" then
                    SpellEditor.SetField("rangePolicy", option.value);
                    break;
                end
            end
            if draft.maximumRange <= 0 then
                SpellEditor.SetField("maximumRange", 30);
            end
        end
        return accepted;
    end);
    Widgets.NewStackedDropdownField(section, "Target Shape", draft.targetShape, catalog.targetShapes, 248, -62, 220, function(value)
        local accepted = SpellEditor.SetField("targetShape", value);
        local shape = DataLookup.FindOption(catalog.targetShapes, value);
        if accepted and shape and shape.name == "Single" then
            SpellEditor.SetField("targetRadius", 0);
            SpellEditor.SetField("maximumTargets", 1i);
        elseif accepted and shape and shape.name == "Radius" then
            if draft.targetRadius <= 0 then SpellEditor.SetField("targetRadius", 5) end;
            if integer.le(draft.maximumTargets, 1i) then SpellEditor.SetField("maximumTargets", 8i) end;
        end
        return accepted;
    end);
    Widgets.NewStackedDropdownField(section, "Anchor Relation", draft.targetRelation, catalog.targetRelations, 482, -62, 220, function(value)
        return SpellEditor.SetField("targetRelation", value);
    end);

    Widgets.NewStackedDropdownField(section, "Range Policy", draft.rangePolicy, catalog.rangePolicies, 14, -130, 220, function(value)
        local accepted = SpellEditor.SetField("rangePolicy", value);
        local policy = DataLookup.FindOption(catalog.rangePolicies, value);
        if accepted and policy and policy.name ~= "Distance" then
            SpellEditor.SetField("minimumRange", 0);
            SpellEditor.SetField("maximumRange", 0);
        end
        return accepted;
    end);
    local rangePolicy = DataLookup.FindOption(catalog.rangePolicies, draft.rangePolicy);
    local rangeEnabled = rangePolicy == nil or rangePolicy.name == "Distance";
    Widgets.NewStackedFieldInput(section, "Minimum Range", rangeEnabled and draft.minimumRange or "Unavailable", 248, -130, 220, function(value)
        Widgets.CommitNumber(value, function(number) SpellEditor.SetField("minimumRange", number) end);
    end, rangeEnabled);
    Widgets.NewStackedFieldInput(section, "Maximum Range", rangeEnabled and draft.maximumRange or "Unavailable", 482, -130, 220, function(value)
        Widgets.CommitNumber(value, function(number) SpellEditor.SetField("maximumRange", number) end);
    end, rangeEnabled);

    local shape = DataLookup.FindOption(catalog.targetShapes, draft.targetShape);
    local radiusEnabled = shape == nil or shape.name == "Radius";
    Widgets.NewStackedFieldInput(section, "Target Radius", radiusEnabled and draft.targetRadius or "Unavailable", 14, -198, 220, function(value)
        Widgets.CommitNumber(value, function(number) SpellEditor.SetField("targetRadius", number) end);
    end, radiusEnabled);
    Widgets.NewStackedFieldInput(section, "Maximum Targets", radiusEnabled and draft.maximumTargets or "Unavailable", 248, -198, 220, function(value)
        Widgets.CommitNumber(value, function(number)
            SpellEditor.SetField("maximumTargets", DataLookup.ToInteger(number))
        end);
    end, radiusEnabled);

    local selector = DataLookup.FindOption(catalog.targetSelectors, draft.targetSelector);
    local relation = DataLookup.FindOption(catalog.targetRelations, draft.targetRelation);
    local rangeSummary = rangePolicy and rangePolicy.name == "None" and "No range check"
        or rangePolicy and rangePolicy.name == "Distance" and `{draft.minimumRange}-{draft.maximumRange}`
        or (rangePolicy and rangePolicy.name or "Unknown range policy");
    Widgets.NewText(section, "Resolved Behavior", 482, -198, "SpellEditorMuted", Colors.Text);
    local resolvedPanel = Widgets.NewPanel(section, 482, -218, 220, Layout.ControlHeight, Colors.Title, 0.72);
    local resolvedSummary = `{selector and selector.name or "Unknown"} / {shape and shape.name or "Unknown"} / {relation and relation.name or "Unknown"} / {rangeSummary}`;
    Widgets.NewVerticallyCenteredSingleLineText(resolvedPanel, resolvedSummary, 10, 0, 200, "SpellEditorMuted", Colors.Success);

    local recipientOptions = {};
    for _, option in catalog.targetRecipientMasks do
        if option.value ~= 0i
            and integer.band(option.value, integer.sub(option.value, 1i)) == 0i
        then
            table.insert(recipientOptions, option);
        end
    end
    Widgets.NewStackedFlagsField(section, "Effect Recipients", draft.targetRecipientMask, recipientOptions, 14, -266, 220, function(value)
        return SpellEditor.SetField("targetRecipientMask", value);
    end);
    Widgets.NewText(section, "Self is classified before faction relations. Friendly uses CanAssist; Attackable uses CanAttack.",
        248, -266, "SpellEditorMuted", Colors.Muted);
    Widgets.NewText(section, "Friendly + Attackable therefore affects both groups without affecting the caster.",
        248, -290, "SpellEditorMuted", Colors.Muted);
    return 406;
end

function ViewBuilders.Effects(parent)
    local catalogHeight = 0;
    if effectCatalogOpen then
        local targetedCount = 0;
        local targetlessCount = 0;
        for _, descriptor in catalog.effects do
            if descriptor.targetMode == "Required" then targetedCount += 1 else targetlessCount += 1 end;
        end
        catalogHeight = 82 + math.ceil(targetedCount / 2) * 36 + math.ceil(targetlessCount / 2) * 36;
    end
    local effectRowHeight = 58;
    local effectRowStride = 64;
    local effectActionSize = 24;
    local effectActionY = -((effectRowHeight - effectActionSize) / 2);
    local effectNameHeight = math.ceil(UI.CalculateTextSize("Ag", "SpellEditorEffectName").y);
    local effectIDHeight = math.ceil(UI.CalculateTextSize("Ag", "SpellEditorMuted").y);
    local effectTextGap = 2;
    local effectNameY = (effectIDHeight + effectTextGap) * 0.5;
    local effectIDY = -(effectNameHeight + effectTextGap) * 0.5;
    local sectionDescriptionY = Widgets.GetSectionDescriptionY();
    local sectionDescriptionHeight = math.ceil(UI.CalculateTextSize("Ag", "SpellEditorMuted").y);
    local firstEffectRowY = sectionDescriptionY - sectionDescriptionHeight - 10;
    local listHeight = math.max(190, #draft.effects * effectRowStride + math.abs(firstEffectRowY) + 54 + catalogHeight);
    local section = Widgets.NewSection(parent, "Effect List", 0, listHeight);
    Widgets.NewText(section, "Effects execute from top to bottom. Select one to edit its catalog-defined parameters.",
        14, sectionDescriptionY, "SpellEditorMuted", Colors.Muted);
    for index, effect in draft.effects do
        local y = firstEffectRowY - (index - 1) * effectRowStride;
        local descriptor = DataLookup.FindCatalogEffect(effect.type);
        local row = Widgets.NewButton(section, "", 12, y, Layout.BodyWidth - 60, effectRowHeight, nil, false);
        row:SetHighlighted(selectedEffectID == effect.id);
        row:SetOnMouseUp(function()
            selectedEffectID = effect.id;
            Actions.RefreshWorkspace();
        end);
        Widgets.NewVerticallyCenteredText(row.root, `{index}`, 10, 0, "SpellEditorMuted", Colors.Muted);
        Widgets.NewVerticallyCenteredSingleLineText(row.root, descriptor and DataLookup.FormatIdentifier(descriptor.name) or "Unknown Effect",
            36, effectNameY, 278, "SpellEditorEffectName", Colors.Text);
        Widgets.NewVerticallyCenteredText(row.root, `Effect ID {effect.id}`, 36, effectIDY, "SpellEditorMuted", Colors.Muted);
        local targetSummary = descriptor and descriptor.targetMode == "Required"
            and "Uses spell target"
            or "No target required";
        Widgets.NewVerticallyCenteredSingleLineText(row.root, targetSummary, 314, 0, 206, "SpellEditorMuted",
            descriptor and descriptor.targetMode == "Required" and Colors.Success or Colors.Muted);
        local effectDiagnostics = DataLookup.GetEffectDiagnostics(effect.id);
        if #effectDiagnostics > 0 then
            local errorLabel = #effectDiagnostics == 1 and "1 error" or `{#effectDiagnostics} errors`;
            Widgets.NewVerticallyCenteredText(row.root, errorLabel, 424, 0, "SpellEditorMuted", Colors.Error);
        end
        local moveUp = Widgets.NewIconButton(row.root, "texture/ui/icons/50x50/white/arrow_up.png", Layout.BodyWidth - 238, effectActionY, effectActionSize, function()
            if SpellEditor.MoveEffect(effect.id, -1i) then Actions.RefreshAll() end;
        end);
        moveUp:SetVisualEnabled(index > 1);
        moveUp:SetInteractable(index > 1);
        local moveDown = Widgets.NewIconButton(row.root, "texture/ui/icons/50x50/white/arrow_down.png", Layout.BodyWidth - 216, effectActionY, effectActionSize, function()
            if SpellEditor.MoveEffect(effect.id, 1i) then Actions.RefreshAll() end;
        end);
        moveDown:SetVisualEnabled(index < #draft.effects);
        moveDown:SetInteractable(index < #draft.effects);
        Widgets.NewIconButton(row.root, "texture/ui/icons/50x50/white/cross.png", Layout.BodyWidth - 164, effectActionY, effectActionSize, function()
            if SpellEditor.RemoveEffect(effect.id) then
                if selectedEffectID == effect.id then selectedEffectID = nil end;
                Actions.RefreshAll();
            end
        end, Colors.Error, vec.New(1.0, 0.46, 0.46));
    end

    local addY = firstEffectRowY - #draft.effects * effectRowStride - 10;
    Widgets.NewButton(section, effectCatalogOpen and "Close Catalog" or "+ Add Effect", 12, addY, 140, 32, function()
        effectCatalogOpen = not effectCatalogOpen;
        Actions.RefreshWorkspace();
    end, true);

    if effectCatalogOpen then
        local catalogY = addY - 46;
        local function AddCatalogGroup(title, requiresTarget)
            Widgets.NewText(section, title, 14, catalogY, "SpellEditorText", Colors.Text);
            catalogY -= 28;
            local column = 0;
            for _, descriptor in catalog.effects do
                if (descriptor.targetMode == "Required") == requiresTarget then
                    local columnWidth = (Layout.BodyWidth - 76) / 2;
                    local x = 14 + column * (columnWidth + 8);
                    local button = Widgets.NewButton(section, DataLookup.FormatIdentifier(descriptor.name), x, catalogY, columnWidth, 30, function()
                        local effectID = SpellEditor.AddEffect(descriptor.value);
                        if effectID ~= 0i then
                            selectedEffectID = effectID;
                            effectCatalogOpen = false;
                            Actions.RefreshAll();
                        end
                    end, false);
                    button.text:SetColor(requiresTarget and Colors.Text or Colors.Muted);
                    column += 1;
                    if column == 2 then
                        column = 0;
                        catalogY -= 36;
                    end
                end
            end
            if column ~= 0 then catalogY -= 36 end;
            catalogY -= 8;
        end
        AddCatalogGroup("Target-requiring effects", true);
        AddCatalogGroup("Targetless effects", false);
    end
    return listHeight + 10;
end

function ViewBuilders.Aura(parent)
    local sectionWidth = Layout.BodyWidth - Layout.Padding * 2;
    local section = Widgets.NewSection(parent, "Aura Definition", 0, draft.aura and 430 or 170, sectionWidth);
    if not draft.aura then
        Widgets.NewText(section, "This spell does not currently own an aura definition.", 18, -48, "SpellEditorText", Colors.Warning);
        Widgets.NewText(section, "Add one to make a directly cast aura spell apply its own aura definition.", 18, -78, "SpellEditorMuted", Colors.Muted);
        Widgets.NewButton(section, "+ Add Aura Definition", 18, -116, 206, 36, function()
            SpellEditor.SetAuraEnabled(true);
            Actions.RefreshAll();
        end, true);
        return 186;
    end

    local aura = draft.aura;
    local leftX = 18;
    local leftWidth = 366;
    local rightX = 400;
    local rightWidth = sectionWidth - rightX - 18;
    Widgets.NewFieldInput(section, "Duration (seconds)", aura.duration, leftX, -52, leftWidth, function(value)
        Widgets.CommitNumber(value, function(number)
            SpellEditor.SetAuraField("duration", number == 0 and -1 or number);
        end);
    end);
    Widgets.NewFieldInput(section, "Stacks Per Application", aura.stacksPerApplication, leftX, -96, leftWidth, function(value)
        Widgets.CommitNumber(value, function(number)
            SpellEditor.SetAuraField("stacksPerApplication", DataLookup.ToInteger(number))
        end);
    end);
    Widgets.NewFieldInput(section, "Maximum Stacks", aura.maximumStacks, leftX, -140, leftWidth, function(value)
        Widgets.CommitNumber(value, function(number)
            SpellEditor.SetAuraField("maximumStacks", DataLookup.ToInteger(number))
        end);
    end);
    Widgets.NewDropdownField(section, "Application Policy", aura.applicationPolicy, catalog.auraApplicationPolicies, leftX, -184, leftWidth, function(value)
        SpellEditor.SetAuraField("applicationPolicy", value);
    end);
    Widgets.NewDropdownField(section, "Disposition", aura.disposition, catalog.auraDispositions, leftX, -228, leftWidth, function(value)
        SpellEditor.SetAuraField("disposition", value);
    end);
    Widgets.NewDropdownField(section, "Dispel Type", aura.dispelType, catalog.auraDispelTypes, leftX, -272, leftWidth, function(value)
        SpellEditor.SetAuraField("dispelType", value);
    end);

    Widgets.NewText(section, "Lifecycle Behavior", rightX, -46, "SpellEditorText", Colors.Text);
    local persistFlag = nil;
    local casterDeathFlag = nil;
    local casterWorldExitFlag = nil;
    for _, flag in catalog.auraLifecycleFlags do
        local normalizedName = string.lower(flag.name);
        if string.find(normalizedName, "persist", 1, true) and string.find(normalizedName, "target", 1, true) then persistFlag = flag end;
        if string.find(normalizedName, "caster", 1, true) and string.find(normalizedName, "death", 1, true) then casterDeathFlag = flag end;
        if string.find(normalizedName, "caster", 1, true) and string.find(normalizedName, "world", 1, true) then casterWorldExitFlag = flag end;
    end
    local persistsThroughDeath = persistFlag and integer.btest(aura.lifecycleFlags, persistFlag.value);
    local targetDeathOptions = {
        { value = 0, name = "Remove when target dies" },
        { value = 1, name = "Persist through target death" },
    };
    Widgets.NewStackedDropdownField(section, "Target Death Behavior", persistsThroughDeath and 1 or 0,
        targetDeathOptions, rightX, -76, rightWidth, function(value)
        if persistFlag then
            local newFlags = value == 1
                and integer.bor(aura.lifecycleFlags, persistFlag.value)
                or integer.band(aura.lifecycleFlags, integer.bnot(persistFlag.value));
            SpellEditor.SetAuraField("lifecycleFlags", newFlags);
        end
    end);

    local function AddLifecycleCheckbox(label, flag, y)
        if not flag then
            return;
        end

        local isEnabled = integer.btest(aura.lifecycleFlags, flag.value);
        Widgets.NewStackedBooleanField(section, label, isEnabled and 1i or 0i, rightX, y, function(value)
            local newFlags = value == 1i
                and integer.bor(aura.lifecycleFlags, flag.value)
                or integer.band(aura.lifecycleFlags, integer.bnot(flag.value));
            SpellEditor.SetAuraField("lifecycleFlags", newFlags);
        end);
    end

    AddLifecycleCheckbox("Remove when caster dies", casterDeathFlag, -148);
    AddLifecycleCheckbox("Remove when caster leaves the world", casterWorldExitFlag, -182);

    local applicationPolicy = DataLookup.FindOption(catalog.auraApplicationPolicies, aura.applicationPolicy);
    local disposition = DataLookup.FindOption(catalog.auraDispositions, aura.disposition);
    local durationLabel = aura.duration == -1 and "Infinite duration" or `{aura.duration}s duration`;
    local behaviorPanel = Widgets.NewPanel(section, rightX, -314, rightWidth, 100, Colors.Title, 0.72);
    Widgets.NewText(behaviorPanel, "Behavior Summary", 12, -10, "SpellEditorText", Colors.Text);
    Widgets.NewWrappedText(behaviorPanel,
        `{durationLabel}; {aura.stacksPerApplication} stack(s) per application, {aura.maximumStacks} maximum.\n{applicationPolicy and applicationPolicy.name or "Unknown"}; {disposition and disposition.name or "Unknown"}.\n{persistsThroughDeath and "Persists through target death" or "Removed on target death"}.`,
        12, -36, rightWidth - 24, "SpellEditorMuted", Colors.Muted);

    local removeAura = Widgets.NewButton(section, "Remove Aura Definition", leftX, -382, 210, 32, function()
        Dialogs.Confirm("Remove this spell's aura definition and all owned aura constraints?", function()
            SpellEditor.SetAuraEnabled(false);
            selectedConstraintID = nil;
            Actions.RefreshAll();
        end);
    end, false);
    removeAura.text:SetColor(Colors.Error);
    return 446;
end

function DataLookup.GetReferencedAuraSpellID()
    if draft and not draft.aura then
        for _, effect in draft.effects do
            local descriptor = DataLookup.FindCatalogEffect(effect.type);
            if descriptor
                and descriptor.name == "ApplyAura"
                and effect.parameters[1]
                and integer.gt(effect.parameters[1], 0i)
            then
                return effect.parameters[1];
            end
        end
    end
    return nil;
end

function ViewBuilders.AuraConstraintList(parent, constraints, isReadOnly, referencedSpellID, height)
    local title = isReadOnly and "Referenced Aura Constraints (Read-Only)" or "Aura Constraints";
    local section = Widgets.NewSection(parent, title, 0, height);
    local referenced = referencedSpellID and DataLookup.FindSpell(referencedSpellID) or nil;
    local description = isReadOnly
        and `These constraints belong to {referenced and referenced.name or "missing spell"} (Spell {referencedSpellID}).`
        or "Constraints currently applied to this aura. Each one is evaluated independently.";
    local descriptionY = Widgets.GetSectionDescriptionY();
    Widgets.NewSingleLineText(section, description, 14, descriptionY,
        isReadOnly and Layout.BodyWidth - 280 or Layout.BodyWidth - 56,
        "SpellEditorMuted", isReadOnly and not referenced and Colors.Error or Colors.Muted);
    if isReadOnly and referenced then
        Widgets.NewButton(section, `Open Spell {referencedSpellID}`, Layout.BodyWidth - 224, -30, 150, 28, function()
            Actions.OpenSpell(referencedSpellID);
        end, false);
    end

    if #constraints == 0 then
        local descriptionHeight = math.ceil(UI.CalculateTextSize("Ag", "SpellEditorMuted").y);
        Widgets.NewText(section, isReadOnly
            and "The referenced aura has no constraints."
            or "This aura does not use any shared constraint groups yet.",
            14, descriptionY - descriptionHeight - 12, "SpellEditorMuted", Colors.Muted);
        return;
    end

    local listTop = -66;
    local listWidth = Layout.BodyWidth - 44;
    local listHeight = height - 78;
    local constraintList = Components.ScrollBox.New(section, vec.New(14, listTop), vec.New(listWidth, listHeight), 2i, {
        panelTemplate = "DebugDarkGrey",
        contentPanelTemplate = "SpellEditorFlatPanel",
        contentSizeX = listWidth,
        contentSizeY = listHeight,
        verticalScrollBar = true,
        scrollBarWidth = 9,
        alpha = 0.0,
    });
    constraintList:SetAnchor(vec.New(0.0, 1.0));
    constraintList:SetRelativePoint(vec.New(0.0, 1.0));

    local rowHeight = 52;
    local rowStride = 56;
    local rowWidth = listWidth - 12;
    constraintList:SetContentHeight(math.max(listHeight, #constraints * rowStride + 2));
    for index, constraint in constraints do
        local group = DataLookup.FindConstraintGroup(constraint.groupID);
        local scope = DataLookup.FindOption(catalog.constraintScopes, constraint.scope);
        local overflow = DataLookup.FindOption(catalog.constraintOverflowBehaviors, constraint.overflowBehavior);
        local row = Widgets.NewButton(constraintList.content, "", 0, -((index - 1) * rowStride), rowWidth, rowHeight, nil, false);
        row:SetHighlighted(selectedConstraintID == constraint.id and selectedConstraintReadOnly == isReadOnly);
        row:SetOnMouseUp(function()
            selectedConstraintID = constraint.id;
            selectedConstraintReadOnly = isReadOnly;
            Actions.RefreshWorkspace();
        end);
        Widgets.NewVerticallyCenteredSingleLineText(row.root,
            group and group.name or `Missing Group {constraint.groupID}`,
            12, 9, rowWidth - 154, "SpellEditorEffectName", group and Colors.Text or Colors.Error);
        Widgets.NewVerticallyCenteredSingleLineText(row.root,
            `{scope and scope.name or "Unknown"}  |  Maximum {constraint.maximumApplications}  |  {overflow and overflow.name or "Unknown"}`,
            12, -9, rowWidth - 154, "SpellEditorMuted", Colors.Muted);
        if isReadOnly then
            Widgets.NewVerticallyCenteredText(row.root, "Read-only", rowWidth - 82, 0, "SpellEditorMuted", Colors.Muted);
        else
            Widgets.NewIconButton(row.root, "texture/ui/icons/50x50/white/cross.png",
                rowWidth - 34, -(rowHeight - 20) / 2, 20, function()
                if SpellEditor.RemoveConstraint(constraint.id) then
                    if selectedConstraintID == constraint.id then
                        selectedConstraintID = nil;
                    end
                    Actions.RefreshAll();
                end
            end, Colors.Error, vec.New(1.0, 0.46, 0.46));
        end
    end
end

function ViewBuilders.ConstraintGroupCatalog(parent, groups, constraints, y, height)
    local section = Widgets.NewSection(parent, "Shared Constraint Groups", y, height);
    local canMutateGroups = draft.state == "Clean";
    local activeGroupIDs = {};
    local usageCounts = {};
    local inheritedUsageCounts = {};
    local customUsageCounts = {};
    for _, constraint in constraints do
        activeGroupIDs[constraint.groupID] = true;
    end
    for _, constraint in snapshot.constraints or {} do
        usageCounts[constraint.groupID] = (usageCounts[constraint.groupID] or 0) + 1;
        if constraint.overrideMask == 0i then
            inheritedUsageCounts[constraint.groupID] = (inheritedUsageCounts[constraint.groupID] or 0) + 1;
        else
            customUsageCounts[constraint.groupID] = (customUsageCounts[constraint.groupID] or 0) + 1;
        end
    end

    local sectionWidth = Layout.BodyWidth - Layout.Padding * 2;
    local contentX = 14;
    local listWidth = sectionWidth - 28;
    local rowWidth = listWidth - 12;
    local searchRowY = -38;
    local searchRowHeight = 30;
    local createGroupWidth = 104;
    local searchButtonGap = 10;
    local createGroupX = contentX + rowWidth - createGroupWidth;
    local searchWidth = createGroupX - searchButtonGap - contentX;
    local searchInput = Components.InputBox.New(
        section, vec.New(contentX, searchRowY), vec.New(searchWidth, searchRowHeight), 2i, {
        panelTemplate = "SpellEditorInput",
        textTemplate = TEXT,
        placeholderText = "Search constraint groups...",
        defaultText = constraintViewState.search,
    });
    searchInput:SetAnchor(vec.New(0.0, 1.0));
    searchInput:SetRelativePoint(vec.New(0.0, 1.0));
    local createGroup = Widgets.NewButton(
        section, "+ New Group", createGroupX, searchRowY, createGroupWidth, searchRowHeight, function()
        Dialogs.OpenConstraintGroupModal(nil);
    end, true);
    createGroup:SetVisualEnabled(canMutateGroups);
    createGroup:SetInteractable(canMutateGroups);

    local listTop = -76;
    local listHeight = height - 88;
    local groupList = Components.VirtualList.New(
        section, vec.New(contentX, listTop), vec.New(listWidth, listHeight), 2i, {
        rowHeight = 42,
        scrollBox = {
            panelTemplate = "DebugDarkGrey",
            contentPanelTemplate = "SpellEditorFlatPanel",
            contentSizeX = listWidth,
            contentSizeY = listHeight,
            verticalScrollBar = true,
            scrollBarWidth = 9,
            alpha = 0.0,
        },
        createRow = function(rowParent)
            local row = Widgets.NewButton(rowParent, "", 0, 0, rowWidth, 38, nil, false);
            row.currentGroupID = nil;
            row.currentGroupName = "";
            row.canRename = false;
            row.canDelete = false;
            row.canAdd = false;
            row.nameText = Widgets.NewVerticallyCenteredSingleLineText(row.root, "", 12, 8, listWidth - 340, "SpellEditorEffectName", Colors.Text);
            row.metaText = Widgets.NewVerticallyCenteredSingleLineText(row.root, "", 12, -9, listWidth - 340, "SpellEditorMuted", Colors.Muted);
            row.addButton = Widgets.NewButton(row.root, "Add", listWidth - 298, -4, 64, 30, function()
                if not row.canAdd then
                    return;
                end
                local constraintID = SpellEditor.AddConstraint(row.currentGroupID);
                if constraintID ~= 0i then
                    selectedConstraintID = constraintID;
                    selectedConstraintReadOnly = false;
                    Actions.RefreshAll();
                end
            end, true);
            row.renameButton = Widgets.NewButton(row.root, "Edit", listWidth - 226, -4, 78, 30, function()
                if row.canRename then
                    Dialogs.OpenConstraintGroupModal(DataLookup.FindConstraintGroup(row.currentGroupID));
                end
            end, false);
            row.deleteButton = Widgets.NewButton(row.root, "Delete", listWidth - 140, -4, 70, 30, function()
                if not row.canDelete then
                    return;
                end
                Dialogs.Confirm(`Delete constraint group "{row.currentGroupName}"?`, function()
                    if SpellEditor.DeleteConstraintGroup(row.currentGroupID) then
                        Actions.RefreshAll();
                    end
                end);
            end, false);
            return row;
        end,
        bindRow = function(row, _, item)
            row.currentGroupID = item.group.id;
            row.currentGroupName = item.group.name;
            row.canAdd = not item.isAdded;
            row.canRename = canMutateGroups;
            row.canDelete = canMutateGroups and item.usageCount == 0;
            row.nameText:SetText(Components.TextUtil.TruncateToWidth(item.group.name, "SpellEditorEffectName", listWidth - 340));
            local defaultScope = DataLookup.FindOption(catalog.constraintScopes, item.group.defaultScope);
            local defaultOverflow = DataLookup.FindOption(
                catalog.constraintOverflowBehaviors, item.group.defaultOverflowBehavior);
            row.metaText:SetText(
                `Default: {defaultScope and defaultScope.name or "Unknown"} / {item.group.defaultMaximumApplications} / {defaultOverflow and defaultOverflow.name or "Unknown"}  |  {item.inheritedCount} inherited, {item.customCount} custom`);
            row:SetHighlighted(item.isAdded);
            row.addButton.text:SetText(item.isAdded and "Added" or "Add");
            row.addButton:SetVisualEnabled(row.canAdd);
            row.addButton:SetInteractable(row.canAdd);
            row.renameButton:SetVisualEnabled(row.canRename);
            row.renameButton:SetInteractable(row.canRename);
            row.deleteButton:SetVisualEnabled(row.canDelete);
            row.deleteButton:SetInteractable(row.canDelete);
            row.deleteButton.text:SetColor(row.canDelete and Colors.Error or Colors.Muted);
        end,
    });
    groupList:SetAnchor(vec.New(0.0, 1.0));
    groupList:SetRelativePoint(vec.New(0.0, 1.0));

    local function RefreshGroups()
        local items = {};
        for _, group in groups do
            if constraintViewState.search == ""
                or Components.TextUtil.ContainsIgnoreCase(group.name, constraintViewState.search)
                or string.find(tostring(group.id), constraintViewState.search, 1, true) ~= nil
            then
                table.insert(items, {
                    group = group,
                    isAdded = activeGroupIDs[group.id] == true,
                    usageCount = usageCounts[group.id] or 0,
                    inheritedCount = inheritedUsageCounts[group.id] or 0,
                    customCount = customUsageCounts[group.id] or 0,
                });
            end
        end
        groupList:SetItems(items);
    end
    searchInput:SetOnTextChanged(function()
        constraintViewState.search = searchInput.text or "";
        RefreshGroups();
    end);
    RefreshGroups();

    if #groups == 0 then
        Widgets.NewText(section, "No authorable groups exist. Create one before adding an aura constraint.",
            14, -72, "SpellEditorMuted", Colors.Warning);
    end
end

function ViewBuilders.Constraints(parent)
    local referencedSpellID = DataLookup.GetReferencedAuraSpellID();
    local isReadOnly = not draft.aura and referencedSpellID ~= nil;
    local constraints = draft.constraints;
    if isReadOnly then
        constraints = {};
        for _, constraint in snapshot.constraints do
            if constraint.spellID == referencedSpellID then
                table.insert(constraints, constraint);
            end
        end
    end

    local contentHeight = Layout.BodyHeight - Layout.Padding * 2;
    if not draft.aura and not isReadOnly then
        local section = Widgets.NewSection(parent, "Aura Constraints", 0, 150);
        Widgets.NewText(section, "Constraints belong to an aura spell. Add an aura definition first.",
            14, -42, "SpellEditorMuted", Colors.Warning);
        return contentHeight;
    end
    if isReadOnly then
        ViewBuilders.AuraConstraintList(parent, constraints, true, referencedSpellID, contentHeight);
        return contentHeight;
    end

    local sectionGap = 10;
    local constraintSectionHeight = 250;
    local groupSectionHeight = contentHeight - constraintSectionHeight - sectionGap;
    ViewBuilders.AuraConstraintList(parent, constraints, false, nil, constraintSectionHeight);
    ViewBuilders.ConstraintGroupCatalog(parent, snapshot.constraintGroups or {}, constraints,
        -(constraintSectionHeight + sectionGap), groupSectionHeight);
    return contentHeight;
end

function ViewBuilders.Procs(parent)
    local contentHeight = Layout.BodyHeight - Layout.Padding * 2;
    local sectionGap = 10;
    local linkSectionHeight = 250;
    local dataSectionHeight = contentHeight - linkSectionHeight - sectionGap;
    local sectionWidth = Layout.BodyWidth - Layout.Padding * 2;
    local contentX = 14;
    local listWidth = sectionWidth - 28;
    local rowWidth = listWidth - 12;

    local links = Widgets.NewSection(parent, "ProcLinks", 0, linkSectionHeight);
    Widgets.NewText(links, "Spell-owned links connect one visible ProcData definition to selected effect rows.",
        contentX, Widgets.GetSectionDescriptionY(), "SpellEditorMuted", Colors.Muted);
    if #draft.procLinks == 0 then
        Widgets.NewText(links, "This spell does not use any ProcData yet. Add one from the catalog below.",
            contentX, Widgets.GetSectionDescriptionY() - 30, "SpellEditorMuted", Colors.Muted);
    else
        local linkListTop = -66;
        local linkListHeight = linkSectionHeight - 78;
        local linkList = Components.ScrollBox.New(
            links, vec.New(contentX, linkListTop), vec.New(listWidth, linkListHeight), 2i, {
            panelTemplate = "DebugDarkGrey",
            contentPanelTemplate = "SpellEditorFlatPanel",
            contentSizeX = listWidth,
            contentSizeY = linkListHeight,
            verticalScrollBar = true,
            scrollBarWidth = 9,
            alpha = 0.0,
        });
        linkList:SetAnchor(vec.New(0.0, 1.0));
        linkList:SetRelativePoint(vec.New(0.0, 1.0));
        linkList:SetContentHeight(math.max(linkListHeight, #draft.procLinks * 56 + 2));
        for index, procLink in draft.procLinks do
            local procData = DataLookup.FindProcData(procLink.procDataID);
            local row = Widgets.NewButton(linkList.content, "", 0, -((index - 1) * 56), rowWidth, 52, nil, false);
            row:SetHighlighted(selectedProcLinkID == procLink.id);
            row:SetOnMouseUp(function()
                selectedProcLinkID = procLink.id;
                selectedProcDataID = nil;
                Actions.RefreshWorkspace();
            end);
            Widgets.NewVerticallyCenteredSingleLineText(
                row.root,
                procData and DataLookup.GetProcDataName(procData) or `Missing ProcData {procLink.procDataID}`,
                14, 8, rowWidth - 190, "SpellEditorEffectName",
                procData and Colors.Text or Colors.Error);
            Widgets.NewVerticallyCenteredSingleLineText(
                row.root,
                `ProcLink {procLink.id} | {#procLink.selectedEffectIDs} effect rows`,
                14, -9, rowWidth - 190, "SpellEditorMuted", Colors.Muted);
            Widgets.NewIconButton(row.root, "texture/ui/icons/50x50/white/cross.png",
                rowWidth - 34, -16, 20, function()
                if SpellEditor.RemoveProcLink(procLink.id) then
                    if selectedProcLinkID == procLink.id then selectedProcLinkID = nil end;
                    Actions.RefreshAll();
                end
            end, Colors.Error, vec.New(1.0, 0.46, 0.46));
        end
    end

    local dataSection = Widgets.NewSection(
        parent, "Private & Shared ProcData", -(linkSectionHeight + sectionGap), dataSectionHeight);
    local canMutateProcData = (draft.state == "Clean" or draft.state == "Dirty") and not draft.isCreate;
    local canEditProcLinks = draft.state ~= "Synchronizing" and draft.state ~= "Refreshing";
    local searchRowY = -38;
    local searchRowHeight = 30;
    local createWidth = 124;
    local controlGap = 10;
    local createX = contentX + rowWidth - createWidth;
    local searchWidth = createX - controlGap - contentX;
    local searchInput = Components.InputBox.New(
        dataSection, vec.New(contentX, searchRowY), vec.New(searchWidth, searchRowHeight), 2i, {
        panelTemplate = "SpellEditorInput",
        textTemplate = TEXT,
        placeholderText = "Search ProcData...",
        defaultText = procDataViewState.search,
    });
    searchInput:SetAnchor(vec.New(0.0, 1.0));
    searchInput:SetRelativePoint(vec.New(0.0, 1.0));
    local createButton = Widgets.NewButton(
        dataSection, "+ New Private", createX, searchRowY, createWidth, searchRowHeight, function()
        local id = Actions.CreatePrivateProcData();
        if id ~= 0i then
            selectedProcDataID = id;
            selectedProcLinkID = nil;
            Actions.RefreshAll();
        end
    end, true);
    createButton:SetVisualEnabled(canMutateProcData);
    createButton:SetInteractable(canMutateProcData);

    local usageCounts = {};
    for _, procLink in snapshot.procLinks or {} do
        usageCounts[procLink.procDataID] = (usageCounts[procLink.procDataID] or 0) + 1;
    end
    local linkedProcDataIDs = {};
    for _, procLink in draft.procLinks do
        linkedProcDataIDs[procLink.procDataID] = true;
    end
    local catalogList = Components.VirtualList.New(
        dataSection, vec.New(contentX, -76), vec.New(listWidth, dataSectionHeight - 88), 2i, {
        rowHeight = 46,
        scrollBox = {
            panelTemplate = "DebugDarkGrey",
            contentPanelTemplate = "SpellEditorFlatPanel",
            contentSizeX = listWidth,
            contentSizeY = dataSectionHeight - 88,
            verticalScrollBar = true,
            scrollBarWidth = 9,
            alpha = 0.0,
        },
        createRow = function(rowParent)
            local row = Widgets.NewButton(rowParent, "", 0, 0, rowWidth, 42, nil, false);
            row.currentProcData = nil;
            row.canAdd = false;
            row.canDelete = false;
            row.nameText = Widgets.NewVerticallyCenteredSingleLineText(
                row.root, "", 12, 8, rowWidth - 230, "SpellEditorEffectName", Colors.Text);
            row.metaText = Widgets.NewVerticallyCenteredSingleLineText(
                row.root, "", 12, -9, rowWidth - 230, "SpellEditorMuted", Colors.Muted);
            row:SetOnMouseUp(function()
                if row.currentProcData then
                    selectedProcDataID = row.currentProcData.id;
                    selectedProcLinkID = nil;
                    Actions.RefreshWorkspace();
                end
            end);
            row.addButton = Widgets.NewButton(row.root, "Add Link", rowWidth - 188, -4, 78, 30, function()
                if not row.canAdd or not row.currentProcData then
                    return;
                end
                local id = SpellEditor.AddProcLink(row.currentProcData.id);
                if id ~= 0i then
                    selectedProcLinkID = id;
                    selectedProcDataID = nil;
                    Actions.RefreshAll();
                end
            end, true);
            row.deleteButton = Widgets.NewButton(row.root, "Delete", rowWidth - 102, -4, 70, 30, function()
                if not row.canDelete or not row.currentProcData then
                    return;
                end
                local procDataID = row.currentProcData.id;
                Dialogs.Confirm(`Delete ProcData {procDataID}?`, function()
                    if SpellEditor.DeleteProcData(procDataID) then
                        selectedProcDataID = nil;
                        Actions.RefreshAll();
                    end
                end);
            end, false);
            return row;
        end,
        bindRow = function(row, _, procData)
            row.currentProcData = procData;
            row.canAdd = canEditProcLinks and not linkedProcDataIDs[procData.id];
            row.canDelete = canMutateProcData and (usageCounts[procData.id] or 0) == 0;
            row.nameText:SetText(Components.TextUtil.TruncateToWidth(
                DataLookup.GetProcDataName(procData), "SpellEditorEffectName", rowWidth - 230));
            local visibility = procData.ownerSpellID == 0i and "Shared" or "Private";
            row.metaText:SetText(
                `{visibility} | ID {procData.id} | PPM {procData.procsPerMinute} | Chance {math.floor(procData.chanceToProc * 100)}% | Used by {usageCounts[procData.id] or 0}`);
            row:SetHighlighted(selectedProcDataID == procData.id);
            row.addButton:SetText(linkedProcDataIDs[procData.id] and "Added" or "Add Link");
            row.addButton:SetVisualEnabled(row.canAdd);
            row.addButton:SetInteractable(row.canAdd);
            row.deleteButton:SetVisualEnabled(row.canDelete);
            row.deleteButton:SetInteractable(row.canDelete);
        end,
    });
    catalogList:SetAnchor(vec.New(0.0, 1.0));
    catalogList:SetRelativePoint(vec.New(0.0, 1.0));

    local function RefreshCatalog()
        local items = {};
        for _, procData in snapshot.procDataDefinitions or {} do
            local name = DataLookup.GetProcDataName(procData);
            if DataLookup.IsProcDataVisible(procData)
                and (procDataViewState.search == ""
                    or Components.TextUtil.ContainsIgnoreCase(name, procDataViewState.search)
                    or string.find(tostring(procData.id), procDataViewState.search, 1, true) ~= nil)
            then
                table.insert(items, procData);
            end
        end
        catalogList:SetItems(items);
    end
    searchInput:SetOnTextChanged(function()
        procDataViewState.search = searchInput.text or "";
        RefreshCatalog();
    end);
    RefreshCatalog();
    return contentHeight;
end

function Widgets.CalculateWrappedTextHeight(value, template, wrapWidth)
    local textSize = UI.CalculateTextSize(value, template);
    return math.max(textSize.y, math.ceil(textSize.x / wrapWidth) * textSize.y);
end

function Widgets.CalculateEffectDiagnosticsHeight(effectDiagnostics, textWidth)
    if #effectDiagnostics == 0 then
        return 0;
    end
    local height = 26;
    for _, diagnostic in effectDiagnostics do
        height += Widgets.CalculateWrappedTextHeight(`- {diagnostic.message}`, "SpellEditorMuted", textWidth) + 8;
    end
    return height + 8;
end

function Widgets.CalculateEffectInspectorHeight(descriptor, effectDiagnostics, textWidth)
    local y = -220;
    y -= #effectDiagnostics > 0 and Widgets.CalculateEffectDiagnosticsHeight(effectDiagnostics, textWidth) or 96;
    if descriptor then
        for _, parameter in descriptor.parameters do
            if parameter.kind == PARAMETER_KIND.Flags then
                y -= 36 + #parameter.options * 24;
            elseif parameter.kind == PARAMETER_KIND.Boolean then
                y -= 38;
            elseif parameter.kind == PARAMETER_KIND.SpellID then
                y -= 94;
            else
                y -= DataLookup.GetParameterHelp(parameter) and 76 or 58;
            end
        end
    end
    return math.max(Layout.InspectorViewportHeight, -y + 12);
end

function ViewBuilders.EffectInspector(parent, effect)
    local descriptor = DataLookup.FindCatalogEffect(effect.type);
    local effectDiagnostics = DataLookup.GetEffectDiagnostics(effect.id);
    local contentWidth = parent:GetWidth();
    local fieldWidth = contentWidth - 28;
    local textWidth = contentWidth - 28;
    Widgets.NewText(parent, "Effect Inspector", 14, -14, "SpellEditorTitle", Colors.Text);
    Widgets.NewText(parent, `Effect ID {effect.id}`, 14, -46, "SpellEditorMuted", Colors.Muted);
    Widgets.NewStackedDropdownField(parent, "Effect Type", effect.type, DataLookup.BuildDisplayOptions(catalog.effects), 14, -72, fieldWidth, function(value)
        SpellEditor.SetEffectField(effect.id, "type", value);
    end);
    Widgets.NewText(parent, "Execution", 14, -128, "SpellEditorText", Colors.Text);
    Widgets.NewWrappedText(parent, descriptor and `Handler: {DataLookup.FormatIdentifier(descriptor.handler)}` or "Missing catalog descriptor",
        14, -152, textWidth, "SpellEditorMuted", descriptor and Colors.Muted or Colors.Error);
    local targetDescription = descriptor and descriptor.targetMode == "Required"
        and `Requires a {string.lower(descriptor.targetState)} {string.lower(descriptor.targetKind)} target. Uses the target resolved by the spell.`
        or "Does not consume the target resolved by the spell.";
    Widgets.NewWrappedText(parent, targetDescription, 14, -172, textWidth, "SpellEditorMuted", Colors.Muted);
    local y = -220;
    if descriptor then
        for _, parameter in descriptor.parameters do
            local currentValue = effect.parameters[integer.tonumber(parameter.index)];
            local label = DataLookup.FormatIdentifier(parameter.name);
            if parameter.kind == PARAMETER_KIND.Boolean then
                Widgets.NewStackedBooleanField(parent, label, currentValue, 14, y, function(value)
                    SpellEditor.SetEffectParameter(effect.id, parameter.index, value);
                end);
                y -= 38;
            elseif parameter.kind == PARAMETER_KIND.Enum then
                Widgets.NewStackedDropdownField(parent, label, currentValue, DataLookup.BuildDisplayOptions(parameter.options), 14, y, fieldWidth, function(value)
                    SpellEditor.SetEffectParameter(effect.id, parameter.index, value);
                end);
                y -= 58;
            elseif parameter.kind == PARAMETER_KIND.Flags then
                local usedHeight = Widgets.NewStackedFlagsField(parent, label, currentValue, parameter.options, 14, y, fieldWidth, function(value)
                    SpellEditor.SetEffectParameter(effect.id, parameter.index, value);
                end);
                y -= usedHeight + 8;
            elseif parameter.kind == PARAMETER_KIND.SpellID then
                Widgets.NewStackedSpellField(parent, label, parameter, currentValue, 14, y, fieldWidth, function(value)
                    SpellEditor.SetEffectParameter(effect.id, parameter.index, value);
                end);
                local referencedSpell = integer.gt(currentValue, 0i) and DataLookup.FindSpell(currentValue) or nil;
                if referencedSpell then
                    Widgets.NewButton(parent, "Open Reference", 14, y - 58, fieldWidth, 28, function()
                        Actions.OpenSpell(referencedSpell.id);
                    end, false);
                elseif integer.gt(currentValue, 0i) then
                    Widgets.NewWrappedText(parent, `Referenced spell {currentValue} is unavailable.`, 14, y - 56, fieldWidth, "SpellEditorMuted", Colors.Error);
                end
                y -= 94;
            else
                Widgets.NewStackedFieldInput(parent, label, currentValue, 14, y, fieldWidth, function(value)
                    Widgets.CommitNumber(value, function(number)
                        SpellEditor.SetEffectParameter(effect.id, parameter.index, DataLookup.ToInteger(number))
                    end);
                end);
                local help = DataLookup.GetParameterHelp(parameter);
                if help then
                    Widgets.NewWrappedText(parent, help, 14, y - 54, textWidth, "SpellEditorMuted", Colors.Muted);
                end
                y -= help and 76 or 58;
            end
        end
    end
    if #effectDiagnostics > 0 then
        Widgets.NewText(parent, `Validation ({#effectDiagnostics})`, 14, y, "SpellEditorText", Colors.Error);
        y -= 26;
        for _, diagnostic in effectDiagnostics do
            local message = `- {diagnostic.message}`;
            Widgets.NewWrappedText(parent, message, 14, y, textWidth, "SpellEditorMuted", Colors.Error);
            y -= Widgets.CalculateWrappedTextHeight(message, "SpellEditorMuted", textWidth) + 8;
        end
        y -= 8;
    else
        Widgets.NewText(parent, "Diagnostics", 14, y, "SpellEditorText", Colors.Text);
        local message = #diagnostics > 0
            and `This effect has no errors. {#diagnostics} validation errors exist elsewhere in the spell.`
            or "This effect has no validation errors.";
        Widgets.NewWrappedText(parent, message, 14, y - 26, textWidth, "SpellEditorMuted",
            #diagnostics > 0 and Colors.Warning or Colors.Success);
        if #diagnostics > 0 then
            Widgets.NewButton(parent, "View Errors", 14, y - 72, 112, 28, Dialogs.OpenDiagnostics, false);
        end
        y -= 96;
    end
    --local remove = Widgets.NewButton(parent, "Remove Effect", 14, y - 8, 142, 28, function()
    --    SpellEditor.RemoveEffect(effect.id);
    --    selectedEffectID = nil;
    --    Actions.RefreshAll();
    --end, false);
    --remove.text:SetColor(Colors.Error);
end

function ViewBuilders.ConstraintInspector(parent, constraint, readOnly)
    local contentWidth = parent:GetWidth();
    local fieldWidth = contentWidth - 28;
    local textWidth = contentWidth - 28;
    local scope = DataLookup.FindOption(catalog.constraintScopes, constraint.scope);
    local overflow = DataLookup.FindOption(catalog.constraintOverflowBehaviors, constraint.overflowBehavior);
    local function FindOverrideFlag(name)
        for _, option in catalog.constraintOverrideFlags or {} do
            if option.name == name then
                return option.value;
            end
        end
            return 0i;
    end
    local scopeOverride = integer.btest(constraint.overrideMask or 0i, FindOverrideFlag("Scope"));
    local maximumOverride = integer.btest(
        constraint.overrideMask or 0i, FindOverrideFlag("MaximumApplications"));
    local overflowOverride = integer.btest(
        constraint.overrideMask or 0i, FindOverrideFlag("OverflowBehavior"));
    local scopeDescription = scope and scope.name == "Target"
        and "Counts matching auras on one target, regardless of caster."
        or scope and scope.name == "CasterTarget"
            and "Counts matching auras independently for each caster-target pair."
        or scope and scope.name == "CasterWorld"
            and "Counts matching auras across all targets for one caster in the current world."
        or "The selected scope determines which matching aura applications share this limit.";
    local overflowDescription = overflow and overflow.name == "ReplaceOldest"
        and "When the limit is reached, the oldest matching aura is removed to make room."
        or overflow and overflow.name == "RejectNew"
            and "When the limit is reached, the new aura application is rejected."
        or "The overflow behavior determines what happens when this limit is reached.";

    Widgets.NewText(parent, "Constraint Inspector", 14, -14, "SpellEditorTitle", Colors.Text);
    Widgets.NewText(parent, `Constraint ID {constraint.id}`, 14, -46, "SpellEditorMuted", Colors.Muted);
    Widgets.NewText(parent, readOnly and "Referenced Definition" or "Definition", 14, -76, "SpellEditorText", Colors.Text);
    if readOnly then
        Widgets.NewWrappedText(parent, "Owned by the referenced aura and read-only from this spell.",
            14, -100, textWidth, "SpellEditorMuted", Colors.Warning);
        Widgets.NewStackedReadOnlyField(parent, "Scope", scope and scope.name or constraint.scope, 14, -136, fieldWidth);
        Widgets.NewStackedReadOnlyField(parent, "Maximum Applications", constraint.maximumApplications, 14, -202, fieldWidth);
        Widgets.NewStackedReadOnlyField(parent, "Overflow Behavior", overflow and overflow.name or constraint.overflowBehavior, 14, -268, fieldWidth);
        local behaviorTitleY = -336;
        local scopeDescriptionY = behaviorTitleY - 24;
        local scopeDescriptionHeight = Widgets.CalculateWrappedTextHeight(scopeDescription, "SpellEditorMuted", textWidth);
        local overflowDescriptionY = scopeDescriptionY - scopeDescriptionHeight - 6;
        Widgets.NewText(parent, "Behavior", 14, behaviorTitleY, "SpellEditorText", Colors.Text);
        Widgets.NewWrappedText(parent, scopeDescription, 14, scopeDescriptionY, textWidth, "SpellEditorMuted", Colors.Muted);
        Widgets.NewWrappedText(parent, overflowDescription, 14, overflowDescriptionY, textWidth, "SpellEditorMuted", Colors.Muted);
        local referencedSpellID = DataLookup.GetReferencedAuraSpellID();
        if referencedSpellID then
            local overflowDescriptionHeight = Widgets.CalculateWrappedTextHeight(overflowDescription, "SpellEditorMuted", textWidth);
            Widgets.NewButton(parent, `Open Spell {referencedSpellID}`, 14,
                overflowDescriptionY - overflowDescriptionHeight - 18, fieldWidth, 32, function()
                Actions.OpenSpell(referencedSpellID);
            end, false);
        end
        return;
    end

    local function AddResetButton(field, isOverridden, fieldY)
        if not isOverridden then
            return 66;
        end
        Widgets.NewButton(parent, "Use Default", 14 + fieldWidth - 92, fieldY - 54, 92, 18, function()
            if SpellEditor.ResetConstraintField(constraint.id, field) then
                Actions.RefreshAll();
            end
        end, false);
        return 86;
    end

    local fieldY = -104;
    Widgets.NewStackedDropdownField(parent,
        scopeOverride and "Scope (Custom)" or "Scope (Inherited)",
        constraint.scope, catalog.constraintScopes, 14, fieldY, fieldWidth, function(value)
        SpellEditor.SetConstraintField(constraint.id, "scope", value);
    end);
    fieldY -= AddResetButton("scope", scopeOverride, fieldY);
    Widgets.NewStackedFieldInput(parent,
        maximumOverride and "Maximum Applications (Custom)" or "Maximum Applications (Inherited)",
        constraint.maximumApplications, 14, fieldY, fieldWidth, function(value)
        Widgets.CommitNumber(value, function(number)
            SpellEditor.SetConstraintField(constraint.id, "maximumApplications", DataLookup.ToInteger(number))
        end);
    end);
    fieldY -= AddResetButton("maximumApplications", maximumOverride, fieldY);
    Widgets.NewStackedDropdownField(parent,
        overflowOverride and "Overflow Behavior (Custom)" or "Overflow Behavior (Inherited)",
        constraint.overflowBehavior, catalog.constraintOverflowBehaviors, 14, fieldY, fieldWidth, function(value)
        SpellEditor.SetConstraintField(constraint.id, "overflowBehavior", value);
    end);
    fieldY -= AddResetButton("overflowBehavior", overflowOverride, fieldY);
    local behaviorTitleY = fieldY - 12;
    local scopeDescriptionY = behaviorTitleY - 24;
    local scopeDescriptionHeight = Widgets.CalculateWrappedTextHeight(scopeDescription, "SpellEditorMuted", textWidth);
    local overflowDescriptionY = scopeDescriptionY - scopeDescriptionHeight - 6;
    Widgets.NewText(parent, "Behavior", 14, behaviorTitleY, "SpellEditorText", Colors.Text);
    Widgets.NewWrappedText(parent, scopeDescription, 14, scopeDescriptionY, textWidth, "SpellEditorMuted", Colors.Muted);
    Widgets.NewWrappedText(parent, overflowDescription, 14, overflowDescriptionY, textWidth, "SpellEditorMuted", Colors.Muted);
end

function ViewBuilders.ProcDataInspector(parent, procData)
    procData = Actions.GetEffectiveProcData(procData);
    local contentWidth = parent:GetWidth();
    local fieldWidth = contentWidth - 28;
    local textWidth = contentWidth - 28;
    local canMutate = (draft.state == "Clean" or draft.state == "Dirty") and not draft.isCreate;
    local visibility = procData.ownerSpellID == 0i and "Shared" or "Private";

    Widgets.NewText(parent, "ProcData Inspector", 14, -14, "SpellEditorTitle", Colors.Text);
    Widgets.NewText(parent, `{visibility} ProcData | ID {procData.id}`, 14, -46, "SpellEditorMuted", Colors.Muted);
    Widgets.NewText(parent, "Definition", 14, -76, "SpellEditorText", Colors.Text);
    Widgets.NewStackedFieldInput(parent, "Debug Name", procData.name, 14, -104, fieldWidth, function(value)
        if value ~= procData.name then
            Actions.UpdateProcData(procData, "name", value);
        end
    end, canMutate);

    local function BuildMaskOptions(options)
        local result = {};
        local specificOptionCount = 0;
        for _, option in options do
            if option.value ~= 0i and option.name ~= "All" then
                specificOptionCount += 1;
            end
        end
        for _, option in options do
            local show = option.value ~= 0i and (option.name ~= "All" or specificOptionCount == 0);
            if show then
                table.insert(result, option);
            end
        end
        return result;
    end

    local y = -176;
    Widgets.NewText(parent, "Trigger Conditions", 14, y, "SpellEditorText", Colors.Text);
    y -= 30;
    local function AddMask(title, field, currentValue, options)
        local usedHeight = Widgets.NewStackedFlagsField(
            parent, title, currentValue, BuildMaskOptions(options), 14, y, fieldWidth, function(value)
            return Actions.UpdateProcData(procData, field, value);
        end, canMutate, false);
        y -= usedHeight + 10;
    end
    AddMask("Phase Mask", "phaseMask", procData.phaseMask, catalog.procPhaseMask);
    AddMask("Type Mask", "typeMask", procData.typeMask, catalog.procTypeMask);
    AddMask("Hit Mask", "hitMask", procData.hitMask, catalog.procHitMask);
    AddMask("Flags", "flags", procData.flags, catalog.procFlags);

    Widgets.NewText(parent, "Proc Behavior", 14, y, "SpellEditorText", Colors.Text);
    y -= 30;
    local procMode = if procData.procsPerMinute ~= 0 then 0
        elseif procData.chanceToProc ~= 0 then 1
        else 0;
    local procModeOptions = {
        { name = "PPM", value = 0 },
        { name = "Proc Chance", value = 1 },
    };
    Widgets.NewStackedDropdownField(parent, "Proc Mode", procMode, procModeOptions, 14, y, fieldWidth, function(value)
        if value == procMode then
            return true;
        end
        local changes = if value == 0
            then {
                procsPerMinute = procData.procsPerMinute ~= 0 and procData.procsPerMinute or 1,
                chanceToProc = 0,
            }
            else {
                procsPerMinute = 0,
                chanceToProc = procData.chanceToProc ~= 0 and procData.chanceToProc or 1,
            };
        return Actions.UpdateProcDataFields(procData, changes);
    end, canMutate, false);
    y -= 58;
    local modeField = if procMode == 0
        then { "Procs Per Minute", "procsPerMinute", procData.procsPerMinute }
        else { "Proc Chance (0-1)", "chanceToProc", procData.chanceToProc };
    Widgets.NewStackedFieldInput(parent, modeField[1], modeField[3], 14, y, fieldWidth, function(value)
        Widgets.CommitNumber(value, function(number)
            return Actions.UpdateProcData(procData, modeField[2], number);
        end);
    end, canMutate);
    y -= 58;
    local fields = {
        { "Internal Cooldown (ms)", "internalCooldownMS", procData.internalCooldownMS },
        { "Charges (-1 = infinite)", "charges", procData.charges },
    };
    for _, field in fields do
        Widgets.NewStackedFieldInput(parent, field[1], field[3], 14, y, fieldWidth, function(value)
            Widgets.CommitNumber(value, function(number)
                return Actions.UpdateProcData(procData, field[2], DataLookup.ToInteger(number));
            end);
        end, canMutate);
        y -= 58;
    end

    Widgets.NewText(parent, "Sharing", 14, y, "SpellEditorText", Colors.Text);
    y -= 26;
    if procData.ownerSpellID ~= 0i then
        local sharingDescription =
            "Private ProcData is only visible to this spell. Promotion makes it reusable by every spell and cannot be reversed.";
        Widgets.NewWrappedText(parent, sharingDescription, 14, y, textWidth, "SpellEditorMuted", Colors.Muted);
        local descriptionHeight = Widgets.CalculateWrappedTextHeight(
            sharingDescription, "SpellEditorMuted", textWidth);
        y -= descriptionHeight + 12;
        local promote = Widgets.NewButton(parent, "Promote to Shared", 14, y, fieldWidth, 28, function()
            if procData.name ~= "" and Actions.UpdateProcData(procData, "ownerSpellID", 0i) then
                Actions.RefreshAll();
            end
        end, false);
        promote:SetVisualEnabled(canMutate and procData.name ~= "");
        promote:SetInteractable(canMutate and procData.name ~= "");
    else
        local sharingDescription =
            "Shared ProcData is reusable by every spell. Duplicate it to create a definition owned by this spell.";
        Widgets.NewWrappedText(parent, sharingDescription, 14, y, textWidth, "SpellEditorMuted", Colors.Muted);
        local descriptionHeight = Widgets.CalculateWrappedTextHeight(
            sharingDescription, "SpellEditorMuted", textWidth);
        y -= descriptionHeight + 12;
        local duplicate = Widgets.NewButton(parent, "Duplicate as Private", 14, y, fieldWidth, 28, function()
            local id = SpellEditor.CreateProcData(
                draft.spellID,
                "",
                procData.phaseMask,
                procData.typeMask,
                procData.hitMask,
                procData.flags,
                procData.procsPerMinute,
                procData.chanceToProc,
                procData.internalCooldownMS,
                procData.charges);
            if id ~= 0i then
                selectedProcDataID = id;
                Actions.RefreshAll();
            end
        end, false);
        duplicate:SetVisualEnabled(canMutate);
        duplicate:SetInteractable(canMutate);
    end
end

function ViewBuilders.ProcLinkInspector(parent, procLink)
    local contentWidth = parent:GetWidth();
    local fieldWidth = contentWidth - 28;
    local textWidth = contentWidth - 28;
    Widgets.NewText(parent, "ProcLink Inspector", 14, -14, "SpellEditorTitle", Colors.Text);
    Widgets.NewText(parent, `ProcLink ID {procLink.id}`, 14, -46, "SpellEditorMuted", Colors.Muted);
    Widgets.NewText(parent, "Definition", 14, -76, "SpellEditorText", Colors.Text);
    local procData = DataLookup.FindProcData(procLink.procDataID);
    Widgets.NewText(parent, "ProcData Definition", 14, -104, "SpellEditorMuted", Colors.Text);
    local procDataName = procData and DataLookup.GetProcDataName(procData)
        or `Missing ProcData {procLink.procDataID}`;
    local openProcData = Widgets.NewButton(parent, procDataName, 14, -124, fieldWidth, Layout.ControlHeight, function()
        if procData then
            selectedProcDataID = procData.id;
            selectedProcLinkID = nil;
            Actions.RefreshWorkspace();
        end
    end, false);
    openProcData:SetVisualEnabled(procData ~= nil);
    openProcData:SetInteractable(procData ~= nil);

    Widgets.NewText(parent, "Effects Triggered", 14, -184, "SpellEditorText", Colors.Text);
    local executionDescription =
        "Choose the effect rows this link executes when its ProcData triggers.";
    Widgets.NewWrappedText(parent, executionDescription, 14, -208, textWidth, "SpellEditorMuted", Colors.Muted);
    local executionHeight = Widgets.CalculateWrappedTextHeight(
        executionDescription, "SpellEditorMuted", textWidth);
    local y = -208 - executionHeight - 12;
    if #draft.effects == 0 then
        Widgets.NewWrappedText(parent, "This spell has no effect rows to select.", 14, y,
            textWidth, "SpellEditorMuted", Colors.Muted);
        return;
    end
    for index, effect in draft.effects do
        local selected = false;
        for _, selectedID in procLink.selectedEffectIDs do
            selected = selected or selectedID == effect.id;
        end
        local checkbox = Components.Checkbox.New(parent, vec.New(16, y), vec.New(18, 18), 2i, {
            backgroundTemplate = "SpellEditorCheckbox",
            fillTemplate = "SpellEditorCheckboxFill",
            fillInset = 4,
            isChecked = selected,
        });
        checkbox:SetAnchor(vec.New(0.0, 1.0));
        checkbox:SetRelativePoint(vec.New(0.0, 1.0));
        checkbox.root:SetAnchor(vec.New(0.0, 1.0));
        checkbox.root:SetRelativePoint(vec.New(0.0, 1.0));
        checkbox.root:SetBorderColor(Colors.Border);
        checkbox.root:SetBorderSize(1);
        Widgets.NewSingleLineText(
            parent, `Effect {index}: {effect.typeName}`, 44, y, fieldWidth - 30, "SpellEditorText", Colors.Text);
        checkbox:SetOnValueChanged(function(_, value)
            if not SpellEditor.SetProcLinkEffect(procLink.id, effect.id, value) then
                return false;
            end
            Actions.RefreshAll();
            return true;
        end);
        y -= 30;
    end
end

function Actions.RefreshInspector()
    Widgets.DestroyContent(inspectorContent);
    local activeTab = tabSelection:Get() or "Overview";
    local selectedEffect = nil;
    if activeTab == "Effects" and selectedEffectID and draft then
        for _, effect in draft.effects do
            if effect.id == selectedEffectID then
                selectedEffect = effect;
                break;
            end
        end
    end
    local inspectorContentHeight = activeTab == "Procs" and 1440 or Layout.InspectorViewportHeight;
    if selectedEffect then
        local effectDiagnostics = DataLookup.GetEffectDiagnostics(selectedEffect.id);
        local descriptor = DataLookup.FindCatalogEffect(selectedEffect.type);
        inspectorContentHeight = Widgets.CalculateEffectInspectorHeight(descriptor, effectDiagnostics, Layout.InspectorViewportWidth - 28);
        if inspectorContentHeight > Layout.InspectorViewportHeight then
            inspectorContentHeight = Widgets.CalculateEffectInspectorHeight(descriptor, effectDiagnostics, Layout.InspectorViewportWidth - Layout.InspectorScrollBarWidth - 28);
        end
    end
    inspectorScroll:SetContentHeight(inspectorContentHeight);
    local inspectorContentWidth = Layout.InspectorViewportWidth -
        (if inspectorContentHeight > Layout.InspectorViewportHeight then Layout.InspectorScrollBarWidth else 0);
    inspectorContent = inspectorScroll.content:NewPanel(vec.New(0, 0), vec.New(inspectorContentWidth, inspectorContentHeight), 2i, "DebugDarkGrey");
    inspectorContent:SetAnchor(vec.New(0.0, 1.0));
    inspectorContent:SetRelativePoint(vec.New(0.0, 1.0));
    inspectorContent:SetAlpha(0.0);
    if not draft then
        Widgets.NewText(inspectorContent, "Inspector", 14, -14, "SpellEditorTitle", Colors.Text);
        Widgets.NewText(inspectorContent, "Select a spell to inspect its definition.", 14, -54, "SpellEditorMuted", Colors.Muted);
        return;
    end
    if selectedEffect then
        ViewBuilders.EffectInspector(inspectorContent, selectedEffect);
        return;
    elseif activeTab == "Constraints" and selectedConstraintID then
        if selectedConstraintReadOnly then
            for _, constraint in snapshot.constraints do
                if constraint.id == selectedConstraintID then
                    ViewBuilders.ConstraintInspector(inspectorContent, constraint, true);
                    return;
                end
            end
        else
            for _, constraint in draft.constraints do
                if constraint.id == selectedConstraintID then
                    ViewBuilders.ConstraintInspector(inspectorContent, constraint, false);
                    return;
                end
            end
        end
    elseif activeTab == "Procs" and selectedProcDataID then
        local procData = DataLookup.FindProcData(selectedProcDataID);
        if procData and DataLookup.IsProcDataVisible(procData) then
            ViewBuilders.ProcDataInspector(inspectorContent, procData);
            return;
        end
    elseif activeTab == "Procs" and selectedProcLinkID then
        for _, procLink in draft.procLinks do
            if procLink.id == selectedProcLinkID then
                ViewBuilders.ProcLinkInspector(inspectorContent, procLink);
                return;
            end
        end
    end
    Widgets.NewText(inspectorContent, `{activeTab} Inspector`, 14, -14, "SpellEditorTitle", Colors.Text);
    if activeTab == "Targeting" then
        Widgets.NewWrappedText(inspectorContent, "The spell resolves one authoritative target before its target-requiring effects execute.", 14, -54, Layout.InspectorWidth - 54, "SpellEditorMuted", Colors.Muted);

        local separator = Widgets.NewPanel(inspectorContent, 14, -108, Layout.InspectorWidth - 44, 1, Colors.Border, 0.8);
        separator:SetBorderSize(0);
        Widgets.NewText(inspectorContent, "Authored Targeting", 14, -124, "SpellEditorText", Colors.Text);

        local selector = DataLookup.FindOption(catalog.targetSelectors, draft.targetSelector);
        local relation = DataLookup.FindOption(catalog.targetRelations, draft.targetRelation);
        local rangePolicy = DataLookup.FindOption(catalog.rangePolicies, draft.rangePolicy);
        local rangeValue = rangePolicy and rangePolicy.name == "None" and "No range check"
            or rangePolicy and rangePolicy.name == "Distance" and `{draft.minimumRange} - {draft.maximumRange}`
            or (rangePolicy and rangePolicy.name or "Unknown");
        Widgets.NewText(inspectorContent, "Selector", 14, -158, "SpellEditorMuted", Colors.Muted);
        Widgets.NewSingleLineText(inspectorContent, selector and selector.name or "Unknown", 126, -158, Layout.InspectorWidth - 154, "SpellEditorMuted", Colors.Text);
        Widgets.NewText(inspectorContent, "Relation", 14, -184, "SpellEditorMuted", Colors.Muted);
        Widgets.NewSingleLineText(inspectorContent, relation and relation.name or "Unknown", 126, -184, Layout.InspectorWidth - 154, "SpellEditorMuted", Colors.Text);
        Widgets.NewText(inspectorContent, "Range Policy", 14, -210, "SpellEditorMuted", Colors.Muted);
        Widgets.NewSingleLineText(inspectorContent, rangePolicy and rangePolicy.name or "Unknown", 126, -210, Layout.InspectorWidth - 154, "SpellEditorMuted", Colors.Text);
        Widgets.NewText(inspectorContent, "Resolved Range", 14, -236, "SpellEditorMuted", Colors.Muted);
        Widgets.NewSingleLineText(inspectorContent, rangeValue, 126, -236, Layout.InspectorWidth - 154, "SpellEditorMuted", Colors.Text);

        local effectCount = 0;
        for _, effect in draft.effects do
            if effect.targetMode == "Required" then
                effectCount += 1;
            end
        end
        local contractSeparator = Widgets.NewPanel(inspectorContent, 14, -268, Layout.InspectorWidth - 44, 1, Colors.Border, 0.8);
        contractSeparator:SetBorderSize(0);
        Widgets.NewText(inspectorContent, "Effect Contract", 14, -284, "SpellEditorText", Colors.Text);
        Widgets.NewWrappedText(inspectorContent, `{effectCount} of {#draft.effects} effects consume this target. Effects do not select targets independently.`, 14, -316, Layout.InspectorWidth - 54, "SpellEditorMuted", Colors.Muted);
    elseif activeTab == "Aura" then
        if draft.aura then
            Widgets.NewWrappedText(inspectorContent,
                "This spell owns an aura definition. Casting it directly applies this aura using the spell-resolved target.",
                14, -56, Layout.InspectorWidth - 54, "SpellEditorMuted", Colors.Muted);
            local auraSeparator = Widgets.NewPanel(inspectorContent, 14, -122, Layout.InspectorWidth - 44, 1, Colors.Border, 0.8);
            auraSeparator:SetBorderSize(0);
            Widgets.NewText(inspectorContent, "Field Help", 14, -132, "SpellEditorText", Colors.Text);
            Widgets.NewWrappedText(inspectorContent,
                "Duration controls how long the aura remains.\n\nStacks per application is the amount added each time. Maximum stacks is the cap on the target.\n\nApplication policy determines whether applications are shared or tracked per caster.",
                14, -164, Layout.InspectorWidth - 54, "SpellEditorMuted", Colors.Muted);
            local lifecycleSeparator = Widgets.NewPanel(inspectorContent, 14, -338, Layout.InspectorWidth - 44, 1, Colors.Border, 0.8);
            lifecycleSeparator:SetBorderSize(0);
            Widgets.NewText(inspectorContent, "Lifecycle", 14, -356, "SpellEditorText", Colors.Text);
            Widgets.NewWrappedText(inspectorContent,
                "Target Death Behavior determines whether the aura is removed when its target dies or persists through death.\n\nThe caster options can remove the aura when its caster dies and/or leaves the current world.\n\nActive auras are always removed when their target changes worlds.",
                14, -388, Layout.InspectorWidth - 54, "SpellEditorMuted", Colors.Muted);
        else
            Widgets.NewWrappedText(inspectorContent,
                "This spell does not own an aura definition. Add one only when directly casting this spell should apply the spell as an aura.\n\nA normal spell can instead apply a separate aura spell through an ApplyAura effect.",
                14, -56, Layout.InspectorWidth - 54, "SpellEditorMuted", Colors.Muted);
        end
    elseif activeTab == "Constraints" then
        local referencedSpellID = DataLookup.GetReferencedAuraSpellID();
        local message = if draft.aura
            then "This spell owns these constraints. Select a row to edit it."
            elseif referencedSpellID
            then `These constraints belong to referenced Spell {referencedSpellID} and are read-only here.`
            else "Add an aura definition before authoring constraints.";
        Widgets.NewWrappedText(inspectorContent, message, 14, -56, Layout.InspectorWidth - 54, "SpellEditorMuted", Colors.Muted);
    elseif activeTab == "Procs" then
        Widgets.NewWrappedText(inspectorContent,
            "ProcLinks belong to this spell. Private ProcData is visible only to its owner; shared ProcData is intentionally reusable across spells.",
            14, -56, Layout.InspectorWidth - 54, "SpellEditorMuted", Colors.Muted);
    else
        Widgets.NewWrappedText(inspectorContent, "Select a row for field-level metadata and controls.", 14, -56, Layout.InspectorWidth - 54, "SpellEditorMuted", Colors.Muted);
    end
end

function Actions.RefreshSummary()
    Widgets.DestroyContent(summaryContent);
    summaryContent = summaryPanel:NewPanel(vec.New(0, 0), vec.New(Layout.BodyWidth + Layout.InspectorWidth, Layout.SummaryHeight), 2i, "DebugDarkGrey");
    summaryContent:SetAlpha(0.0);
    Widgets.NewText(summaryContent, "Summary (Related Definitions)", 12, -8, "SpellEditorText", Colors.Text);
    if not draft then
        Widgets.NewText(summaryContent, "No related definition is selected.", 14, -48, "SpellEditorMuted", Colors.Muted);
        return;
    end

    local referencedAuraID = DataLookup.GetReferencedAuraSpellID();
    local referencedAura = referencedAuraID and DataLookup.FindSpell(referencedAuraID) or nil;
    local summaryWidth = Layout.BodyWidth + Layout.InspectorWidth;
    local cardGap = 10;
    local cardWidth = math.floor((summaryWidth - 24 - cardGap * 2) / 3);
    local cardY = -30;
    local cardHeight = 58;

    local auraCard = Widgets.NewPanel(summaryContent, 12, cardY, cardWidth, cardHeight, Colors.Panel, 1.0);
    Widgets.NewText(auraCard, draft.aura and "Owned Aura Definition" or "Referenced Aura", 12, -10, "SpellEditorText", Colors.Text);
    if draft.aura then
        Widgets.NewSingleLineText(auraCard, `Spell {draft.spellID} owns its aura definition.`, 12, -34, cardWidth - 24, "SpellEditorMuted", Colors.Muted);
    elseif referencedAura then
        Widgets.NewSingleLineText(auraCard, `{referencedAura.name} (Spell {referencedAura.id})`, 12, -34, cardWidth - 108, "SpellEditorMuted", Colors.Text);
        Widgets.NewButton(auraCard, "Open", cardWidth - 84, -28, 72, 24, function() Actions.OpenSpell(referencedAura.id) end, false);
    elseif referencedAuraID then
        Widgets.NewSingleLineText(auraCard, `Missing Spell {referencedAuraID}`, 12, -34, cardWidth - 24, "SpellEditorMuted", Colors.Error);
    else
        Widgets.NewSingleLineText(auraCard, "This spell does not own or apply an aura.", 12, -34, cardWidth - 24, "SpellEditorMuted", Colors.Muted);
    end

    local diagnosticsX = 12 + cardWidth + cardGap;
    local diagnosticsCard = Widgets.NewPanel(summaryContent, diagnosticsX, cardY, cardWidth, cardHeight, Colors.Panel, 1.0);
    local diagnosticsTitle = #diagnostics == 0 and "Diagnostics" or `Diagnostics ({#diagnostics} errors)`;
    Widgets.NewText(diagnosticsCard, diagnosticsTitle, 12, -10, "SpellEditorText", Colors.Text);
    local serverFeedback = draft.serverDiagnostic ~= "" and draft.serverDiagnostic or successMessage;
    local diagnosticsSummary = serverFeedback ~= "" and serverFeedback
        or #diagnostics == 0 and "Draft is locally valid."
        or "Synchronization is blocked until the errors are resolved.";
    local diagnosticsColor = draft.serverDiagnostic ~= "" and (draft.state == "MutationFailed" and Colors.Error or Colors.Muted)
        or successMessage ~= "" and Colors.Success
        or #diagnostics == 0 and Colors.Success
        or Colors.Error;
    Widgets.NewSingleLineText(diagnosticsCard, diagnosticsSummary, 12, -34,
        #diagnostics == 0 and cardWidth - 24 or cardWidth - 132, "SpellEditorMuted", diagnosticsColor);
    if #diagnostics > 0 then
        Widgets.NewButton(diagnosticsCard, "View Errors", cardWidth - 108, -28, 96, 24, Dialogs.OpenDiagnostics, false).text:SetColor(Colors.Error);
    end

    local referenceCount = 0;
    local firstRelatedSpellID = nil;
    for _, reference in snapshot.auraReferences do
        if reference.targetSpellID == draft.spellID or reference.sourceSpellID == draft.spellID then
            referenceCount += 1;
            firstRelatedSpellID = firstRelatedSpellID or (reference.sourceSpellID == draft.spellID and reference.targetSpellID or reference.sourceSpellID);
        end
    end
    local referencesX = diagnosticsX + cardWidth + cardGap;
    local referencesCard = Widgets.NewPanel(summaryContent, referencesX, cardY, cardWidth, cardHeight, Colors.Panel, 1.0);
    Widgets.NewText(referencesCard, "References", 12, -10, "SpellEditorText", Colors.Text);
    local referenceSummary = if referenceCount == 1 then "1 ApplyAura relationship" else `{referenceCount} ApplyAura relationships`;
    Widgets.NewSingleLineText(referencesCard, referenceSummary, 12, -34, firstRelatedSpellID and cardWidth - 128 or cardWidth - 24, "SpellEditorMuted", Colors.Muted);
    if firstRelatedSpellID then
        Widgets.NewButton(referencesCard, `Open {firstRelatedSpellID}`, cardWidth - 104, -28, 92, 24, function() Actions.OpenSpell(firstRelatedSpellID) end, false);
    end
end

Actions.RefreshWorkspace = function()
    Widgets.DestroyContent(bodyContent);
    local activeTab = tabSelection:Get() or "Overview";
    bodyContent = bodyScroll.content:NewPanel(vec.New(0, 0), vec.New(Layout.BodyWidth - Layout.Padding * 2, bodyScroll.content:GetHeight()), 1i, "DebugDarkGrey");
    bodyContent:SetAnchor(vec.New(0.0, 1.0));
    bodyContent:SetRelativePoint(vec.New(0.0, 1.0));
    bodyContent:SetAlpha(0.0);

    if not draft then
        local state = snapshot and snapshot.dataState or "Unavailable";
        Widgets.NewText(bodyContent, state == "Loading" and "Loading authoritative spell data..." or "Select a spell to begin authoring.", 24, -28, "SpellEditorTitle", Colors.Text);
        if state == "Unavailable" or state == "Failed" then
            Widgets.NewButton(bodyContent, "Retry", 24, -78, 100, 30, function()
                SpellEditor.RequestSnapshot();
                Actions.RefreshAll();
            end, true);
        end
        bodyScroll:SetContentHeight(Layout.BodyHeight - Layout.Padding * 2);
        Actions.RefreshInspector();
        return;
    end

    local height = Layout.BodyHeight - Layout.Padding * 2;
    if activeTab == "Overview" then
        height = ViewBuilders.Overview(bodyContent);
    elseif activeTab == "Targeting" then
        height = ViewBuilders.Targeting(bodyContent);
    elseif activeTab == "Effects" then
        height = ViewBuilders.Effects(bodyContent);
    elseif activeTab == "Aura" then
        height = ViewBuilders.Aura(bodyContent);
    elseif activeTab == "Constraints" then
        height = ViewBuilders.Constraints(bodyContent);
    elseif activeTab == "Procs" then
        height = ViewBuilders.Procs(bodyContent);
    end
    bodyScroll:SetContentHeight(math.max(Layout.BodyHeight - Layout.Padding * 2, height));
    Actions.RefreshInspector();
end

for index, tabName in TABS do
    local width = Layout.BodyWidth / #TABS;
    local button = Widgets.NewButton(tabsPanel, tabName, (index - 1) * width, 0, width, Layout.TabHeight, nil, false);
    button:SetOnMouseUp(function()
        Actions.SetActiveTab(tabName);
    end);
    tabButtons[tabName] = button;
end

Actions.SetActiveTab = function(tabName, refreshWorkspace)
    if tabButtons[tabName] == nil then
        return false;
    end
    if not tabSelection:Set(tabName, false) then
        return false;
    end
    for name, tabButton in tabButtons do
        tabButton:SetHighlighted(name == tabName);
    end
    if refreshWorkspace ~= false then
        bodyScroll:ScrollToStart();
        Actions.RefreshWorkspace();
    end
    return true;
end

Actions.RefreshAll = function()
    snapshot = SpellEditor.GetSnapshot();
    draft = SpellEditor.GetDraft();
    diagnostics = draft and SpellEditor.Validate() or {};
    if snapshot and snapshot.mutationSucceeded then
        successMessage = "Server accepted the mutation; runtime cache updated.";
    elseif draft and draft.state == "Dirty" then
        successMessage = "";
    end
    if draft then
        selectedSpellID = draft.spellID;
    end
    Actions.SetActiveTab(tabSelection:Get() or "Overview", false);
    Actions.RefreshHeader();
    Actions.RefreshBrowser(true);
    Actions.RefreshWorkspace();
    Actions.RefreshSummary();
    local diagnosticText = draft and draft.serverDiagnostic or "";
    lastStatusKey = `{snapshot and snapshot.dataState or "Unavailable"}|{draft and draft.state or "None"}|{diagnosticText}`;
end

searchInput:SetOnTextChanged(function()
    Actions.RefreshBrowser();
end);
createButton:SetOnMouseUp(function()
    local function Create()
        if SpellEditor.CreateDraft() then
            draft = SpellEditor.GetDraft();
            selectedSpellID = draft and draft.spellID or nil;
            Actions.SetActiveTab("Overview", false);
            Actions.RefreshAll();
        end
    end
    if Actions.IsDirty() then
        Dialogs.Confirm("Discard the current unsynchronized draft and create a spell?", Create);
    else
        Create();
    end
end);
duplicateButton:SetOnMouseUp(function()
    if not draft then
        return;
    end
    local function Duplicate()
        if SpellEditor.DuplicateDraft(draft.spellID) then
            Actions.RefreshAll();
        end
    end
    if Actions.IsDirty() then
        Dialogs.Confirm("Discard local changes and duplicate the authoritative spell?", Duplicate);
    else
        Duplicate();
    end
end);

local methods = {};

function methods:Show()
    window:SetVisible(true);
    catalog = catalog or SpellEditor.GetCatalog();
    snapshot = SpellEditor.GetSnapshot();
    if snapshot.dataState == "Unavailable" or snapshot.dataState == "Failed" then
        SpellEditor.RequestSnapshot();
    end
    Actions.RefreshAll();
end

function methods:Hide()
    if Actions.IsDirty() then
        Dialogs.Confirm("Close the Spell Editor and discard the local draft?", function()
            SpellEditor.DiscardDraft();
            Dialogs.CloseIconCatalog();
            window:SetVisible(false);
        end);
    else
        Dialogs.CloseIconCatalog();
        window:SetVisible(false);
    end
end

function methods:IsVisible()
    return window:IsVisible();
end

closeButton:SetOnMouseUp(function()
    methods:Hide();
end);

local lastPoll = 0.0;
RegisterEvent(GameEvent.Updated, function()
    if not window:IsVisible() then
        return;
    end
    local now = os.clock();
    if now - lastPoll < 0.20 then
        return;
    end
    lastPoll = now;
    local currentSnapshot = SpellEditor.GetSnapshot();
    local currentDraft = SpellEditor.GetDraft();
    if currentSnapshot and currentSnapshot.mutationSucceeded then
        successMessage = "Server accepted the mutation; runtime cache updated.";
    end
    local diagnosticText = currentDraft and currentDraft.serverDiagnostic or "";
    local statusKey = `{currentSnapshot and currentSnapshot.dataState or "Unavailable"}|{currentDraft and currentDraft.state or "None"}|{diagnosticText}`;
    if statusKey ~= lastStatusKey then
        Actions.RefreshAll();
    end
end);

Components.EditorRegistry.Register("SpellEditor", methods);
