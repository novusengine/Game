--[[
CVarEditor - searchable, category-aware editor for the engine CVar registry.

The window intentionally uses text labels in places where the current UI system does not yet
support the iconography in the visual reference. Rows are virtualized so the complete registry can
be browsed without creating one widget tree per CVar.
]]

local Button = require("@src/API/UI/Button")
local EditorRegistry = require("@src/API/Editor/Registry")
local InputBox = require("@src/API/UI/InputBox")
local VirtualList = require("@src/API/UI/VirtualList")
local ContextMenu = require("@src/API/UI/ContextMenu")
local EditorWindow = require("@src/API/Editor/UI/Window")
local TextUtil = require("@src/API/UI/TextUtil")

require("@src/API/Editor/UI/MenuBar")

local WINDOW_W = 1050
local WINDOW_H = 620
local TITLE_H = 24
local TOOLBAR_H = 42
local HEADER_H = 24
local FOOTER_H = 28
local CATEGORY_W = 164
local DETAILS_W = 286
local LIST_W = WINDOW_W - CATEGORY_W - DETAILS_W
local BODY_H = WINDOW_H - TITLE_H - TOOLBAR_H - HEADER_H - FOOTER_H
local ROW_H = 25
local LIST_BODY_H = BODY_H - (BODY_H % ROW_H)
local PAD = 8
local LAYER = 1100i
local TEXT_TEMPLATE = "DevToolsText"

local COLOR_WINDOW = vec.New(0.075, 0.085, 0.10)
local COLOR_TITLE = vec.New(0.045, 0.052, 0.064)
local COLOR_PANEL = vec.New(0.095, 0.105, 0.12)
local COLOR_BORDER = vec.New(0.22, 0.25, 0.30)
local COLOR_TEXT = vec.New(0.84, 0.86, 0.90)
local COLOR_MUTED = vec.New(0.58, 0.61, 0.67)
local COLOR_ACCENT = vec.New(0.18, 0.42, 0.78)
local COLOR_MODIFIED = vec.New(1.0, 0.48, 0.08)

local FLAG_HIDDEN = 2i
local FLAG_READ_ONLY = 4i
local FLAG_ADVANCED = 8i

local categories = {
    { name = "All Categories", mask = nil },
    { name = "Server", mask = 1i },
    { name = "Client", mask = 2i },
    { name = "Rendering", mask = 4i },
    { name = "Physics", mask = 8i },
    { name = "Network", mask = 16i },
    { name = "Database", mask = 32i },
}

local types = { "All", "Boolean", "Integer", "Float", "String", "Float Vector", "Integer Vector" }

local function HasFlag(value, flag)
    return integer.btest(value, flag)
end

local function ValuesEqual(a, b)
    if type(a) == "table" and type(b) == "table" then
        return a.x == b.x and a.y == b.y and a.z == b.z and a.w == b.w
    end
    return a == b
end

local function FormatValue(value, typeName)
    if typeName == "Boolean" then
        return value and "Enabled" or "Disabled"
    elseif typeName == "Float" then
        return string.format("%.4g", value)
    elseif typeName == "String" and value == "" then
        return '""'
    elseif typeName == "Float Vector" then
        return string.format("%.3g, %.3g, %.3g, %.3g", value.x, value.y, value.z, value.w)
    elseif typeName == "Integer Vector" then
        return string.format("%d, %d, %d, %d", value.x, value.y, value.z, value.w)
    end
    return tostring(value)
end

local function FormatEditorValue(value, typeName)
    if typeName == "String" then return value end
    return FormatValue(value, typeName)
end

local function ParseValue(text, typeName)
    if typeName == "Integer" then
        local value = tonumber(text)
        return value and math.floor(value) or nil
    elseif typeName == "Float" then
        return tonumber(text)
    elseif typeName == "String" then
        return text
    elseif typeName == "Float Vector" or typeName == "Integer Vector" then
        local values = {}
        for component in string.gmatch(text, "[-+]?[%d%.]+") do
            table.insert(values, tonumber(component))
        end
        if #values ~= 4 then return nil end
        if typeName == "Integer Vector" then
            for i = 1, 4 do values[i] = math.floor(values[i]) end
        end
        return { x = values[1], y = values[2], z = values[3], w = values[4] }
    end
    return nil
end

local function MakeButton(parent, x, y, w, h, text)
    return Button.New(parent, vec.New(x, y), vec.New(w, h), 1i, {
        text = text,
        textTemplate = TEXT_TEMPLATE,
    })
end

local function SetButtonEnabledVisual(button, enabled)
    button:SetVisualEnabled(enabled)
end

local canvas = UI.GetCanvas("CVarEditor", vec.New(0, 0), vec.New(1920, 1080), false)
local editorWindow = EditorWindow.New(canvas, vec.New(430, -120), vec.New(WINDOW_W, WINDOW_H), LAYER, {
    title = "Console Variables",
    titleHeight = TITLE_H,
    padding = PAD,
    windowColor = COLOR_WINDOW,
    titleColor = COLOR_TITLE,
    alpha = 0.98,
    closeSize = vec.New(18, 18),
})
local window = editorWindow.root
local titleBar = editorWindow.titleBar
local title = editorWindow.title
local closeButton = editorWindow.closeButton

