local EditorWindow = require("@src/API/Editor/UI/Window")
--[[
AssetBrowser - Lua port of the C++ ImGui Asset Browser. Left column is a collapsible
folder tree (lazily expanded); selecting a folder lists its files in the right column
in either a Row or thumbnail Display mode, with a search filter. Right-clicking a
.complexmodel file spawns it at the camera.

Filesystem access and model spawning go through the dev-only Asset handler. Thumbnails
reuse Panel:SetBackground, which loads a texture by path at runtime.

Layer 1100 sits above the dev top bar (1000).
]]

local ContextMenu = require("@src/API/UI/ContextMenu")
local Button = require("@src/API/UI/Button")
local EditorRegistry = require("@src/API/Editor/Registry")
local InputBox = require("@src/API/UI/InputBox")
local ScrollBox = require("@src/API/UI/ScrollBox")
local TextUtil = require("@src/API/UI/TextUtil")


local PANEL_W      = 500
local WINDOW_H     = 440
local TITLE_H      = 16
local PATH_H       = 16
local TOOLBAR_H    = 20
local ROW_H        = 16
local SIDE_PAD     = 6
local TOP_PAD      = 2
local FOLDER_W     = 150
local LAYER        = 1100i
local BTN_TEXT_TPL = "DevToolsText"

local CELL_W   = 56
local CELL_H   = CELL_W + 14
local CELL_GAP = 4
local MAX_FILES = 400
local MODEL_EXT = ".complexmodel"

local WINDOW_BG_COLOR = vec.New(0.10, 0.10, 0.12)
local WINDOW_BG_ALPHA = 0.96
local BORDER_COLOR    = vec.New(0.30, 0.30, 0.35)
local BORDER_SIZE     = 1
local LABEL_TINT      = vec.New(0.82, 0.82, 0.88)
local ROW_COLOR       = vec.New(0.095, 0.105, 0.12)
local ROW_HOVER_COLOR = vec.New(0.13, 0.20, 0.29)

local FILES_X = SIDE_PAD + FOLDER_W + SIDE_PAD
local FILES_W = PANEL_W - FILES_X - SIDE_PAD
local BODY_TOP = TITLE_H + PATH_H + TOP_PAD
local FOLDER_LIST_H = WINDOW_H - BODY_TOP - SIDE_PAD
local FILES_LIST_H = WINDOW_H - BODY_TOP - TOOLBAR_H - 4 - SIDE_PAD

UI.RegisterPanelTemplate("AssetBrowserRowPanel",
{
    color = ROW_COLOR,
    alpha = 1.0,
    cornerRadius = 2.0,
    borderSize = 0.0,
    onClickTemplate = "DefaultButtonDownPanel",
    onHoverTemplate = "DefaultButtonHoveredPanel",
    onUninteractableTemplate = "DefaultButtonDisabledPanel",
})

UI.RegisterPanelTemplate("AssetBrowserCellPanel",
{
    alpha = 0.0,
    cornerRadius = 0.0,
    borderSize = 0.0,
})

UI.RegisterPanelTemplate("AssetBrowserThumbnailPanel",
{
    color = vec.New(1.0, 1.0, 1.0),
    alpha = 1.0,
    cornerRadius = 0.0,
    borderSize = 0.0,
})

local function MakeBtnOpts(text)
    return {
        text = text,
        textTemplate = BTN_TEXT_TPL,
        borderColor = BORDER_COLOR,
        borderSize  = BORDER_SIZE,
    }
end

local canvas = UI.GetCanvas("AssetBrowserEditor", vec.New(0, 0), vec.New(1920, 1080), false)

local editorWindow = EditorWindow.New(canvas, vec.New(560, -120), vec.New(PANEL_W, WINDOW_H), LAYER, { title = "Asset Browser" })
local windowPanel = editorWindow.root
local titleBar = editorWindow.titleBar
local titleText = editorWindow.title
local closeBtn = editorWindow.closeButton

