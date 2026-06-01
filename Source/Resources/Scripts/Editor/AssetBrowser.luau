--[[
AssetBrowser — Lua port of the C++ ImGui Asset Browser. Left column is a collapsible
folder tree (lazily expanded); selecting a folder lists its files in the right column
in either a Row or thumbnail Display mode, with a search filter. Right-clicking a
.complexmodel file spawns it at the camera.

Filesystem access and model spawning go through the dev-only Asset handler. Thumbnails
reuse Panel:SetBackground, which loads a texture by path at runtime.

Layer 1100 sits above the dev top bar (1000).
]]

local ContextMenu = require("@src/API/UI/ContextMenu")
local EditorButton = require("@src/API/UI/EditorButton")
local EditorRegistry = require("@src/API/UI/EditorRegistry")
local InputBox = require("@src/API/UI/InputBox")
local ScrollBox = require("@src/API/UI/ScrollBox")

require("@src/API/UI/MenuBar")

local PANEL_W      = 500
local WINDOW_H     = 440
local TITLE_H      = 16
local PATH_H       = 16
local TOOLBAR_H    = 20
local ROW_H        = 16
local SIDE_PAD     = 6
local TOP_PAD      = 2
local FOLDER_W     = 150
local LAYER        = 1100
local BTN_TEXT_TPL = "DevToolsText"

local CELL_W   = 56
local CELL_H   = CELL_W + 14
local CELL_GAP = 4
local MAX_FILES = 400
local MODEL_EXT = ".complexmodel"

local WINDOW_BG_COLOR = vector.create(0.10, 0.10, 0.12)
local WINDOW_BG_ALPHA = 0.96
local BORDER_COLOR    = vector.create(0.30, 0.30, 0.35)
local BORDER_SIZE     = 1
local LABEL_TINT      = vector.create(0.82, 0.82, 0.88)

local FILES_X = SIDE_PAD + FOLDER_W + SIDE_PAD
local FILES_W = PANEL_W - FILES_X - SIDE_PAD
local BODY_TOP = TITLE_H + PATH_H + TOP_PAD
local FOLDER_LIST_H = WINDOW_H - BODY_TOP - SIDE_PAD
local FILES_LIST_H = WINDOW_H - BODY_TOP - TOOLBAR_H - 4 - SIDE_PAD

local function MakeBtnOpts(text)
    return {
        text = text,
        textTemplate = BTN_TEXT_TPL,
        borderColor = BORDER_COLOR,
        borderSize  = BORDER_SIZE,
    }
end

local canvas = UI.GetCanvas("AssetBrowserEditor", 0, 0, 1920, 1080, false)

local windowPanel = canvas:NewPanel(560, -120, PANEL_W, WINDOW_H, LAYER, "DebugDarkGrey")
windowPanel:SetAnchor(0.0, 1.0)
windowPanel:SetRelativePoint(0.0, 1.0)
windowPanel:SetVisible(false)
windowPanel:SetColor(WINDOW_BG_COLOR)
windowPanel:SetAlpha(WINDOW_BG_ALPHA)
windowPanel:SetBorderColor(BORDER_COLOR)
windowPanel:SetBorderSize(BORDER_SIZE)

local titleBar = windowPanel:NewPanel(0, 0, PANEL_W, TITLE_H, 1, "DebugDarkGrey")
titleBar:SetAnchor(0.0, 1.0)
titleBar:SetRelativePoint(0.0, 1.0)
titleBar:SetColor(vector.create(0.06, 0.06, 0.08))
titleBar:SetAlpha(WINDOW_BG_ALPHA)

local titleText = titleBar:NewText("Asset Browser", SIDE_PAD, 0, 1, BTN_TEXT_TPL)
titleText:SetAnchor(0.0, 0.5)
titleText:SetRelativePoint(0.0, 0.5)
titleText:SetInteractable(false)