local toolbar = window:NewPanel(vec.New(0, -TITLE_H), vec.New(WINDOW_W, TOOLBAR_H), 1i, "DebugDarkGrey")
toolbar:SetAnchor(vec.New(0.0, 1.0))
toolbar:SetRelativePoint(vec.New(0.0, 1.0))
toolbar:SetColor(COLOR_PANEL)
toolbar:SetAlpha(1.0)

local SEARCH_INPUT_H = 24
local searchInput = InputBox.New(toolbar, vec.New(PAD, -9), vec.New(292, SEARCH_INPUT_H), 1i, {
    panelTemplate = "DebugDarkGrey",
    textTemplate = TEXT_TEMPLATE,
    defaultText = "",
    placeholderText = "Search console variables...",
})
searchInput:SetAnchor(vec.New(0.0, 1.0))
searchInput:SetRelativePoint(vec.New(0.0, 1.0))
searchInput:SetTextVerticallyCentered()

local allButton = MakeButton(toolbar, 310, -9, 62, 24, "All")
local modifiedButton = MakeButton(toolbar, 378, -9, 88, 24, "Modified")
local advancedButton = MakeButton(toolbar, 472, -9, 88, 24, "Advanced")
local hiddenButton = MakeButton(toolbar, 566, -9, 74, 24, "Hidden")
local favoritesButton = MakeButton(toolbar, 646, -9, 82, 24, "Favorites")
for _, button in { allButton, modifiedButton, advancedButton, hiddenButton, favoritesButton } do
    button:SetAnchor(vec.New(0.0, 1.0))
    button:SetRelativePoint(vec.New(0.0, 1.0))
end
local TYPE_BUTTON_W = 150
local TYPE_BUTTON_H = 24
local typeButton = MakeButton(toolbar, -PAD, -9, TYPE_BUTTON_W, TYPE_BUTTON_H, "Type: All  v")
typeButton:SetAnchor(vec.New(1.0, 1.0))
typeButton:SetRelativePoint(vec.New(1.0, 1.0))

local categoryPanel = window:NewPanel(vec.New(0, -(TITLE_H + TOOLBAR_H)), vec.New(CATEGORY_W, HEADER_H + BODY_H), 1i, "DebugDarkGrey")
categoryPanel:SetAnchor(vec.New(0.0, 1.0))
categoryPanel:SetRelativePoint(vec.New(0.0, 1.0))
categoryPanel:SetColor(COLOR_PANEL)
categoryPanel:SetAlpha(0.72)
categoryPanel:SetBorderColor(COLOR_BORDER)
categoryPanel:SetBorderSize(1)
local categoryTitle = categoryPanel:NewText("CATEGORIES", vec.New(PAD, -6), 1i, TEXT_TEMPLATE)
categoryTitle:SetAnchor(vec.New(0.0, 1.0))
categoryTitle:SetRelativePoint(vec.New(0.0, 1.0))
categoryTitle:SetColor(COLOR_MUTED)
categoryTitle:SetInteractable(false)

local listHeader = window:NewPanel(vec.New(CATEGORY_W, -(TITLE_H + TOOLBAR_H)), vec.New(LIST_W, HEADER_H), 1i, "DebugDarkGrey")
listHeader:SetAnchor(vec.New(0.0, 1.0))
listHeader:SetRelativePoint(vec.New(0.0, 1.0))
listHeader:SetColor(COLOR_PANEL)
listHeader:SetAlpha(0.9)
local function HeaderText(text, x)
    local widget = listHeader:NewText(text, vec.New(x, 0), 1i, TEXT_TEMPLATE)
    widget:SetAnchor(vec.New(0.0, 0.5))
    widget:SetRelativePoint(vec.New(0.0, 0.5))
    widget:SetColor(COLOR_MUTED)
    widget:SetInteractable(false)
end
HeaderText("Name", 10)
HeaderText("Current Value", 225)
HeaderText("Default Value", 365)
HeaderText("Type", 475)

local detailsPanel = window:NewPanel(vec.New(0, -(TITLE_H + TOOLBAR_H)), vec.New(DETAILS_W, HEADER_H + BODY_H), 1i, "DebugDarkGrey")
detailsPanel:SetAnchor(vec.New(1.0, 1.0))
detailsPanel:SetRelativePoint(vec.New(1.0, 1.0))
detailsPanel:SetClipChildren(true)
detailsPanel:SetColor(COLOR_PANEL)
detailsPanel:SetAlpha(0.78)
detailsPanel:SetBorderColor(COLOR_BORDER)
detailsPanel:SetBorderSize(1)
local detailsTitle = detailsPanel:NewText("DETAILS", vec.New(PAD, -6), 1i, TEXT_TEMPLATE)
detailsTitle:SetAnchor(vec.New(0.0, 1.0))
detailsTitle:SetRelativePoint(vec.New(0.0, 1.0))
detailsTitle:SetColor(COLOR_MUTED)
detailsTitle:SetInteractable(false)

local function NewDetailText(text, x, color)
    local widget = detailsPanel:NewText(text, vec.New(x, 0), 1i, TEXT_TEMPLATE)
    widget:SetAnchor(vec.New(0.0, 1.0))
    widget:SetRelativePoint(vec.New(0.0, 1.0))
    widget:SetColor(color or COLOR_TEXT)
    widget:SetInteractable(false)
    return widget
end

