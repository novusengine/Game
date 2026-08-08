local EditMode = require("@src/API/Game/EditMode")
local EditModeDefaults = require("@src/API/Game/EditModeDefaults")
local EditModeProfile = require("@src/API/Game/EditModeProfile")
local Button = require("@src/API/UI/Button")
local Checkbox = require("@src/API/UI/Checkbox")
local Collapsible = require("@src/API/UI/Collapsible")
local ScrollBox = require("@src/API/UI/ScrollBox")
local Dropdown = require("@src/API/UI/Dropdown")
local InputBox = require("@src/API/UI/InputBox")
local Draggable = require("@src/API/UI/Draggable")

local BAR_WIDTH = 460
local BAR_HEIGHT = 300
local HEADER_HEIGHT = 36
local FOOTER_HEIGHT = 40
local BODY_HEIGHT = BAR_HEIGHT - HEADER_HEIGHT - FOOTER_HEIGHT
local BODY_WIDTH = BAR_WIDTH - 20
local PADDING = 8
local BUTTON_HEIGHT = 24
local CHECKBOX_SIZE = 16
local ROW_HEIGHT = 24
local ROW_GAP = 2
local CATEGORY_GAP = 4
local COLLAPSIBLE_BODY_PADDING = 6
local GRID_COLUMNS = 2
local GRID_CELL_HEIGHT = ROW_HEIGHT + ROW_GAP
local GRID_CELL_WIDTH = math.floor((BODY_WIDTH - PADDING * 2 - 10) / GRID_COLUMNS)

local COLOR_BAR_BG = vec.New(0.035, 0.042, 0.050)
local COLOR_HEADER_BG = vec.New(0.025, 0.030, 0.037)
local COLOR_FOOTER_BG = vec.New(0.025, 0.030, 0.037)
local COLOR_BORDER = vec.New(0.225, 0.255, 0.290)
local COLOR_ACCENT = vec.New(0.390, 0.650, 0.960)
local COLOR_TEXT = vec.New(0.900, 0.915, 0.935)
local COLOR_MUTED = vec.New(0.570, 0.600, 0.640)
local COLOR_ROW_BG = vec.New(0.055, 0.063, 0.075)
local COLOR_ROW_HOVER = vec.New(0.075, 0.085, 0.100)
local COLOR_MODAL_BG = vec.New(0.020, 0.025, 0.030)

local barRoot : Panel? = nil
local barBody : Panel? = nil
local bodyScroll : ScrollBox.ScrollBox? = nil
local profileDropdown : Dropdown.Dropdown? = nil
local saveButton : Button.Button? = nil
local categoryCollapsibles : { [EditModeDefaults.CategoryID] : Collapsible.Collapsible } = {}
local elementCheckboxes : { [EditModeDefaults.ElementID] : Checkbox.Checkbox } = {}
local modalRoot : Panel? = nil
local contentPanel : Panel? = nil

local function RefreshProfileDropdown()
    if profileDropdown == nil then
        return
    end

    local names = EditModeProfile.GetProfileNames()
    local options : { Dropdown.Option } = {}

    table.insert(options, { label = "Default", value = "Default", enabled = true })

    for _, name in names do
        table.insert(options, { label = name, value = name, enabled = true })
    end

    table.insert(options, { label = "--- Create New Profile ---", value = "__create_new__", enabled = true })

    profileDropdown:SetOptions(options)

    local activeName = EditModeProfile.GetActiveProfileName()
    profileDropdown:SetValue(activeName, false)
end

local function RefreshSaveButton()
    if saveButton ~= nil then
        saveButton:SetInteractable(EditMode.CanSaveActiveProfile())
    end
end

local function CloseModal()
    if modalRoot ~= nil then
        UI.DestroyWidget(modalRoot)
        modalRoot = nil
    end
end