-- ---- path bar ----
local pathText = windowPanel:NewText("Data/", vec.New(SIDE_PAD, -(TITLE_H + 2)), 1i, BTN_TEXT_TPL)
pathText:SetAnchor(vec.New(0.0, 1.0))
pathText:SetRelativePoint(vec.New(0.0, 1.0))
pathText:SetColor(LABEL_TINT)
pathText:SetInteractable(false)

-- ---- folder list (left) ----
local folderList = ScrollBox.New(windowPanel, vec.New(SIDE_PAD, -BODY_TOP), vec.New(FOLDER_W, FOLDER_LIST_H), 1i, {
    panelTemplate = "DebugDarkGrey",
    contentSizeX = FOLDER_W,
    contentSizeY = FOLDER_LIST_H,
    verticalScrollBar = true,
    scrollBarWidth = 10,
    alpha = 0.0,
})
folderList:SetAnchor(vec.New(0.0, 1.0))
folderList:SetRelativePoint(vec.New(0.0, 1.0))

-- ---- file toolbar (right) ----
local rowModeBtn = Button.New(windowPanel, vec.New(FILES_X, -BODY_TOP), vec.New(44, TOOLBAR_H), 1i, MakeBtnOpts("Row"))
rowModeBtn:SetAnchor(vec.New(0.0, 1.0))
rowModeBtn:SetRelativePoint(vec.New(0.0, 1.0))

local displayModeBtn = Button.New(windowPanel, vec.New(FILES_X + 48, -BODY_TOP), vec.New(60, TOOLBAR_H), 1i, MakeBtnOpts("Display"))
displayModeBtn:SetAnchor(vec.New(0.0, 1.0))
displayModeBtn:SetRelativePoint(vec.New(0.0, 1.0))

local SEARCH_X = FILES_X + 112
local searchInput = InputBox.New(windowPanel, vec.New(SEARCH_X, -BODY_TOP), vec.New(FILES_X + FILES_W - SEARCH_X, TOOLBAR_H), 1i, {
    panelTemplate = "DebugDarkGrey",
    textTemplate = BTN_TEXT_TPL,
    defaultText = "",
})
searchInput:SetAnchor(vec.New(0.0, 1.0))
searchInput:SetRelativePoint(vec.New(0.0, 1.0))

-- ---- file list (right) ----
local fileList = ScrollBox.New(windowPanel, vec.New(FILES_X, -(BODY_TOP + TOOLBAR_H + 4)), vec.New(FILES_W, FILES_LIST_H), 1i, {
    panelTemplate = "DebugDarkGrey",
    contentSizeX = FILES_W,
    contentSizeY = FILES_LIST_H,
    verticalScrollBar = true,
    scrollBarWidth = 10,
    alpha = 0.0,
})
fileList:SetAnchor(vec.New(0.0, 1.0))
fileList:SetRelativePoint(vec.New(0.0, 1.0))

-- Right-click context menu for files (built per right-click in OpenFileMenu).
local fileMenu = ContextMenu.New(canvas, { items = {}, layer = integer.add(LAYER, 100i) })

local selectedPath = ""
local pickerCallback : ((string) -> ())? = nil
local pickerExtensions : { [string] : boolean }? = nil
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
local methods = {}

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
        if (pickerExtensions == nil or pickerExtensions[file.ext]) and TextUtil.ContainsIgnoreCase(file.name, search) then
            table.insert(out, file)
            if #out >= MAX_FILES then break end
        end
    end
    return out
end

local function SelectFile(file, button, mouseX, mouseY)
    if button == 0i and pickerCallback ~= nil then
        local callback = pickerCallback
        pickerCallback = nil
        pickerExtensions = nil
        methods:Hide()
        callback(file.path)
    elseif button == 1i then
        OpenFileMenu(file, mouseX, mouseY)
    end
end