local detailFullNameLabel = NewDetailText("Full CVar name", PAD, COLOR_MUTED)
local detailName = NewDetailText("Select a console variable", PAD, COLOR_TEXT)
local detailDescriptionLabel = NewDetailText("Description", PAD, COLOR_MUTED)
local detailDescription = NewDetailText("", PAD, COLOR_TEXT)

local function NewDetailCard()
    local card = detailsPanel:NewPanel(vec.New(PAD, 0), vec.New(DETAILS_W - PAD * 2, 22), 0i, "DebugDarkGrey")
    card:SetAnchor(vec.New(0.0, 1.0))
    card:SetRelativePoint(vec.New(0.0, 1.0))
    card:SetColor(COLOR_TITLE)
    card:SetAlpha(0.92)
    card:SetBorderColor(COLOR_BORDER)
    card:SetBorderSize(1)
    card:SetInteractable(false)
    return card
end

local detailNameCard = NewDetailCard()
local detailNameAccent = detailNameCard:NewPanel(vec.New(0, 0), vec.New(3, 22), 1i, "DebugDarkGrey")
detailNameAccent:SetAnchor(vec.New(0.0, 1.0))
detailNameAccent:SetRelativePoint(vec.New(0.0, 1.0))
detailNameAccent:SetColor(COLOR_ACCENT)
detailNameAccent:SetAlpha(0.95)
detailNameAccent:SetInteractable(false)
local detailDescriptionCard = NewDetailCard()
detailDescriptionCard:SetAlpha(0.62)

detailName:SetPosX(PAD + 8)
detailName:SetColor(vec.New(0.66, 0.82, 1.0))
detailDescription:SetPosX(PAD + 7)

local DETAIL_VALUE_X = 112
local detailRows = {
    { label = NewDetailText("Current value", PAD, COLOR_MUTED), value = NewDetailText("", DETAIL_VALUE_X, COLOR_TEXT) },
    { label = NewDetailText("Default value", PAD, COLOR_MUTED), value = NewDetailText("", DETAIL_VALUE_X, COLOR_TEXT) },
    { label = NewDetailText("Minimum value", PAD, COLOR_MUTED), value = NewDetailText("", DETAIL_VALUE_X, COLOR_TEXT) },
    { label = NewDetailText("Maximum value", PAD, COLOR_MUTED), value = NewDetailText("", DETAIL_VALUE_X, COLOR_TEXT) },
    { label = NewDetailText("Type", PAD, COLOR_MUTED), value = NewDetailText("", DETAIL_VALUE_X, COLOR_TEXT) },
    { label = NewDetailText("Categories", PAD, COLOR_MUTED), value = NewDetailText("", DETAIL_VALUE_X, COLOR_TEXT) },
    { label = NewDetailText("Flags", PAD, COLOR_MUTED), value = NewDetailText("", DETAIL_VALUE_X, COLOR_TEXT) },
}
local provenanceRows = {
    { label = NewDetailText("Source", PAD, COLOR_MUTED), value = NewDetailText("", DETAIL_VALUE_X, COLOR_TEXT) },
    { label = NewDetailText("Registered by", PAD, COLOR_MUTED), value = NewDetailText("", DETAIL_VALUE_X, COLOR_TEXT) },
    { label = NewDetailText("Last modified", PAD, COLOR_MUTED), value = NewDetailText("", DETAIL_VALUE_X, COLOR_TEXT) },
}
detailRows[3].value:SetColor(COLOR_MUTED)
detailRows[4].value:SetColor(COLOR_MUTED)
for _, row in provenanceRows do row.value:SetColor(COLOR_MUTED) end

local detailSeparatorA = detailsPanel:NewPanel(vec.New(PAD, 0), vec.New(DETAILS_W - PAD * 2, 1), 1i, "DebugDarkGrey")
local detailSeparatorB = detailsPanel:NewPanel(vec.New(PAD, 0), vec.New(DETAILS_W - PAD * 2, 1), 1i, "DebugDarkGrey")
for _, separator in { detailSeparatorA, detailSeparatorB } do
    separator:SetAnchor(vec.New(0.0, 1.0))
    separator:SetRelativePoint(vec.New(0.0, 1.0))
    separator:SetColor(COLOR_BORDER)
    separator:SetAlpha(0.7)
    separator:SetInteractable(false)
end

local resetButton = Button.New(detailsPanel, vec.New(PAD, 46), vec.New(DETAILS_W - PAD * 2, 28), 1i, {
    text = "Reset to Default",
    textTemplate = TEXT_TEMPLATE,
    color = vec.New(0.16, 0.34, 0.64),
    hoverColor = vec.New(0.28, 0.55, 0.95),
    alpha = 0.68,
    hoverAlpha = 0.90,
    disabledAlpha = 0.08,
    borderColor = COLOR_BORDER,
    borderSize = 1,
})
resetButton:SetAnchor(vec.New(0.0, 0.0))
resetButton:SetRelativePoint(vec.New(0.0, 0.0))
local favoriteDetailButton = Button.New(detailsPanel, vec.New(PAD, 12), vec.New(DETAILS_W - PAD * 2, 24), 1i, {
    text = "Add to Favorites",
    textTemplate = TEXT_TEMPLATE,
    color = COLOR_ACCENT,
    hoverColor = vec.New(0.28, 0.55, 0.95),
    alpha = 0.62,
    hoverAlpha = 0.88,
    disabledAlpha = 0.08,
    borderColor = COLOR_BORDER,
    borderSize = 1,
})
favoriteDetailButton:SetAnchor(vec.New(0.0, 0.0))
favoriteDetailButton:SetRelativePoint(vec.New(0.0, 0.0))