local function OpenDiscardChangesModal(confirmText : string, onDiscard : () -> (), onCancel : (() -> ())?)
    CloseModal()

    local canvas = EditMode.GetRoot()
    if canvas == nil then
        if onCancel ~= nil then
            onCancel()
        end
        return
    end

    modalRoot = canvas:NewPanel(vec.New(0, 0), vec.New(1920, 1080), 10i, "DebugDarkGrey")
    modalRoot:SetColor(COLOR_MODAL_BG)
    modalRoot:SetAlpha(0.85)
    modalRoot:SetOnMouseScroll(function() end)

    local dialog = modalRoot:NewPanel(vec.New(0, 0), vec.New(460, 150), 1i, "DebugDarkGrey")
    dialog:SetAnchor(vec.New(0.5, 0.5))
    dialog:SetRelativePoint(vec.New(0.5, 0.5))
    dialog:SetColor(COLOR_BAR_BG)
    dialog:SetAlpha(0.99)
    dialog:SetBorderColor(COLOR_ACCENT)
    dialog:SetBorderSize(1)

    local title = dialog:NewText("Unsaved Changes", vec.New(0, -18), 1i, "DefaultButtonText")
    title:SetAnchor(vec.New(0.5, 1.0))
    title:SetRelativePoint(vec.New(0.5, 0.5))
    title:SetFontSize(12)
    title:SetColor(COLOR_TEXT)

    local message = dialog:NewText("Your unsaved layout changes will be discarded.", vec.New(0, -52), 1i, "DefaultButtonText")
    message:SetAnchor(vec.New(0.5, 1.0))
    message:SetRelativePoint(vec.New(0.5, 0.5))
    message:SetFontSize(9)
    message:SetColor(COLOR_MUTED)

    local discardButton = Button.New(dialog, vec.New(-70, -94), vec.New(130, BUTTON_HEIGHT), 1i, {
        text = confirmText,
        panelTemplate = "DebugDarkGrey",
        textTemplate = "DefaultButtonText",
        color = vec.New(0.50, 0.20, 0.20),
        hoverColor = vec.New(0.70, 0.30, 0.30),
        alpha = 0.95,
        hoverAlpha = 1.0,
        borderColor = vec.New(0.70, 0.30, 0.30),
        borderSize = 1,
    })
    discardButton:SetAnchor(vec.New(0.5, 1.0))
    discardButton:SetRelativePoint(vec.New(0.5, 1.0))
    discardButton:SetFontSize(9)

    local cancelButton = Button.New(dialog, vec.New(70, -94), vec.New(100, BUTTON_HEIGHT), 1i, {
        text = "Cancel",
        panelTemplate = "DebugDarkGrey",
        textTemplate = "DefaultButtonText",
        color = COLOR_ROW_BG,
        hoverColor = COLOR_ROW_HOVER,
        alpha = 0.90,
        hoverAlpha = 1.0,
        borderColor = COLOR_BORDER,
        borderSize = 1,
    })
    cancelButton:SetAnchor(vec.New(0.5, 1.0))
    cancelButton:SetRelativePoint(vec.New(0.5, 1.0))
    cancelButton:SetFontSize(9)

    discardButton:SetOnMouseUp(function()
        CloseModal()
        onDiscard()
    end)

    cancelButton:SetOnMouseUp(function()
        CloseModal()
        if onCancel ~= nil then
            onCancel()
        end
    end)
end

local function RunAfterDiscardConfirmation(confirmText : string, action : () -> (), onCancel : (() -> ())?)
    if not EditMode.HasUnsavedChanges() then
        action()
        return
    end
    OpenDiscardChangesModal(confirmText, action, onCancel)
end

local function ApplyProfile(profileName : string)
    if EditModeProfile.SetActiveProfile(profileName) then
        EditMode.ApplyActiveProfile()
    end
    RefreshProfileDropdown()
    RefreshSaveButton()
end