local CLOSE_BTN_SIZE = 12
local closeBtn = EditorButton.NewEditorButton(titleBar, -2, 0, CLOSE_BTN_SIZE, CLOSE_BTN_SIZE, 1, MakeBtnOpts("X"))
closeBtn:SetAnchor(1.0, 0.5)
closeBtn:SetRelativePoint(1.0, 0.5)
closeBtn.text:SetPosX(1)

windowPanel:MakeDraggable(titleBar)

-- ---- path bar ----
local pathText = windowPanel:NewText("Data/", SIDE_PAD, -(TITLE_H + 2), 1, BTN_TEXT_TPL)
pathText:SetAnchor(0.0, 1.0)
pathText:SetRelativePoint(0.0, 1.0)
pathText:SetColor(LABEL_TINT)
pathText:SetInteractable(false)

-- ---- folder list (left) ----
local folderList = ScrollBox.NewScrollBox(windowPanel, SIDE_PAD, -BODY_TOP, FOLDER_W, FOLDER_LIST_H, 1, {
    panelTemplate = "DebugDarkGrey",
    contentSizeX = FOLDER_W,
    contentSizeY = FOLDER_LIST_H,
    verticalScrollBar = true,
    scrollBarWidth = 10,
    alpha = 0.0,
})
folderList:SetAnchor(0.0, 1.0)
folderList:SetRelativePoint(0.0, 1.0)

-- ---- file toolbar (right) ----
local rowModeBtn = EditorButton.NewEditorButton(windowPanel, FILES_X, -BODY_TOP, 44, TOOLBAR_H, 1, MakeBtnOpts("Row"))
rowModeBtn:SetAnchor(0.0, 1.0)
rowModeBtn:SetRelativePoint(0.0, 1.0)

local displayModeBtn = EditorButton.NewEditorButton(windowPanel, FILES_X + 48, -BODY_TOP, 60, TOOLBAR_H, 1, MakeBtnOpts("Display"))
displayModeBtn:SetAnchor(0.0, 1.0)
displayModeBtn:SetRelativePoint(0.0, 1.0)

local SEARCH_X = FILES_X + 112
local searchInput = InputBox.NewInputBox(windowPanel, SEARCH_X, -BODY_TOP, FILES_X + FILES_W - SEARCH_X, TOOLBAR_H, 1, {
    panelTemplate = "DebugDarkGrey",
    textTemplate = BTN_TEXT_TPL,
    defaultText = "",
})
searchInput:SetAnchor(0.0, 1.0)
searchInput:SetRelativePoint(0.0, 1.0)

-- ---- file list (right) ----
local fileList = ScrollBox.NewScrollBox(windowPanel, FILES_X, -(BODY_TOP + TOOLBAR_H + 4), FILES_W, FILES_LIST_H, 1, {
    panelTemplate = "DebugDarkGrey",
    contentSizeX = FILES_W,
    contentSizeY = FILES_LIST_H,
    verticalScrollBar = true,
    scrollBarWidth = 10,
    alpha = 0.0,
})
fileList:SetAnchor(0.0, 1.0)
fileList:SetRelativePoint(0.0, 1.0)

-- Right-click context menu for files (built per right-click in OpenFileMenu).
local fileMenu = ContextMenu.New(canvas, { items = {}, layer = LAYER + 100 })

local selectedPath = ""
local displayMode = "row"
local listing = nil
local fileItems = {}

-- Folder tree (left): a synthetic "Data" root whose children are lazily listed via
-- Asset.ListDir on first expand. treeWidgets holds the rendered rows for teardown.
local treeRoot = nil
local treeWidgets = {}
local treeRows = {}
local TREE_INDENT = 12
local CHEVRON_W   = 14
local BuildTree, SelectFolder, ToggleNode

local function ContainsCI(haystack, needle)
    if needle == nil or #needle == 0 then return true end
    return string.find(string.lower(haystack), string.lower(needle), 1, true) ~= nil
end

local function Truncate(name, maxChars)
    if utf8.len(name) > maxChars then
        return name:sub(1, maxChars - 1) .. "."
    end
    return name