local countText = categoryPanel:NewText("0 shown / 0 total", vec.New(0, 10), 1i, TEXT_TEMPLATE)
countText:SetAnchor(vec.New(0.5, 0.0))
countText:SetRelativePoint(vec.New(0.5, 0.0))
countText:SetColor(COLOR_MUTED)
countText:SetInteractable(false)

local footer = window:NewPanel(vec.New(0, 0), vec.New(WINDOW_W, FOOTER_H), 1i, "DebugDarkGrey")
footer:SetAnchor(vec.New(0.0, 0.0))
footer:SetRelativePoint(vec.New(0.0, 0.0))
footer:SetColor(COLOR_TITLE)
footer:SetAlpha(1.0)
local refreshButton = MakeButton(footer, -PAD, 0, 88, 22, "Refresh")
refreshButton:SetAnchor(vec.New(1.0, 0.5))
refreshButton:SetRelativePoint(vec.New(1.0, 0.5))
local resetAllButton = Button.New(footer, vec.New(-(PAD + 94), 0), vec.New(88, 22), 1i, {
    text = "Reset All",
    textTemplate = TEXT_TEMPLATE,
    color = vec.New(0.48, 0.14, 0.12),
    hoverColor = vec.New(0.78, 0.22, 0.18),
    alpha = 0.58,
    hoverAlpha = 0.88,
    borderColor = COLOR_BORDER,
    borderSize = 1,
})
resetAllButton:SetAnchor(vec.New(1.0, 0.5))
resetAllButton:SetRelativePoint(vec.New(1.0, 0.5))

local allCVars = {}
local filteredCVars = {}
local favorites = {}
local selected = nil
local selectedCategory = nil
local selectedTypeIndex = 1
local showModified = false
local showAdvanced = false
local showHidden = false
local showFavorites = false
local categoryButtons = {}

local RebuildFilters
local RefreshDetails
local RefreshData
local cvarList
local typeMenu = ContextMenu.New(canvas, {
    items = {},
    layer = integer.add(LAYER, 100i),
    minWidth = TYPE_BUTTON_W,
})

local RESET_DIALOG_LAYER = integer.add(LAYER, 200i)
local resetAllShield = canvas:NewPanel(vec.New(0, 0), vec.New(8192, 8192), integer.sub(RESET_DIALOG_LAYER, 1i), "DebugDarkGrey")
resetAllShield:SetAnchor(vec.New(0.0, 0.0))
resetAllShield:SetRelativePoint(vec.New(0.0, 0.0))
resetAllShield:SetColor(COLOR_TITLE)
resetAllShield:SetAlpha(0.55)
resetAllShield:SetVisible(false)

local resetAllDialog = canvas:NewPanel(vec.New(0, 0), vec.New(400, 210), RESET_DIALOG_LAYER, "DebugDarkGrey")
resetAllDialog:SetAnchor(vec.New(0.5, 0.5))
resetAllDialog:SetRelativePoint(vec.New(0.5, 0.5))
resetAllDialog:SetColor(COLOR_WINDOW)
resetAllDialog:SetAlpha(0.99)
resetAllDialog:SetBorderColor(COLOR_BORDER)
resetAllDialog:SetBorderSize(1)
resetAllDialog:SetVisible(false)

local resetDialogTitle = resetAllDialog:NewText("Reset all CVars", vec.New(PAD, -12), 1i, TEXT_TEMPLATE)
resetDialogTitle:SetAnchor(vec.New(0.0, 1.0))
resetDialogTitle:SetRelativePoint(vec.New(0.0, 1.0))
resetDialogTitle:SetColor(vec.New(1.0, 0.72, 0.68))
resetDialogTitle:SetInteractable(false)
local resetDialogMessage = resetAllDialog:NewText("Choose how to reset the CVar registry. Reset Values keeps every CVar\n"
        .. "and restores defaults. Remove Stale deletes saved CVars that were\n"
        .. "not registered by the application during this run.", vec.New(PAD, -40), 1i, TEXT_TEMPLATE)
resetDialogMessage:SetAnchor(vec.New(0.0, 1.0))
resetDialogMessage:SetRelativePoint(vec.New(0.0, 1.0))
resetDialogMessage:SetColor(COLOR_TEXT)
resetDialogMessage:SetInteractable(false)