local function OpenImportModal()
    CloseModal()

    local canvas = EditMode.GetRoot()
    if canvas == nil then
        return
    end

    modalRoot = canvas:NewPanel(vec.New(0, 0), vec.New(1920, 1080), 10i, "DebugDarkGrey")
    modalRoot:SetColor(COLOR_MODAL_BG)
    modalRoot:SetAlpha(0.85)
    modalRoot:SetOnMouseScroll(function() end)

    local dialog = modalRoot:NewPanel(vec.New(0, 0), vec.New(500, 220), 1i, "DebugDarkGrey")
    dialog:SetAnchor(vec.New(0.5, 0.5))
    dialog:SetRelativePoint(vec.New(0.5, 0.5))
    dialog:SetColor(COLOR_BAR_BG)
    dialog:SetAlpha(0.99)
    dialog:SetBorderColor(COLOR_ACCENT)
    dialog:SetBorderSize(1)

    local title = dialog:NewText("Import UI Code", vec.New(0, -16), 1i, "DefaultButtonText")
    title:SetAnchor(vec.New(0.5, 1.0))
    title:SetRelativePoint(vec.New(0.5, 0.5))
    title:SetFontSize(12)
    title:SetColor(COLOR_TEXT)

    local instruction = dialog:NewText("Paste a UI code to import:", vec.New(PADDING, -40), 1i, "DefaultButtonText")
    instruction:SetAnchor(vec.New(0.0, 1.0))
    instruction:SetRelativePoint(vec.New(0.0, 1.0))
    instruction:SetFontSize(9)
    instruction:SetColor(COLOR_MUTED)

    local inputBox = InputBox.New(dialog, vec.New(PADDING, -54), vec.New(500 - PADDING * 2, 28), 1i, {
        panelTemplate = "DialogBox",
        textTemplate = "DefaultButtonText",
        placeholderText = "NVP1:...",
    })
    inputBox:SetAnchor(vec.New(0.0, 1.0))
    inputBox:SetRelativePoint(vec.New(0.0, 1.0))

    local canOverride = not EditModeProfile.IsDefaultProfile(EditModeProfile.GetActiveProfileName())
    local importAsNew = true

    local toggleBtn = Button.New(dialog, vec.New(PADDING, -90), vec.New(500 - PADDING * 2, BUTTON_HEIGHT), 1i, {
        text = "Mode: Import as New Profile",
        panelTemplate = "DebugDarkGrey",
        textTemplate = "DefaultButtonText",
        color = COLOR_ROW_BG,
        hoverColor = COLOR_ROW_HOVER,
        alpha = 0.90,
        hoverAlpha = 1.0,
        borderColor = COLOR_BORDER,
        borderSize = 1,
    })
    toggleBtn:SetAnchor(vec.New(0.0, 1.0))
    toggleBtn:SetRelativePoint(vec.New(0.0, 1.0))
    toggleBtn:SetFontSize(9)
    toggleBtn.text:SetAnchor(vec.New(0.0, 0.5))
    toggleBtn.text:SetRelativePoint(vec.New(0.0, 0.5))
    toggleBtn.text:SetPos(vec.New(8, 0))
    toggleBtn:SetInteractable(canOverride)

    toggleBtn:SetOnMouseUp(function()
        if not canOverride then
            return
        end
        importAsNew = not importAsNew
        if importAsNew then
            toggleBtn:SetText("Mode: Import as New Profile")
        else
            toggleBtn:SetText("Mode: Override Current Profile")
        end
    end)

    local applyButton = Button.New(dialog, vec.New(PADDING, -124), vec.New(120, BUTTON_HEIGHT), 1i, {
        text = "Import",
        panelTemplate = "DebugDarkGrey",
        textTemplate = "DefaultButtonText",
        color = vec.New(0.15, 0.35, 0.55),
        hoverColor = vec.New(0.20, 0.45, 0.70),
        alpha = 0.95,
        hoverAlpha = 1.0,
        borderColor = COLOR_ACCENT,
        borderSize = 1,
    })
    applyButton:SetAnchor(vec.New(0.0, 1.0))
    applyButton:SetRelativePoint(vec.New(0.0, 1.0))
    applyButton:SetFontSize(9)

    local cancelButton = Button.New(dialog, vec.New(136, -124), vec.New(80, BUTTON_HEIGHT), 1i, {
        text = "Cancel",
        panelTemplate = "DebugDarkGrey",
        textTemplate = "DefaultButtonText",
        color = COLOR_ROW_BG,
        hoverColor = COLOR_ROW_HOVER,
        alpha = 0.90,
        hoverAlpha = 1.0,
        borderColor = COLOR_BORDER,
        borderSize = 1,
    })
    cancelButton:SetAnchor(vec.New(0.0, 1.0))
    cancelButton:SetRelativePoint(vec.New(0.0, 1.0))
    cancelButton:SetFontSize(9)

    applyButton:SetOnMouseUp(function()
        local code = inputBox:GetText()
        if code == "" then
            return
        end

        local function Import()
            local success = if importAsNew
                then EditModeProfile.ImportAsNewProfile(code)
                else EditModeProfile.ImportAsOverride(code)
            if success then
                EditMode.ApplyActiveProfile()
                RefreshProfileDropdown()
                RefreshSaveButton()
                CloseModal()
            end
        end

        RunAfterDiscardConfirmation("Discard & Import", Import, nil)
    end)

    cancelButton:SetOnMouseUp(function()
        CloseModal()
    end)