end

local function ParentPath(path)
    return path:match("^(.*)/[^/]*$") or ""
end

local function RefreshModeButtons()
    rowModeBtn:SetHighlighted(displayMode == "row")
    displayModeBtn:SetHighlighted(displayMode == "display")
end

local function OpenFileMenu(file, mouseX, mouseY)
    -- Only models have an action right now, so only they get a menu.
    if file.ext ~= MODEL_EXT then return end
    fileMenu:SetItems({
        { label = "Spawn at camera", onClick = function() Asset.SpawnModel(file.path) end },
    })
    fileMenu:Open(mouseX, mouseY)
end

local function FilteredFiles()
    local out = {}
    if listing == nil then return out end
    local search = searchInput.text
    for _, file in listing.files do
        if ContainsCI(file.name, search) then
            table.insert(out, file)
            if #out >= MAX_FILES then break end
        end
    end
    return out
end

local function FilteredFolders()
    local out = {}
    if listing == nil or listing.folders == nil then return out end
    local search = searchInput.text
    for _, folder in listing.folders do
        if ContainsCI(folder.name, search) then
            table.insert(out, folder)
        end
    end
    return out
end

-- Right panel rows: subdirectories first (click to navigate in), then files.
local function BuildRows(folders, files)
    local content = fileList.content
    local index = 0

    for _, folder in folders do
        local target = folder.path
        local text = if folder.name == ".." then ".." else folder.name .. "/"
        local btn = EditorButton.NewEditorButton(content, 2, -(index * ROW_H) - 1, FILES_W - 14, ROW_H, 1, {
            text = text,
            textTemplate = BTN_TEXT_TPL,
            textAlign = "left",
            textPadX = 4,
        })
        btn:SetAnchor(0.0, 1.0)
        btn:SetRelativePoint(0.0, 1.0)
        btn.panel:SetOnMouseUp(function(_, _, button)
            if button == 0 then SelectFolder(target) end
        end)
        table.insert(fileItems, btn.panel)
        index += 1
    end

    for _, file in files do
        local btn = EditorButton.NewEditorButton(content, 2, -(index * ROW_H) - 1, FILES_W - 14, ROW_H, 1, {
            text = file.name,
            textTemplate = BTN_TEXT_TPL,
            textAlign = "left",
            textPadX = 4,
        })
        btn:SetAnchor(0.0, 1.0)
        btn:SetRelativePoint(0.0, 1.0)
        btn.panel:SetOnMouseUp(function(_, _, button, mouseX, mouseY)
            if button == 1 then OpenFileMenu(file, mouseX, mouseY) end
        end)
        if file.ext == MODEL_EXT then
            btn.panel:SetOnMouseDown(function(_, _, button)
                if button == 0 then Asset.BeginDragSpawn(file.path) end
            end)
        end
        table.insert(fileItems, btn.panel)
        index += 1
    end

    fileList:SetContentHeight(math.max(FILES_LIST_H, index * ROW_H + 2))
end