local cancelResetAllButton = MakeButton(resetAllDialog, PAD, 12, 400 - PAD * 2, 26, "Cancel")
cancelResetAllButton:SetAnchor(vec.New(0.0, 0.0))
cancelResetAllButton:SetRelativePoint(vec.New(0.0, 0.0))
local removeStaleCVarsButton = Button.New(resetAllDialog, vec.New(PAD, 46), vec.New(400 - PAD * 2, 28), 1i, {
    text = "Remove Stale CVars",
    textTemplate = TEXT_TEMPLATE,
    color = vec.New(0.58, 0.12, 0.10),
    hoverColor = vec.New(0.88, 0.20, 0.16),
    alpha = 0.78,
    hoverAlpha = 0.96,
    borderColor = COLOR_BORDER,
    borderSize = 1,
})
removeStaleCVarsButton:SetAnchor(vec.New(0.0, 0.0))
removeStaleCVarsButton:SetRelativePoint(vec.New(0.0, 0.0))
local resetAllValuesButton = Button.New(resetAllDialog, vec.New(PAD, 82), vec.New(400 - PAD * 2, 28), 1i, {
    text = "Reset Values to Defaults",
    textTemplate = TEXT_TEMPLATE,
    color = vec.New(0.16, 0.34, 0.64),
    hoverColor = vec.New(0.28, 0.55, 0.95),
    alpha = 0.78,
    hoverAlpha = 0.96,
    borderColor = COLOR_BORDER,
    borderSize = 1,
})
resetAllValuesButton:SetAnchor(vec.New(0.0, 0.0))
resetAllValuesButton:SetRelativePoint(vec.New(0.0, 0.0))

local function CloseResetAllDialog()
    resetAllDialog:SetVisible(false)
    resetAllShield:SetVisible(false)
end

local function OpenResetAllDialog()
    typeMenu:Close()
    resetAllShield:SetVisible(true)
    resetAllDialog:SetVisible(true)
end

resetAllShield:SetOnMouseUp(CloseResetAllDialog)
cancelResetAllButton:SetOnMouseUp(CloseResetAllDialog)
removeStaleCVarsButton:SetOnMouseUp(function()
    local selectedName = selected and selected.qualifiedName or nil
    Editor.ResetAllCVars()
    CloseResetAllDialog()
    RefreshData(selectedName)
end)
resetAllValuesButton:SetOnMouseUp(function()
    local selectedName = selected and selected.qualifiedName or nil
    Editor.ResetAllCVarValues()
    CloseResetAllDialog()
    RefreshData(selectedName)
end)

local function CategoryNames(mask)
    local result = {}
    for i = 2, #categories do
        if integer.btest(mask, categories[i].mask) then
            table.insert(result, categories[i].name)
        end
    end
    return table.concat(result, ", ")
end

local function WrapToHeight(text, maxWidth, maxHeight)
    local wrapped, textSize = UI.WrapText(text, TEXT_TEMPLATE, maxWidth)
    if textSize.y <= maxHeight then return wrapped, textSize.y end

    local length = utf8.len(text) or #text
    local low = 0
    local high = length
    local bestWrapped = "..."
    local bestHeight = maxHeight
    while low <= high do
        local mid = math.floor((low + high) / 2)
        local endByte = utf8.offset(text, mid + 1)
        local prefix = if endByte == nil then text else string.sub(text, 1, endByte - 1)
        local candidateWrapped, candidateSize = UI.WrapText(prefix .. "...", TEXT_TEMPLATE, maxWidth)
        if candidateSize.y <= maxHeight then
            bestWrapped = candidateWrapped
            bestHeight = candidateSize.y
            low = mid + 1
        else
            high = mid - 1
        end
    end
    return bestWrapped, bestHeight
end

local function SelectCVar(cvar)
    selected = cvar
    cvarList:Refresh()
    RefreshDetails()
end

local function UpdateValue(cvar, value)
    local wasModified = not ValuesEqual(cvar.value, cvar.defaultValue)
    cvar.value = value
    local isModified = not ValuesEqual(cvar.value, cvar.defaultValue)

    if showModified and wasModified ~= isModified then
        RebuildFilters(true)
    else
        cvarList:Refresh()
    end
    RefreshDetails()
end

local function ApplyValue(cvar, value)
    if cvar.type == "Integer Vector" then
        Editor.SetCVar(
            cvar.category,
            cvar.name,
            integer.create(value.x),
            integer.create(value.y),
            integer.create(value.z),
            integer.create(value.w)
        )
    elseif cvar.type == "Float Vector" then
        Editor.SetCVar(cvar.category, cvar.name, value.x, value.y, value.z, value.w)
    elseif cvar.type == "Integer" then
        value = integer.create(value)
        Editor.SetCVar(cvar.category, cvar.name, value)
    else
        Editor.SetCVar(cvar.category, cvar.name, value)
    end
    UpdateValue(cvar, value)
end