end

local function OpenExportModal()
    CloseModal()

    local canvas = EditMode.GetRoot()
    if canvas == nil then
        return
    end

    local code = EditModeProfile.ExportProfile()

    modalRoot = canvas:NewPanel(vec.New(0, 0), vec.New(1920, 1080), 10i, "DebugDarkGrey")
    modalRoot:SetColor(COLOR_MODAL_BG)
    modalRoot:SetAlpha(0.85)
    modalRoot:SetOnMouseScroll(function() end)

    local dialog = modalRoot:NewPanel(vec.New(0, 0), vec.New(500, 160), 1i, "DebugDarkGrey")
    dialog:SetAnchor(vec.New(0.5, 0.5))
    dialog:SetRelativePoint(vec.New(0.5, 0.5))
    dialog:SetColor(COLOR_BAR_BG)
    dialog:SetAlpha(0.99)
    dialog:SetBorderColor(COLOR_ACCENT)
    dialog:SetBorderSize(1)

    local title = dialog:NewText("Export UI Code", vec.New(0, -16), 1i, "DefaultButtonText")
    title:SetAnchor(vec.New(0.5, 1.0))
    title:SetRelativePoint(vec.New(0.5, 0.5))
    title:SetFontSize(12)
    title:SetColor(COLOR_TEXT)

    local instruction = dialog:NewText("Copy this code to share your layout:", vec.New(PADDING, -40), 1i, "DefaultButtonText")
    instruction:SetAnchor(vec.New(0.0, 1.0))
    instruction:SetRelativePoint(vec.New(0.0, 1.0))
    instruction:SetFontSize(9)
    instruction:SetColor(COLOR_MUTED)

    local codeDisplay = InputBox.New(dialog, vec.New(PADDING, -54), vec.New(500 - PADDING * 2, 28), 1i, {
        panelTemplate = "DialogBox",
        textTemplate = "DefaultButtonText",
        defaultText = code,
    })
    codeDisplay:SetAnchor(vec.New(0.0, 1.0))
    codeDisplay:SetRelativePoint(vec.New(0.0, 1.0))

    local closeButton = Button.New(dialog, vec.New(PADDING, -92), vec.New(80, BUTTON_HEIGHT), 1i, {
        text = "Close",
        panelTemplate = "DebugDarkGrey",
        textTemplate = "DefaultButtonText",
        color = COLOR_ROW_BG,
        hoverColor = COLOR_ROW_HOVER,
        alpha = 0.90,
        hoverAlpha = 1.0,
        borderColor = COLOR_BORDER,
        borderSize = 1,
    })
    closeButton:SetAnchor(vec.New(0.0, 1.0))
    closeButton:SetRelativePoint(vec.New(0.0, 1.0))
    closeButton:SetFontSize(9)

    closeButton:SetOnMouseUp(function()
        CloseModal()
    end)
end

local function RefreshCategoryContent(categoryID : EditModeDefaults.CategoryID)
    local collapsible = categoryCollapsibles[categoryID]
    if collapsible == nil or contentPanel == nil then
        return
    end

    local catElements = EditMode.GetElementsByCategory(categoryID)
    local visibleCount = #catElements
    local rows = math.ceil(visibleCount / GRID_COLUMNS)
    local contentHeight = rows * GRID_CELL_HEIGHT

    collapsible:SetBodyContentHeight(contentHeight)

    for i, element in catElements do
        local checkbox = elementCheckboxes[element.id]
        if checkbox ~= nil then
            local zeroIdx = i - 1
            local col = zeroIdx % GRID_COLUMNS
            local row = math.floor(zeroIdx / GRID_COLUMNS)
            local posX = PADDING + col * GRID_CELL_WIDTH
            local posY = -(COLLAPSIBLE_BODY_PADDING + row * GRID_CELL_HEIGHT)
            checkbox.root:SetPos(vec.New(posX, posY))
        end
    end
end