local function FilteredFolders()
    local out = {}
    if listing == nil or listing.folders == nil then return out end
    local search = searchInput.text
    for _, folder in listing.folders do
        if TextUtil.ContainsIgnoreCase(folder.name, search) then
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
        local btn = Button.New(content, vec.New(2, -(index * ROW_H) - 1), vec.New(FILES_W - 14, ROW_H), 1i, {
            text = text,
            panelTemplate = "AssetBrowserRowPanel",
            textTemplate = BTN_TEXT_TPL,
            textAlign = "left",
            textPadX = 4,
            color = ROW_COLOR,
            hoverColor = ROW_HOVER_COLOR,
            highlightColor = ROW_HOVER_COLOR,
            alpha = 1.0,
            hoverAlpha = 1.0,
            highlightAlpha = 1.0,
            borderSize = 0,
        })
        btn:SetAnchor(vec.New(0.0, 1.0))
        btn:SetRelativePoint(vec.New(0.0, 1.0))
        btn.root:SetOnMouseUp(function(_, _, button)
            if button == 0i then SelectFolder(target) end
        end)
        table.insert(fileItems, btn.root)
        index += 1
    end

    for _, file in files do
        local btn = Button.New(content, vec.New(2, -(index * ROW_H) - 1), vec.New(FILES_W - 14, ROW_H), 1i, {
            text = file.name,
            panelTemplate = "AssetBrowserRowPanel",
            textTemplate = BTN_TEXT_TPL,
            textAlign = "left",
            textPadX = 4,
            color = ROW_COLOR,
            hoverColor = ROW_HOVER_COLOR,
            highlightColor = ROW_HOVER_COLOR,
            alpha = 1.0,
            hoverAlpha = 1.0,
            highlightAlpha = 1.0,
            borderSize = 0,
        })
        btn:SetAnchor(vec.New(0.0, 1.0))
        btn:SetRelativePoint(vec.New(0.0, 1.0))
        btn.root:SetOnMouseUp(function(_, _, button, mouseX, mouseY)
            SelectFile(file, button, mouseX, mouseY)
        end)
        if file.ext == MODEL_EXT then
            btn.root:SetOnMouseDown(function(_, _, button)
                if button == 0i then Asset.BeginDragSpawn(file.path) end
            end)
        end
        table.insert(fileItems, btn.root)
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

        local cell = content:NewPanel(vec.New(x, y), vec.New(CELL_W, CELL_H), 1i, "AssetBrowserCellPanel")
        cell:SetAnchor(vec.New(0.0, 1.0))
        cell:SetRelativePoint(vec.New(0.0, 1.0))
        cell:SetAlpha(0.0)

        local thumb = cell:NewPanel(vec.New(0, 0), vec.New(CELL_W, CELL_W), 1i, "AssetBrowserThumbnailPanel")
        thumb:SetAnchor(vec.New(0.5, 1.0))
        thumb:SetRelativePoint(vec.New(0.5, 1.0))
        thumb:SetColor(vec.New(0.40, 0.36, 0.20))
        thumb:SetAlpha(1.0)
        local target = folder.path
        thumb:SetOnMouseUp(function(_, _, button)
            if button == 0i then SelectFolder(target) end
        end)

        local label = cell:NewText(TextUtil.Truncate(folder.name, 9), vec.New(0, -(CELL_W + 1)), 1i, BTN_TEXT_TPL)
        label:SetAnchor(vec.New(0.5, 1.0))
        label:SetRelativePoint(vec.New(0.5, 1.0))
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

        local cell = content:NewPanel(vec.New(x, y), vec.New(CELL_W, CELL_H), 1i, "AssetBrowserCellPanel")
        cell:SetAnchor(vec.New(0.0, 1.0))
        cell:SetRelativePoint(vec.New(0.0, 1.0))
        cell:SetAlpha(0.0)

        local thumb = cell:NewPanel(vec.New(0, 0), vec.New(CELL_W, CELL_W), 1i, "AssetBrowserThumbnailPanel")
        thumb:SetAnchor(vec.New(0.5, 1.0))
        thumb:SetRelativePoint(vec.New(0.5, 1.0))
        if file.ext == ".dds" or file.ext == ".png" then
            thumb:SetBackground("Data/" .. file.path)
            thumb:SetColor(vec.New(1, 1, 1))
            thumb:SetAlpha(1.0)
        else
            thumb:SetColor(vec.New(0.25, 0.25, 0.30))
            thumb:SetAlpha(1.0)
        end
        thumb:SetOnMouseUp(function(_, _, button, mouseX, mouseY)
            SelectFile(file, button, mouseX, mouseY)
        end)
        if file.ext == MODEL_EXT then
            thumb:SetOnMouseDown(function(_, _, button)
                if button == 0i then Asset.BeginDragSpawn(file.path) end
            end)
        end

        local label = cell:NewText(TextUtil.Truncate(file.name, 9), vec.New(0, -(CELL_W + 1)), 1i, BTN_TEXT_TPL)
        label:SetAnchor(vec.New(0.5, 1.0))
        label:SetRelativePoint(vec.New(0.5, 1.0))
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
        fileCount = integer.tonumber(folder.fileCount),
        depth = depth,
        expanded = false,
        loaded = false,
        hasChildren = folder.folderCount ~= 0i, -- known upfront, so the chevron is accurate
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
            local chevron = Button.New(content, vec.New(2 + indent, y), vec.New(CHEVRON_W, ROW_H), 1i, {
                text = node.expanded and "v" or ">",
                panelTemplate = "AssetBrowserRowPanel",
                textTemplate = BTN_TEXT_TPL,
                color = ROW_COLOR,
                hoverColor = ROW_HOVER_COLOR,
                highlightColor = ROW_HOVER_COLOR,
                alpha = 1.0,
                hoverAlpha = 1.0,
                highlightAlpha = 1.0,
                borderSize = 0,
            })
            chevron:SetAnchor(vec.New(0.0, 1.0))
            chevron:SetRelativePoint(vec.New(0.0, 1.0))
            chevron:SetOnMouseUp(function() ToggleNode(node) end)
            table.insert(treeWidgets, chevron.root)
        end

        -- Hide the "(0)" suffix for empty folders; show "(N)" only when there are files.
        local label = node.name
        if node.fileCount and node.fileCount > 0 then
            label = node.name .. " (" .. node.fileCount .. ")"
        end

        local nameX = 2 + indent + CHEVRON_W
        local nameW = math.max(24, FOLDER_W - 14 - indent - CHEVRON_W)
        local nameBtn = Button.New(content, vec.New(nameX, y), vec.New(nameW, ROW_H), 1i, {
            text = label,
            panelTemplate = "AssetBrowserRowPanel",
            textTemplate = BTN_TEXT_TPL,
            textAlign = "left",
            textPadX = 4,
            color = ROW_COLOR,
            hoverColor = ROW_HOVER_COLOR,
            highlightColor = ROW_HOVER_COLOR,
            alpha = 1.0,
            hoverAlpha = 1.0,
            highlightAlpha = 1.0,
            borderSize = 0,
        })
        nameBtn:SetAnchor(vec.New(0.0, 1.0))
        nameBtn:SetRelativePoint(vec.New(0.0, 1.0))
        nameBtn:SetHighlighted(node.path == selectedPath)
        nameBtn:SetOnMouseUp(function() SelectFolder(node.path) end)
        table.insert(treeWidgets, nameBtn.root)
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

function methods:Show()
    pickerCallback = nil
    pickerExtensions = nil
    windowPanel:SetVisible(true)
    LoadTree()
    SelectFolder("")
end

function methods:Pick(startPath : string, extensions : { string }?, callback : (string) -> ())
    pickerCallback = callback
    pickerExtensions = nil
    if extensions ~= nil then
        pickerExtensions = { }
        for _, extension in extensions do
            pickerExtensions[extension] = true
        end
    end
    windowPanel:SetVisible(true)
    LoadTree()
    SelectFolder(startPath)
end

function methods:Hide()
    pickerCallback = nil
    pickerExtensions = nil
    windowPanel:SetVisible(false)
    fileMenu:Close()
end

function methods:IsVisible()
    return windowPanel:IsVisible()
end

closeBtn:SetOnMouseUp(function() methods:Hide() end)

EditorRegistry.Register("AssetBrowser", methods)