local function CreateRow(parent)
    local row = parent:NewPanel(vec.New(0, 0), vec.New(LIST_W - 10, ROW_H - 1), 1i, "DebugDarkGrey")
    row:SetAnchor(vec.New(0.0, 1.0))
    row:SetRelativePoint(vec.New(0.0, 1.0))
    row:SetAlpha(0.20)

    local nameButton = Button.New(row, vec.New(0, 0), vec.New(215, ROW_H - 1), 1i, {
        text = "",
        textTemplate = TEXT_TEMPLATE,
        textAlign = "left",
        textPadX = 10,
        alpha = 0.0,
        hoverAlpha = 0.35,
    })
    nameButton:SetAnchor(vec.New(0.0, 0.5))
    nameButton:SetRelativePoint(vec.New(0.0, 0.5))
    nameButton.root:SetClipChildren(true)

    local valueInput = InputBox.New(row, vec.New(220, 0), vec.New(132, 20), 1i, {
        panelTemplate = "DebugDarkGrey",
        textTemplate = TEXT_TEMPLATE,
        defaultText = "",
    })
    valueInput:SetAnchor(vec.New(0.0, 0.5))
    valueInput:SetRelativePoint(vec.New(0.0, 0.5))

    local booleanButton = MakeButton(row, 220, 0, 132, 20, "")
    booleanButton:SetAnchor(vec.New(0.0, 0.5))
    booleanButton:SetRelativePoint(vec.New(0.0, 0.5))

    local defaultText = row:NewText("", vec.New(365, 0), 1i, TEXT_TEMPLATE)
    defaultText:SetAnchor(vec.New(0.0, 0.5))
    defaultText:SetRelativePoint(vec.New(0.0, 0.5))
    defaultText:SetColor(COLOR_MUTED)
    defaultText:SetInteractable(false)
    local typeText = row:NewText("", vec.New(475, 0), 1i, TEXT_TEMPLATE)
    typeText:SetAnchor(vec.New(0.0, 0.5))
    typeText:SetRelativePoint(vec.New(0.0, 0.5))
    typeText:SetColor(COLOR_MUTED)
    typeText:SetInteractable(false)

    local handle = {
        panel = row,
        nameButton = nameButton,
        valueInput = valueInput,
        booleanButton = booleanButton,
        defaultText = defaultText,
        typeText = typeText,
        item = nil,
    }

    function handle:SetVisible(value) self.panel:SetVisible(value) end
    function handle:SetPosY(value) self.panel:SetPosY(value) end

    row:SetOnMouseUp(function() SelectCVar(handle.item) end)
    nameButton:SetOnMouseUp(function() SelectCVar(handle.item) end)
    valueInput:SetOnFocusBegin(function() SelectCVar(handle.item) end)
    valueInput:SetOnSubmit(function(input)
        local item = handle.item
        if item == nil or HasFlag(item.flags, FLAG_READ_ONLY) then return true end
        local value = ParseValue(input.text, item.type)
        if value ~= nil then ApplyValue(item, value) end
        return true
    end)
    booleanButton:SetOnMouseUp(function()
        local item = handle.item
        if item ~= nil and not HasFlag(item.flags, FLAG_READ_ONLY) then
            SelectCVar(item)
            ApplyValue(item, not item.value)
        end
    end)

    return handle
end

local function BindRow(handle, index, item)
    if handle.item ~= item then
        -- Virtual rows are recycled. Never carry focus/cursor state into the next CVar.
        handle.valueInput:Unfocus()
    end
    handle.item = item
    handle.panel:SetColor(index % 2 == 0 and COLOR_PANEL or COLOR_WINDOW)
    handle.panel:SetAlpha(selected == item and 0.95 or 0.35)
    if selected == item then handle.panel:SetColor(COLOR_ACCENT) end

    local marker = ValuesEqual(item.value, item.defaultValue) and "" or "* "
    handle.nameButton:SetText(TextUtil.TruncateToWidth(marker .. item.qualifiedName, TEXT_TEMPLATE, 205))
    handle.nameButton.text:SetColor(selected == item and COLOR_TEXT or (marker == "" and COLOR_TEXT or COLOR_MODIFIED))
    handle.defaultText:SetText(FormatValue(item.defaultValue, item.type))
    handle.typeText:SetText(item.type)
    handle.defaultText:SetColor(selected == item and COLOR_TEXT or COLOR_MUTED)
    handle.typeText:SetColor(selected == item and COLOR_TEXT or COLOR_MUTED)

    local isBoolean = item.type == "Boolean"
    if isBoolean then handle.valueInput:Unfocus() end
    handle.booleanButton:SetVisible(isBoolean)
    handle.valueInput.button:SetVisible(not isBoolean)
    handle.booleanButton:SetInteractable(not HasFlag(item.flags, FLAG_READ_ONLY))
    handle.valueInput.button:SetInteractable(not HasFlag(item.flags, FLAG_READ_ONLY))
    if isBoolean then
        handle.booleanButton:SetText(item.value and "Enabled" or "Disabled")
    else
        handle.valueInput.placeholderText = ""
        handle.valueInput:SetText(FormatEditorValue(item.value, item.type))
    end
end

cvarList = VirtualList.New(window, vec.New(CATEGORY_W, -(TITLE_H + TOOLBAR_H + HEADER_H)), vec.New(LIST_W, LIST_BODY_H), 1i, {
    rowHeight = ROW_H,
    scrollBox = {
        panelTemplate = "DebugDarkGrey",
        contentSizeX = LIST_W,
        contentSizeY = LIST_BODY_H,
        verticalScrollBar = true,
        scrollBarWidth = 10,
        alpha = 0.0,
    },
    createRow = CreateRow,
    bindRow = BindRow,
})
cvarList:SetAnchor(vec.New(0.0, 1.0))
cvarList:SetRelativePoint(vec.New(0.0, 1.0))

local function PositionDetailRows(rows, startY)
    for i, row in rows do
        local y = startY - (i - 1) * 20
        row.label:SetPosY(y)
        row.value:SetPosY(y)
    end
    return startY - #rows * 20
end