local function RefreshAllCategories()
    local totalHeight = 0
    for _, catDef in EditModeDefaults.Categories do
        RefreshCategoryContent(catDef.id)
        local collapsible = categoryCollapsibles[catDef.id]
        if collapsible ~= nil then
            totalHeight += collapsible:GetTotalHeight() + CATEGORY_GAP
        end
    end

    if bodyScroll ~= nil then
        bodyScroll:SetContentHeight(totalHeight)
    end
end

local function CreateEditModeBar()
    local root = EditMode.GetRoot()
    if root == nil then
        return
    end

    -- Center horizontally, ~25% from top (Y=810 in 1920x1080 space, Y-up)
    local posX = 960 - (BAR_WIDTH / 2)
    local posY = 810 - BAR_HEIGHT

    barRoot = root:NewPanel(vec.New(posX, posY), vec.New(BAR_WIDTH, BAR_HEIGHT), 5i, "DebugDarkGrey")
    barRoot:SetColor(COLOR_BAR_BG)
    barRoot:SetAlpha(0.98)
    barRoot:SetBorderColor(COLOR_BORDER)
    barRoot:SetBorderSize(1)

    -- Header (drag handle)
    local header = barRoot:NewPanel(vec.New(0, 0), vec.New(BAR_WIDTH, HEADER_HEIGHT), 1i, "DebugDarkGrey")
    header:SetAnchor(vec.New(0.0, 1.0))
    header:SetRelativePoint(vec.New(0.0, 1.0))
    header:SetColor(COLOR_HEADER_BG)
    header:SetAlpha(1.0)
    header:SetBorderColor(COLOR_BORDER)
    header:SetBorderSize(1)

    Draggable.Attach(barRoot, header)

    local headerTitle = header:NewText("Edit Mode", vec.New(PADDING, 0), 1i, "DefaultButtonText")
    headerTitle:SetAnchor(vec.New(0.0, 0.5))
    headerTitle:SetRelativePoint(vec.New(0.0, 0.5))
    headerTitle:SetFontSize(12)
    headerTitle:SetColor(COLOR_ACCENT)

    -- Profile dropdown in header
    local dropdownWidth = 160
    profileDropdown = Dropdown.New(header, EditMode.GetCanvas(), vec.New(120, 0), vec.New(dropdownWidth, BUTTON_HEIGHT), 1i, {
        placeholder = "Select Profile...",
        button = {
            panelTemplate = "DebugDarkGrey",
            textTemplate = "DefaultButtonText",
            color = COLOR_ROW_BG,
            hoverColor = COLOR_ROW_HOVER,
            alpha = 0.90,
            hoverAlpha = 1.0,
            borderColor = COLOR_BORDER,
            borderSize = 1,
        },
        menuWidth = dropdownWidth,
        menuLayer = 6i,
        onValueChanged = function(value)
            if value == "__create_new__" then
                local function CreateProfile()
                    local nextNum = 1
                    local existing = EditModeProfile.GetProfileNames()
                    for _, name in existing do
                        local num = tonumber(string.match(name, "^Profile (%d+)$"))
                        if num ~= nil and num >= nextNum then
                            nextNum = num + 1
                        end
                    end
                    local profileName = "Profile " .. tostring(nextNum)
                    EditModeProfile.CreateProfile(profileName)
                    ApplyProfile(profileName)
                end
                RunAfterDiscardConfirmation("Discard & Create", CreateProfile, RefreshProfileDropdown)
            else
                if value == EditModeProfile.GetActiveProfileName() then
                    return
                end
                RunAfterDiscardConfirmation("Discard & Switch", function()
                    ApplyProfile(value)
                end, RefreshProfileDropdown)
            end
        end,
    })
    profileDropdown:SetAnchor(vec.New(0.0, 0.5))
    profileDropdown:SetRelativePoint(vec.New(0.0, 0.5))
    profileDropdown:SetPos(vec.New(100, 0))
    profileDropdown.button:SetFontSize(8)

    RefreshProfileDropdown()

    -- Import button
    local importButton = Button.New(header, vec.New(280, 0), vec.New(60, BUTTON_HEIGHT), 1i, {
        text = "Import",
        panelTemplate = "DebugDarkGrey",
        textTemplate = "DefaultButtonText",
        color = COLOR_ROW_BG,
        hoverColor = COLOR_ROW_HOVER,
        alpha = 0.90,
        hoverAlpha = 1.0,
        borderColor = COLOR_BORDER,
        borderSize = 1,
    })
    importButton:SetAnchor(vec.New(0.0, 0.5))
    importButton:SetRelativePoint(vec.New(0.0, 0.5))
    importButton:SetFontSize(8)
    importButton:SetOnMouseUp(function()
        OpenImportModal()
    end)

    -- Export button
    local exportButton = Button.New(header, vec.New(348, 0), vec.New(60, BUTTON_HEIGHT), 1i, {
        text = "Export",
        panelTemplate = "DebugDarkGrey",
        textTemplate = "DefaultButtonText",
        color = COLOR_ROW_BG,
        hoverColor = COLOR_ROW_HOVER,
        alpha = 0.90,
        hoverAlpha = 1.0,
        borderColor = COLOR_BORDER,
        borderSize = 1,
    })
    exportButton:SetAnchor(vec.New(0.0, 0.5))
    exportButton:SetRelativePoint(vec.New(0.0, 0.5))
    exportButton:SetFontSize(8)
    exportButton:SetOnMouseUp(function()
        OpenExportModal()
    end)

    -- Close button (X)
    local closeButton = Button.New(header, vec.New(-PADDING, 0), vec.New(28, BUTTON_HEIGHT), 1i, {
        text = "X",
        panelTemplate = "DebugDarkGrey",
        textTemplate = "DefaultButtonText",
        color = vec.New(0.50, 0.20, 0.20),
        hoverColor = vec.New(0.70, 0.30, 0.30),
        alpha = 0.90,
        hoverAlpha = 1.0,
        borderColor = vec.New(0.70, 0.30, 0.30),
        borderSize = 1,
    })
    closeButton:SetAnchor(vec.New(1.0, 0.5))
    closeButton:SetRelativePoint(vec.New(1.0, 0.5))
    closeButton:SetFontSize(9)
    closeButton:SetOnMouseUp(function()
        EditMode.Exit()
    end)

    -- Body (scrollable)
    barBody = barRoot:NewPanel(vec.New(0, -HEADER_HEIGHT), vec.New(BAR_WIDTH, BODY_HEIGHT), 1i, "DebugDarkGrey")
    barBody:SetAnchor(vec.New(0.0, 1.0))
    barBody:SetRelativePoint(vec.New(0.0, 1.0))
    barBody:SetAlpha(0.0)

    bodyScroll = ScrollBox.New(barBody, vec.New(0, 0), vec.New(BODY_WIDTH, BODY_HEIGHT), 1i, {
        panelTemplate = "DebugDarkGrey",
        contentPanelTemplate = "DebugDarkGrey",
        contentSizeX = BODY_WIDTH,
        contentSizeY = 600,
        verticalScrollBar = true,
        scrollBarWidth = 8,
        alpha = 0.0,
    })
    bodyScroll:SetAnchor(vec.New(0.5, 1.0))
    bodyScroll:SetRelativePoint(vec.New(0.5, 1.0))

    contentPanel = bodyScroll.content

    -- Create category collapsibles and element rows
    local yOffset = -5
    for _, catDef in EditModeDefaults.Categories do
        local catElements = EditMode.GetElementsByCategory(catDef.id)
        local visibleCount = #catElements

        if visibleCount == 0 then
            continue
        end

        local rows = math.ceil(visibleCount / GRID_COLUMNS)
        local contentHeight = rows * GRID_CELL_HEIGHT

        local collapsible = Collapsible.New(contentPanel, vec.New(0, yOffset), BODY_WIDTH - 10, 1i, {
            title = catDef.displayName,
            expanded = true,
            bodyPadding = COLLAPSIBLE_BODY_PADDING,
            onToggle = function()
                RefreshAllCategories()
            end,
        })
        collapsible:SetBodyContentHeight(contentHeight)

        categoryCollapsibles[catDef.id] = collapsible

        -- Create element rows inside the collapsible body
        for i, element in catElements do
            local zeroIdx = i - 1
            local col = zeroIdx % GRID_COLUMNS
            local row = math.floor(zeroIdx / GRID_COLUMNS)
            local elementPosX = PADDING + col * GRID_CELL_WIDTH
            local elementPosY = -(COLLAPSIBLE_BODY_PADDING + row * GRID_CELL_HEIGHT)

            local isEnabled = EditMode.IsElementEnabled(element.id)

            local checkbox = Checkbox.New(collapsible.body, vec.New(elementPosX, elementPosY), vec.New(CHECKBOX_SIZE, CHECKBOX_SIZE), 1i, {
                backgroundTemplate = "DefaultCheckboxBackground",
                fillTemplate = "DefaultCheckboxFill",
                isChecked = isEnabled,
            })
            checkbox:SetAnchor(vec.New(0.0, 1.0))
            checkbox:SetRelativePoint(vec.New(0.0, 1.0))

            local label = collapsible.body:NewText(element.displayName, vec.New(elementPosX + CHECKBOX_SIZE + 4, elementPosY - 1), 1i, "DefaultButtonText")
            label:SetAnchor(vec.New(0.0, 1.0))
            label:SetRelativePoint(vec.New(0.0, 1.0))
            label:SetFontSize(9)
            label:SetColor(COLOR_TEXT)

            checkbox:SetOnValueChanged(function(_, checked)
                EditMode.SetElementEnabled(element.id, checked)
            end)

            elementCheckboxes[element.id] = checkbox
        end

        yOffset = yOffset + collapsible:GetTotalHeight() + CATEGORY_GAP
    end

    bodyScroll:SetContentHeight(yOffset)

    -- Footer
    local footer = barRoot:NewPanel(vec.New(0, 0), vec.New(BAR_WIDTH, FOOTER_HEIGHT), 1i, "DebugDarkGrey")
    footer:SetAnchor(vec.New(0.0, 0.0))
    footer:SetRelativePoint(vec.New(0.0, 0.0))
    footer:SetColor(COLOR_FOOTER_BG)
    footer:SetAlpha(1.0)
    footer:SetBorderColor(COLOR_BORDER)
    footer:SetBorderSize(1)

    saveButton = Button.New(footer, vec.New(-104, 0), vec.New(100, BUTTON_HEIGHT), 1i, {
        text = "Save Profile",
        panelTemplate = "DebugDarkGrey",
        textTemplate = "DefaultButtonText",
        color = vec.New(0.15, 0.35, 0.55),
        hoverColor = vec.New(0.20, 0.45, 0.70),
        alpha = 0.95,
        hoverAlpha = 1.0,
        borderColor = COLOR_ACCENT,
        borderSize = 1,
    })
    saveButton:SetAnchor(vec.New(0.5, 0.5))
    saveButton:SetRelativePoint(vec.New(0.5, 0.5))
    saveButton:SetFontSize(9)
    saveButton:SetOnMouseUp(function()
        EditMode.SaveActiveProfile()
    end)

    local resetButton = Button.New(footer, vec.New(54, 0), vec.New(200, BUTTON_HEIGHT), 1i, {
        text = "Reset Profile to Defaults",
        panelTemplate = "DebugDarkGrey",
        textTemplate = "DefaultButtonText",
        color = COLOR_ROW_BG,
        hoverColor = COLOR_ROW_HOVER,
        alpha = 0.90,
        hoverAlpha = 1.0,
        borderColor = COLOR_BORDER,
        borderSize = 1,
    })
    resetButton:SetAnchor(vec.New(0.5, 0.5))
    resetButton:SetRelativePoint(vec.New(0.5, 0.5))
    resetButton:SetFontSize(9)
    resetButton:SetOnMouseUp(function()
        EditMode.ApplyDefaultLayout()
    end)

    RefreshSaveButton()
end

local function DestroyEditModeBar()
    CloseModal()
    if barRoot ~= nil then
        UI.DestroyWidget(barRoot)
    end

    categoryCollapsibles = {}
    elementCheckboxes = {}
    profileDropdown = nil
    saveButton = nil
    bodyScroll = nil
    contentPanel = nil
    barBody = nil
    barRoot = nil
end

local function OnEditModeEnter()
    CreateEditModeBar()
end

local function OnEditModeExit()
    DestroyEditModeBar()
end

local function OnExitRequested() : boolean
    OpenDiscardChangesModal("Discard & Exit", function()
        EditMode.Exit(true)
    end, nil)
    return false
end

EditMode.RegisterOnEnter(OnEditModeEnter)
EditMode.RegisterOnExit(OnEditModeExit)
EditMode.SetExitRequestHandler(OnExitRequested)

if EditMode.IsActive() then
    OnEditModeEnter()
end