-- Right panel grid: folder cells first (click to navigate in), then file cells.
local function BuildDisplay(folders, files)
    local content = fileList.content
    local cols = math.max(1, math.floor(FILES_W / (CELL_W + CELL_GAP)))
    local index = 0

    for _, folder in folders do
        local col = index % cols
        local row = math.floor(index / cols)
        local x = 2 + col * (CELL_W + CELL_GAP)
        local y = -(row * (CELL_H + CELL_GAP)) - 1

        local cell = content:NewPanel(x, y, CELL_W, CELL_H, 1, "DebugDarkGrey")
        cell:SetAnchor(0.0, 1.0)
        cell:SetRelativePoint(0.0, 1.0)
        cell:SetAlpha(0.0)

        local thumb = cell:NewPanel(0, 0, CELL_W, CELL_W, 1, "DebugDarkGrey")
        thumb:SetAnchor(0.5, 1.0)
        thumb:SetRelativePoint(0.5, 1.0)
        thumb:SetColor(vector.create(0.40, 0.36, 0.20))
        thumb:SetAlpha(1.0)
        local target = folder.path
        thumb:SetOnMouseUp(function(_, _, button)
            if button == 0 then SelectFolder(target) end
        end)

        local label = cell:NewText(Truncate(folder.name, 9), 0, -(CELL_W + 1), 1, BTN_TEXT_TPL)
        label:SetAnchor(0.5, 1.0)
        label:SetRelativePoint(0.5, 1.0)
        label:SetColor(LABEL_TINT)
        label:SetInteractable(false)

        table.insert(fileItems, cell)
        index += 1
    end

    for _, file in files do
        local col = index % cols
        local row = math.floor(index / cols)
        local x = 2 + col * (CELL_W + CELL_GAP)
        local y = -(row * (CELL_H + CELL_GAP)) - 1

        local cell = content:NewPanel(x, y, CELL_W, CELL_H, 1, "DebugDarkGrey")
        cell:SetAnchor(0.0, 1.0)
        cell:SetRelativePoint(0.0, 1.0)
        cell:SetAlpha(0.0)

        local thumb = cell:NewPanel(0, 0, CELL_W, CELL_W, 1, "DebugDarkGrey")
        thumb:SetAnchor(0.5, 1.0)
        thumb:SetRelativePoint(0.5, 1.0)
        if file.ext == ".dds" then
            thumb:SetBackground("Data/" .. file.path)
            thumb:SetColor(vector.create(1, 1, 1))
            thumb:SetAlpha(1.0)
        else
            thumb:SetColor(vector.create(0.25, 0.25, 0.30))
            thumb:SetAlpha(1.0)
        end
        thumb:SetOnMouseUp(function(_, _, button, mouseX, mouseY)
            if button == 1 then OpenFileMenu(file, mouseX, mouseY) end
        end)
        if file.ext == MODEL_EXT then
            thumb:SetOnMouseDown(function(_, _, button)
                if button == 0 then Asset.BeginDragSpawn(file.path) end
            end)
        end

        local label = cell:NewText(Truncate(file.name, 9), 0, -(CELL_W + 1), 1, BTN_TEXT_TPL)
        label:SetAnchor(0.5, 1.0)
        label:SetRelativePoint(0.5, 1.0)
        label:SetColor(LABEL_TINT)
        label:SetInteractable(false)

        table.insert(fileItems, cell)
        index += 1
    end

    local rows = math.ceil(index / cols)
    fileList:SetContentHeight(math.max(FILES_LIST_H, rows * (CELL_H + CELL_GAP) + 2))
end