RefreshDetails = function()
    detailFullNameLabel:SetPosY(-32)
    local nameCardY = -44
    detailNameCard:SetPosY(nameCardY)

    if selected == nil then
        detailNameCard:SetHeight(22)
        detailNameAccent:SetHeight(22)
        detailName:SetPosY(nameCardY - 5)
        detailName:SetText("Select a console variable")
        detailDescriptionLabel:SetPosY(-78)
        detailDescriptionCard:SetVisible(false)
        detailDescription:SetPosY(-96)
        detailDescription:SetText("")
        detailSeparatorA:SetPosY(-116)
        local afterMetadata = PositionDetailRows(detailRows, -134)
        detailSeparatorB:SetPosY(afterMetadata - 4)
        PositionDetailRows(provenanceRows, afterMetadata - 22)
        for _, row in detailRows do row.value:SetText("") end
        for _, row in provenanceRows do row.value:SetText("") end
        SetButtonEnabledVisual(resetButton, false)
        SetButtonEnabledVisual(favoriteDetailButton, false)
        return
    end

    local detailCardTextWidth = DETAILS_W - PAD * 2 - 12
    local wrappedName, nameSize = UI.WrapText(selected.qualifiedName, TEXT_TEMPLATE, detailCardTextWidth)
    local nameCardHeight = math.max(22, nameSize.y + 10)
    detailNameCard:SetHeight(nameCardHeight)
    detailNameAccent:SetHeight(nameCardHeight)
    detailName:SetPosY(nameCardY - 5)
    detailName:SetText(wrappedName)

    local descriptionLabelY = nameCardY - nameCardHeight - 12
    detailDescriptionLabel:SetPosY(descriptionLabelY)
    local descriptionCardY = descriptionLabelY - 14
    local description = #selected.description > 0 and selected.description or "No description provided"
    local wrappedDescription, descriptionHeight = WrapToHeight(description, detailCardTextWidth, 60)
    local descriptionCardHeight = math.max(24, descriptionHeight + 10)
    detailDescriptionCard:SetVisible(true)
    detailDescriptionCard:SetPosY(descriptionCardY)
    detailDescriptionCard:SetHeight(descriptionCardHeight)
    detailDescription:SetPosY(descriptionCardY - 5)
    detailDescription:SetText(wrappedDescription)
    detailDescription:SetColor(#selected.description > 0 and COLOR_TEXT or COLOR_MUTED)

    local flags = {}
    if HasFlag(selected.flags, FLAG_READ_ONLY) then table.insert(flags, "Read only") end
    if HasFlag(selected.flags, FLAG_ADVANCED) then table.insert(flags, "Advanced") end
    if HasFlag(selected.flags, FLAG_HIDDEN) then table.insert(flags, "Hidden") end
    local flagText = #flags > 0 and table.concat(flags, ", ") or "None"

    detailRows[1].value:SetText(FormatEditorValue(selected.value, selected.type))
    detailRows[2].value:SetText(FormatValue(selected.defaultValue, selected.type))
    detailRows[3].value:SetText("Not exposed")
    detailRows[4].value:SetText("Not exposed")
    detailRows[5].value:SetText(selected.type)
    detailRows[6].value:SetText(CategoryNames(selected.category))
    detailRows[7].value:SetText(flagText)

    local isModified = not ValuesEqual(selected.value, selected.defaultValue)
    provenanceRows[1].value:SetText("Not exposed")
    provenanceRows[2].value:SetText("Not exposed")
    provenanceRows[3].value:SetText(isModified and "Modified; time not tracked" or "Not tracked")

    local metadataY = descriptionCardY - descriptionCardHeight - 26
    detailSeparatorA:SetPosY(metadataY + 10)
    local afterMetadata = PositionDetailRows(detailRows, metadataY)
    detailSeparatorB:SetPosY(afterMetadata - 4)
    PositionDetailRows(provenanceRows, afterMetadata - 22)

    local canReset = not HasFlag(selected.flags, FLAG_READ_ONLY) and isModified
    SetButtonEnabledVisual(resetButton, canReset)
    SetButtonEnabledVisual(favoriteDetailButton, true)
    favoriteDetailButton:SetText(favorites[selected.qualifiedName] and "Remove from Favorites" or "Add to Favorites")
end

local function UpdateFilterButtons()
    local hasStatusFilter = showModified or showAdvanced or showHidden or showFavorites
    allButton:SetHighlighted(not hasStatusFilter)
    modifiedButton:SetHighlighted(showModified)
    advancedButton:SetHighlighted(showAdvanced)
    hiddenButton:SetHighlighted(showHidden)
    favoritesButton:SetHighlighted(showFavorites)
    typeButton:SetText("Type: " .. types[selectedTypeIndex] .. "  v")
    for i, button in categoryButtons do
        local isSelected = categories[i].mask == selectedCategory or (i == 1 and selectedCategory == nil)
        button:SetHighlighted(isSelected)
        button.countBadge:SetColor(isSelected and COLOR_ACCENT or COLOR_BORDER)
        button.countBadge:SetAlpha(isSelected and 0.85 or 0.65)
        button.countText:SetColor(isSelected and COLOR_TEXT or COLOR_MUTED)
    end
end

RebuildFilters = function(preserveScroll)
    local query = string.lower(searchInput.text or "")
    local selectedType = types[selectedTypeIndex]
    filteredCVars = {}

    for _, cvar in allCVars do
        local visible = (query == "" or string.find(cvar.searchText, query, 1, true) ~= nil)
        visible = visible and (selectedCategory == nil or integer.btest(cvar.category, selectedCategory))
        visible = visible and (selectedType == "All" or cvar.type == selectedType)
        visible = visible and (showAdvanced or not HasFlag(cvar.flags, FLAG_ADVANCED))
        visible = visible and (showHidden or not HasFlag(cvar.flags, FLAG_HIDDEN))
        visible = visible and (not showModified or not ValuesEqual(cvar.value, cvar.defaultValue))
        visible = visible and (not showFavorites or favorites[cvar.qualifiedName] == true)
        if visible then table.insert(filteredCVars, cvar) end
    end

    cvarList:SetItems(filteredCVars, preserveScroll == true)
    countText:SetText(string.format("Total: %d CVars (%d shown)", #allCVars, #filteredCVars))
    UpdateFilterButtons()
end

local function UpdateCategoryCounts()
    for i, category in categories do
        local count = 0
        if category.mask == nil then
            count = #allCVars
        else
            for _, cvar in allCVars do
                if integer.btest(cvar.category, category.mask) then count += 1 end
            end
        end
        categoryButtons[i].countText:SetText(tostring(count))
    end
end

RefreshData = function(reselectName)
    reselectName = reselectName or (selected and selected.qualifiedName or nil)
    allCVars = Editor.GetCVars()
    for _, cvar in allCVars do
        cvar.searchText = string.lower(cvar.qualifiedName .. " " .. cvar.description)
    end
    table.sort(allCVars, function(a, b) return a.qualifiedName < b.qualifiedName end)
    UpdateCategoryCounts()
    selected = nil
    if reselectName ~= nil then
        for _, cvar in allCVars do
            if cvar.qualifiedName == reselectName then selected = cvar break end
        end
    end
    RebuildFilters(reselectName ~= nil)
    RefreshDetails()
end

for i, category in categories do
    local button = Button.New(categoryPanel, vec.New(6, -(HEADER_H + 4 + (i - 1) * 27)), vec.New(CATEGORY_W - 12, 23), 1i, {
        text = category.name,
        textTemplate = TEXT_TEMPLATE,
        textAlign = "left",
        textPadX = 8,
        alpha = 0.10,
        hoverAlpha = 0.48,
    })
    button:SetAnchor(vec.New(0.0, 1.0))
    button:SetRelativePoint(vec.New(0.0, 1.0))

    local countBadge = button.root:NewPanel(vec.New(-6, 0), vec.New(28, 15), 1i, "DebugDarkGrey")
    countBadge:SetAnchor(vec.New(1.0, 0.5))
    countBadge:SetRelativePoint(vec.New(1.0, 0.5))
    countBadge:SetColor(COLOR_BORDER)
    countBadge:SetAlpha(0.65)
    countBadge:SetInteractable(false)
    local categoryCount = countBadge:NewText("0", vec.New(0, 0), 1i, TEXT_TEMPLATE)
    categoryCount:SetAnchor(vec.New(0.5, 0.5))
    categoryCount:SetRelativePoint(vec.New(0.5, 0.5))
    categoryCount:SetColor(COLOR_MUTED)
    categoryCount:SetInteractable(false)
    button.countBadge = countBadge
    button.countText = categoryCount
    button:SetOnMouseUp(function()
        selectedCategory = category.mask
        RebuildFilters()
    end)
    table.insert(categoryButtons, button)
end

searchInput:SetOnTextChanged(function() RebuildFilters() end)
allButton:SetOnMouseUp(function()
    showModified = false
    showAdvanced = false
    showHidden = false
    showFavorites = false
    RebuildFilters()
end)
modifiedButton:SetOnMouseUp(function()
    showModified = not showModified
    RebuildFilters()
end)
advancedButton:SetOnMouseUp(function()
    showAdvanced = not showAdvanced
    RebuildFilters()
end)
hiddenButton:SetOnMouseUp(function()
    showHidden = not showHidden
    RebuildFilters()
end)
favoritesButton:SetOnMouseUp(function()
    showFavorites = not showFavorites
    RebuildFilters()
end)
typeButton:SetOnMouseUp(function()
    local items = {}
    for i, typeName in types do
        table.insert(items, {
            label = typeName,
            selected = i == selectedTypeIndex,
            onClick = function()
                selectedTypeIndex = i
                RebuildFilters()
            end,
        })
    end
    typeMenu:SetItems(items)
    typeMenu:OpenBelow(typeButton.root)
end)
resetButton:SetOnMouseUp(function()
    local canReset = selected ~= nil
        and not HasFlag(selected.flags, FLAG_READ_ONLY)
        and not ValuesEqual(selected.value, selected.defaultValue)
    if canReset then
        Editor.ResetCVar(selected.category, selected.name)
        UpdateValue(selected, selected.defaultValue)
    end
end)
favoriteDetailButton:SetOnMouseUp(function()
    if selected == nil then return end
    favorites[selected.qualifiedName] = not favorites[selected.qualifiedName]
    RefreshDetails()
    if showFavorites then RebuildFilters(true) end
end)
refreshButton:SetOnMouseUp(function() RefreshData() end)
resetAllButton:SetOnMouseUp(OpenResetAllDialog)

local methods = {}
function methods:Show()
    window:SetVisible(true)
    RefreshData()
end
function methods:Hide()
    typeMenu:Close()
    CloseResetAllDialog()
    window:SetVisible(false)
end
function methods:IsVisible() return window:IsVisible() end

closeButton:SetOnMouseUp(function() methods:Hide() end)
EditorRegistry.Register("CVarEditor", methods)