local function BuildFiles()
    for _, w in fileItems do UI.DestroyWidget(w) end
    fileItems = {}

    local folders = FilteredFolders()
    -- "Up one level" entry at the very top (unless we're at the Data root). Modelled as a folder so
    -- it goes through the same navigate-on-click path; always present, even while searching.
    if selectedPath ~= "" then
        table.insert(folders, 1, { name = "..", path = ParentPath(selectedPath) })
    end

    local files = FilteredFiles()
    if displayMode == "display" then
        BuildDisplay(folders, files)
    else
        BuildRows(folders, files)
    end
end

local function MakeNode(folder, depth)
    return {
        name = folder.name,
        path = folder.path,
        fileCount = folder.fileCount,
        depth = depth,
        expanded = false,
        loaded = false,
        hasChildren = (folder.folderCount or 0) > 0, -- known upfront, so the chevron is accurate
        children = {},
    }
end

local function EnsureChildrenLoaded(node)
    if node.loaded then return end
    local sub = Asset.ListDir(node.path)
    node.children = {}
    if sub ~= nil then
        if sub.folders ~= nil then
            for _, folder in sub.folders do
                table.insert(node.children, MakeNode(folder, node.depth + 1))
            end
        end
        if sub.files ~= nil then
            node.fileCount = #sub.files
        end
    end
    node.hasChildren = #node.children > 0
    node.loaded = true
end

local function CollectVisible(nodes, out)
    for _, node in nodes do
        table.insert(out, node)
        if node.expanded and node.hasChildren then
            CollectVisible(node.children, out)
        end
    end
end

local function HighlightTree()
    for _, row in treeRows do
        row.nameBtn:SetHighlighted(row.node.path == selectedPath)
    end
end

function BuildTree()
    for _, w in treeWidgets do UI.DestroyWidget(w) end
    treeWidgets = {}
    treeRows = {}

    local content = folderList.content
    local visible = {}
    if treeRoot ~= nil then CollectVisible({ treeRoot }, visible) end

    for i, node in visible do
        local y = -((i - 1) * ROW_H) - 1
        local indent = node.depth * TREE_INDENT

        -- Only folders that actually have subfolders get an expand chevron.
        if node.hasChildren then
            local chevron = EditorButton.NewEditorButton(content, 2 + indent, y, CHEVRON_W, ROW_H, 1, {
                text = node.expanded and "v" or ">",
                textTemplate = BTN_TEXT_TPL,
            })
            chevron:SetAnchor(0.0, 1.0)
            chevron:SetRelativePoint(0.0, 1.0)
            chevron:SetOnMouseUp(function() ToggleNode(node) end)
            table.insert(treeWidgets, chevron.panel)
        end

        -- Hide the "(0)" suffix for empty folders; show "(N)" only when there are files.
        local label = node.name
        if node.fileCount and node.fileCount > 0 then
            label = node.name .. " (" .. node.fileCount .. ")"
        end

        local nameX = 2 + indent + CHEVRON_W
        local nameW = math.max(24, FOLDER_W - 14 - indent - CHEVRON_W)
        local nameBtn = EditorButton.NewEditorButton(content, nameX, y, nameW, ROW_H, 1, {
            text = label,
            textTemplate = BTN_TEXT_TPL,
            textAlign = "left",
            textPadX = 4,
        })
        nameBtn:SetAnchor(0.0, 1.0)
        nameBtn:SetRelativePoint(0.0, 1.0)
        nameBtn:SetHighlighted(node.path == selectedPath)
        nameBtn:SetOnMouseUp(function() SelectFolder(node.path) end)
        table.insert(treeWidgets, nameBtn.panel)
        table.insert(treeRows, { node = node, nameBtn = nameBtn })
    end

    folderList:SetContentHeight(math.max(FOLDER_LIST_H, #visible * ROW_H + 2))
end

function ToggleNode(node)
    if node.expanded then
        node.expanded = false
    else
        EnsureChildrenLoaded(node)
        node.expanded = true
    end
    BuildTree()
end

function SelectFolder(path)
    selectedPath = path
    listing = Asset.ListDir(path)
    pathText:SetText("Data/" .. path)
    BuildFiles()
    HighlightTree()
end

local function LoadTree()
    treeRoot = {
        name = "Data",
        path = "",
        fileCount = 0,
        depth = 0,
        expanded = true,
        loaded = false,
        hasChildren = true,
        children = {},
    }
    EnsureChildrenLoaded(treeRoot)
    treeRoot.expanded = true
    BuildTree()
end

rowModeBtn:SetOnMouseUp(function()
    displayMode = "row"
    RefreshModeButtons()
    BuildFiles()
end)

displayModeBtn:SetOnMouseUp(function()
    displayMode = "display"
    RefreshModeButtons()
    BuildFiles()
end)

searchInput:SetOnTextChanged(function()
    BuildFiles()
end)

RefreshModeButtons()

local methods = {}

function methods:Show()
    windowPanel:SetVisible(true)
    LoadTree()
    SelectFolder("")
end

function methods:Hide()
    windowPanel:SetVisible(false)
    fileMenu:Close()
end

function methods:IsVisible()
    return windowPanel:IsVisible()
end

closeBtn:SetOnMouseUp(function() methods:Hide() end)

EditorRegistry.Register("AssetBrowser", methods)
