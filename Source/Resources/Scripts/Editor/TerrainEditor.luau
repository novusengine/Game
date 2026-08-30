local Button = require("@src/API/UI/Button");
local ContextMenu = require("@src/API/UI/ContextMenu");
local EditorRegistry = require("@src/API/Editor/Registry");
local InputField = require("@src/API/Editor/UI/InputField");
local EditorWindow = require("@src/API/Editor/UI/Window");
local InputAPI = require("@src/API/Input/Input");
local Modal = require("@src/API/UI/Modal");
local UIHorizontalLayout = require("@src/API/UI/HorizontalLayout");
local Slider = require("@src/API/UI/Slider");
local Terrain = require("@src/API/Editor/Terrain");
local TerrainBrushes = require("@src/API/Editor/TerrainBrushes");
local Tooltip = require("@src/API/UI/Tooltip");
local UIVerticalLayout = require("@src/API/UI/VerticalLayout");

type Brush = TerrainBrushes.Brush;
type BrushContext = TerrainBrushes.Context;
type StrokeState = TerrainBrushes.StrokeState;
type TextureLayer = Terrain.TextureLayer;
type QuickTextureEntry = { path : string?, preview : Panel, weightText : TextWidget };

TerrainBrushes.Register(require("@src/API/Editor/TerrainBrushes/RaiseLower"));
TerrainBrushes.Register(require("@src/API/Editor/TerrainBrushes/Flatten"));
TerrainBrushes.Register(require("@src/API/Editor/TerrainBrushes/Smooth"));
TerrainBrushes.Register(require("@src/API/Editor/TerrainBrushes/TexturePaint"));
TerrainBrushes.Register(require("@src/API/Editor/TerrainBrushes/VertexPaint"));

local CONTEXT_NAME = "TerrainEditor";
local PANEL_WIDTH = 300;
local TITLE_HEIGHT = 20;
local LAYER = 1200i;
local TerrainEditor : any = { };

UI.RegisterPanelTemplate("TerrainTexturePreview",
{
    color = vec.New(0.12, 0.13, 0.15),
    alpha = 1.0,
    cornerRadius = 2.0,
    borderColor = vec.New(0.35, 0.38, 0.42),
    borderSize = 1.0,
});

local settings =
{
    radius = 12.0,
    strength = 10.0,
    hardness = 0.5,
    opacity = 1.0,
    vertexRed = 255.0,
    vertexGreen = 255.0,
    vertexBlue = 255.0,
};

TerrainEditor.defaultBrush = TerrainBrushes.Get("raise_lower");
if TerrainEditor.defaultBrush == nil then error("TerrainEditor: the default raise/lower brush is not registered"); end
TerrainEditor.selectedBrush = TerrainEditor.defaultBrush;
TerrainEditor.sculptBrush = TerrainEditor.defaultBrush;
TerrainEditor.textureBrush = TerrainBrushes.Get("texture_paint");
if TerrainEditor.textureBrush == nil then error("TerrainEditor: the texture paint brush is not registered"); end
TerrainEditor.paintBrush = TerrainEditor.textureBrush;
TerrainEditor.vertexColorBrush = TerrainBrushes.Get("vertex_paint");
if TerrainEditor.vertexColorBrush == nil then error("TerrainEditor: the vertex paint brush is not registered"); end
TerrainEditor.vertexBrush = TerrainEditor.vertexColorBrush;
TerrainEditor.editMode = "sculpt";
TerrainEditor.selectedTexturePath = nil;
TerrainEditor.paintTargetLayer = 0i;
TerrainEditor.recentTexturePaths = { };
TerrainEditor.cursorLayers = { };
TerrainEditor.activeStroke = nil;
TerrainEditor.lastBrushContext = nil;
TerrainEditor.saveStatus = nil;
TerrainEditor.previousSculptDown = false;
TerrainEditor.previousPickDown = false;
TerrainEditor.layerPickPending = false;
TerrainEditor.layerPickLayers = { };
TerrainEditor.brushAdjustmentArmedModifier = nil;
TerrainEditor.brushAdjustmentActiveMode = nil;
TerrainEditor.brushAdjustmentMovement = vec.New(0, 0);
TerrainEditor.brushAdjustmentStartRadius = settings.radius;
TerrainEditor.brushAdjustmentStartStrength = settings.strength;
TerrainEditor.brushAdjustmentStartHardness = settings.hardness;
TerrainEditor.brushAdjustmentCancelRequested = false;
TerrainEditor.quickPickPending = false;
TerrainEditor.quickPickOpenRequested = false;
TerrainEditor.quickPickLayers = { };
TerrainEditor.quickPickMousePosition = vec.New(0, 0);
TerrainEditor.quickPickPositionInitialized = false;
TerrainEditor.modeEnabled = false;
TerrainEditor.textureText = nil;
TerrainEditor.SelectPaintTexture = nil;
TerrainEditor.RefreshCursorLayerButtons = function() end;

local canvas = UI.GetCanvas("TerrainEditor", vec.New(0, 0), vec.New(1920, 1080), false);
local window = EditorWindow.New(canvas, vec.New(1580, -70), vec.New(PANEL_WIDTH, 690), LAYER, { title = "Terrain Editor", titleHeight = TITLE_HEIGHT });
local root = window.root;
root:SetVisible(false);

local content = UIVerticalLayout.New(root, vec.New(0, -TITLE_HEIGHT), vec.New(PANEL_WIDTH, 670), 1i, {
    panelTemplate = "DebugDarkGrey",
    padding = 10,
    spacing = 8,
    defaultChildOpts = { modeX = "flex" },
});
content:SetAnchor(vec.New(0.0, 1.0));
content:SetRelativePoint(vec.New(0.0, 1.0));
content:SetAlpha(0.94);

local statusText = content:NewText("No map is ready for editing", vec.New(0, 0), 1i, "DevToolsText");
content:SetChildOpts(statusText, { modeX = "flex", sizeY = 18 });

local modeRow = UIHorizontalLayout.New(content, vec.New(0, 0), vec.New(PANEL_WIDTH - 20, 22), 1i, { alpha = 0.0, spacing = 8 });
content:SetChildOpts(modeRow, { modeX = "flex", sizeY = 22 });
TerrainEditor.modeButtons =
{
    sculpt = Button.New(modeRow, vec.New(0, 0), vec.New(136, 22), 1i, { text = "Sculpt", textTemplate = "DevToolsText" }),
    paint = Button.New(modeRow, vec.New(0, 0), vec.New(136, 22), 1i, { text = "Texture", textTemplate = "DevToolsText" }),
    vertex = Button.New(modeRow, vec.New(0, 0), vec.New(136, 22), 1i, { text = "Vertex", textTemplate = "DevToolsText" }),
    chunks = Button.New(modeRow, vec.New(0, 0), vec.New(136, 22), 1i, { text = "Chunks", textTemplate = "DevToolsText" }),
};
for _, button in TerrainEditor.modeButtons do
    modeRow:SetChildOpts(button, { modeX = "flex", sizeY = 22 });
end
TerrainEditor.modeButtons.sculpt:SetHighlighted(true);

local CHUNK_GRID_SIZE = PANEL_WIDTH - 20;
local CHUNK_GRID_STRIDE = 64i;
-- UI geometry uses numbers, while chunk coordinates and IDs remain integers.
local chunkGridStrideNumber = integer.tonumber(CHUNK_GRID_STRIDE);
local CHUNK_ZOOM_LEVELS = { 4, 8, 16, 32 };
local chunkZoomIndex = 1;
local chunkCellSize = CHUNK_ZOOM_LEVELS[chunkZoomIndex];
local chunkViewOffsetX = math.floor((CHUNK_GRID_SIZE - chunkGridStrideNumber * chunkCellSize) / 2);
local chunkViewOffsetY = chunkViewOffsetX;
local chunkGrid = content:NewPanel(vec.New(0, 0), vec.New(CHUNK_GRID_SIZE, CHUNK_GRID_SIZE), 1i, "DebugDarkGrey");
content:SetChildOpts(chunkGrid, { modeX = "fixed", sizeX = CHUNK_GRID_SIZE, sizeY = CHUNK_GRID_SIZE });
chunkGrid:SetAnchor(vec.New(0.0, 1.0));
chunkGrid:SetRelativePoint(vec.New(0.0, 1.0));
chunkGrid:SetBorderColor(vec.New(0.45, 0.48, 0.52));
chunkGrid:SetBorderSize(1.0);
chunkGrid:SetClipChildren(true);
chunkGrid:SetVisible(false);

local heightFieldPathRow = UIHorizontalLayout.New(content, vec.New(0, 0), vec.New(CHUNK_GRID_SIZE, 22), 1i, { alpha = 0.0, spacing = 8 });
content:SetChildOpts(heightFieldPathRow, { modeX = "flex", sizeY = 22 });
local heightFieldPathInput = InputField.New(heightFieldPathRow, vec.New(0, 0), vec.New(204, 22), 1i, {
    defaultText = "HeightmapImports/continent/continent_16x21_import.json",
    placeholderText = "Height-field manifest path",
});
local heightFieldPreviewButton = Button.New(heightFieldPathRow, vec.New(0, 0), vec.New(68, 22), 1i, { text = "Preview", textTemplate = "DevToolsText" });
heightFieldPathRow:SetChildOpts(heightFieldPathInput.root, { modeX = "flex", sizeY = 22 });
heightFieldPathRow:SetChildOpts(heightFieldPreviewButton, { modeX = "fixed", sizeX = 68, sizeY = 22 });
heightFieldPathRow:SetVisible(false);

local heightFieldSettingsRow = UIHorizontalLayout.New(content, vec.New(0, 0), vec.New(CHUNK_GRID_SIZE, 22), 1i, { alpha = 0.0, spacing = 8 });
content:SetChildOpts(heightFieldSettingsRow, { modeX = "flex", sizeY = 22 });
local heightFieldMinimumInput = InputField.New(heightFieldSettingsRow, vec.New(0, 0), vec.New(76, 22), 1i, { defaultText = "0", placeholderText = "Min height" });
local heightFieldMaximumInput = InputField.New(heightFieldSettingsRow, vec.New(0, 0), vec.New(76, 22), 1i, { defaultText = "2500", placeholderText = "Max height" });
local heightFieldImportButton = Button.New(heightFieldSettingsRow, vec.New(0, 0), vec.New(112, 22), 1i, { text = "Import Height", textTemplate = "DevToolsText" });
heightFieldSettingsRow:SetChildOpts(heightFieldMinimumInput.root, { modeX = "flex", sizeY = 22 });
heightFieldSettingsRow:SetChildOpts(heightFieldMaximumInput.root, { modeX = "flex", sizeY = 22 });
heightFieldSettingsRow:SetChildOpts(heightFieldImportButton, { modeX = "fixed", sizeX = 112, sizeY = 22 });
heightFieldSettingsRow:SetVisible(false);

local chunkMenu = ContextMenu.New(canvas, { items = { }, layer = integer.add(LAYER, 100i) });
local TEXTURE_TOOLTIP_WIDTH = 440;
local TEXTURE_TOOLTIP_HEIGHT = 32;
local textureTooltip = Tooltip.New(canvas, vec.New(TEXTURE_TOOLTIP_WIDTH, TEXTURE_TOOLTIP_HEIGHT), integer.add(LAYER, 120i));
textureTooltip.root:SetAnchor(vec.New(0.0, 0.0));
textureTooltip.root:SetRelativePoint(vec.New(0.0, 1.0));
local function GetFloatingPanelPosition(mousePosition : vec, width : number, height : number) : vec
    local x = mousePosition.x + 12;
    local y = mousePosition.y - 12;
    if x + width > 1920 then x = math.max(0, mousePosition.x - width - 12); end
    if y - height < 0 then y = math.min(1080, mousePosition.y + height + 12); end
    return vec.New(x, y);
end
local function ShowTextureTooltip(text : string?)
    if text == nil or text == "" then return; end

    textureTooltip:Clear();
    textureTooltip:AddLine(text, "DevToolsText");
    textureTooltip:ShowAt(GetFloatingPanelPosition(UI.GetMousePos(), TEXTURE_TOOLTIP_WIDTH, TEXTURE_TOOLTIP_HEIGHT));
end
local function AttachTextureTooltip(preview : Panel, getText : () -> string?)
    preview:SetOnHoverBegin(function() ShowTextureTooltip(getText()); end);
    preview:SetOnHoverEnd(function() textureTooltip:Hide(); end);
end

local QUICK_TEXTURE_SIZE = 56;
local QUICK_TEXTURE_SPACING = 4;
local QUICK_TEXTURE_PALETTE_WIDTH = 12 + 4 * QUICK_TEXTURE_SIZE + 3 * QUICK_TEXTURE_SPACING;
local QUICK_TEXTURE_PALETTE_HEIGHT = 96;
local QUICK_TEXTURE_TITLE_HEIGHT = 18;
local quickTextureWindow = EditorWindow.New(canvas, vec.New(0, 0), vec.New(QUICK_TEXTURE_PALETTE_WIDTH, QUICK_TEXTURE_PALETTE_HEIGHT), integer.add(LAYER, 100i), {
    title = "Cell Textures",
    titleHeight = QUICK_TEXTURE_TITLE_HEIGHT,
    closeSize = vec.New(14, 14),
});
local quickTexturePalette = quickTextureWindow.root;
quickTexturePalette:SetAnchor(vec.New(0.0, 0.0));
quickTexturePalette:SetRelativePoint(vec.New(0.0, 1.0));
local quickTextureEntries : { QuickTextureEntry } = { };
for index = 1, 4 do
    local previewX = 6 + (index - 1) * (QUICK_TEXTURE_SIZE + QUICK_TEXTURE_SPACING);
    local entry : QuickTextureEntry = {
        path = nil,
        preview = quickTexturePalette:NewPanel(vec.New(previewX, -QUICK_TEXTURE_TITLE_HEIGHT - 5), vec.New(QUICK_TEXTURE_SIZE, QUICK_TEXTURE_SIZE), 1i, "TerrainTexturePreview"),
        weightText = quickTexturePalette:NewText("0%", vec.New(previewX, -QUICK_TEXTURE_TITLE_HEIGHT - QUICK_TEXTURE_SIZE - 6), 1i, "DevToolsText"),
    };
    entry.preview:SetAnchor(vec.New(0.0, 1.0));
    entry.preview:SetRelativePoint(vec.New(0.0, 1.0));
    entry.weightText:SetAnchor(vec.New(0.0, 1.0));
    entry.weightText:SetRelativePoint(vec.New(0.0, 1.0));
    entry.weightText:SetInteractable(false);
    entry.preview:SetOnMouseUp(function()
        if entry.path ~= nil and TerrainEditor.SelectPaintTexture ~= nil then TerrainEditor.SelectPaintTexture(entry.path); end
    end);
    AttachTextureTooltip(entry.preview, function() return entry.path or "Layer not in use"; end);
    table.insert(quickTextureEntries, entry);
end
quickTextureWindow.closeButton:SetOnMouseUp(function()
    textureTooltip:Hide();
    quickTexturePalette:SetVisible(false);
end);
local chunkConfirmModal = Modal.New(canvas, vec.New(0, 0), vec.New(430, 180), integer.add(LAYER, 200i), {
    name = "TerrainChunkConfirm",
    title = "Confirm Chunk Operation",
    message = "",
    confirmText = "Confirm",
    cancelText = "Cancel",
});
local occupiedChunks : { [integer] : boolean } = { };
local occupiedChunkIDs : { integer } = { };
local heightFieldPreviewChunkIDs : { integer } = { };
local heightFieldPreviewPath : string? = nil;
local heightFieldPreviewCount = 0i;
local chunkLinePool : { Panel } = { };
local chunkOccupancyPool : { Panel } = { };
local heightFieldPreviewPool : { Panel } = { };
local chunkOperationPreviewPool : { Panel } = { };
local chunkLabelPool : { TextWidget } = { };
local chunkLineCount = 0;
local chunkOccupancyCount = 0;
local heightFieldPreviewPanelCount = 0;
local chunkOperationPreviewCount = 0;
local chunkLabelCount = 0;
local observedLayoutGeneration = -1i;
local hoveredChunkX : integer? = nil;
local hoveredChunkY : integer? = nil;
local selectedChunkX : integer? = nil;
local selectedChunkY : integer? = nil;
local chunkPanActive = false;
local chunkPanMouseX = 0;
local chunkPanMouseY = 0;
local chunkPanOffsetX = 0;
local chunkPanOffsetY = 0;
local chunkOperationMode : string? = nil;
local chunkOperationStartX = 0;
local chunkOperationStartY = 0;
local chunkOperationEndX = 0;
local chunkOperationEndY = 0;

local chunkHover = chunkGrid:NewPanel(vec.New(0, 0), vec.New(chunkCellSize, chunkCellSize), 5i, "DebugDarkGrey");
chunkHover:SetAnchor(vec.New(0.0, 1.0));
chunkHover:SetRelativePoint(vec.New(0.0, 1.0));
chunkHover:SetColor(vec.New(0.95, 0.78, 0.20));
chunkHover:SetAlpha(0.42);
chunkHover:SetBorderColor(vec.New(1.0, 0.9, 0.35));
chunkHover:SetBorderSize(1.0);
chunkHover:SetInteractable(false);
chunkHover:SetVisible(false);

local chunkSelection = chunkGrid:NewPanel(vec.New(0, 0), vec.New(chunkCellSize, chunkCellSize), 4i, "DebugDarkGrey");
chunkSelection:SetAnchor(vec.New(0.0, 1.0));
chunkSelection:SetRelativePoint(vec.New(0.0, 1.0));
chunkSelection:SetColor(vec.New(0.12, 0.32, 0.55));
chunkSelection:SetAlpha(0.24);
chunkSelection:SetBorderColor(vec.New(0.25, 0.65, 1.0));
chunkSelection:SetBorderSize(2.0);
chunkSelection:SetInteractable(false);
chunkSelection:SetVisible(false);

local function ChunkID(x : integer, y : integer) : integer
    return integer.create(integer.tonumber(x) + integer.tonumber(y) * chunkGridStrideNumber);
end

local function ClampChunkViewOffset(offset : number) : number
    local mapExtent = chunkGridStrideNumber * chunkCellSize;
    if mapExtent <= CHUNK_GRID_SIZE then return math.floor((CHUNK_GRID_SIZE - mapExtent) / 2); end
    return math.floor(math.clamp(offset, CHUNK_GRID_SIZE - mapExtent, 0));
end

local function PositionChunkMarker(marker : Panel, x : integer?, y : integer?)
    if x == nil or y == nil then marker:SetVisible(false); return; end
    local numericX = integer.tonumber(x);
    local numericY = integer.tonumber(y);
    local positionX = chunkViewOffsetX + numericX * chunkCellSize;
    local positionY = chunkViewOffsetY + numericY * chunkCellSize;
    if positionX + chunkCellSize <= 0 or positionY + chunkCellSize <= 0 or positionX >= CHUNK_GRID_SIZE or positionY >= CHUNK_GRID_SIZE then
        marker:SetVisible(false);
        return;
    end
    marker:SetPos(vec.New(positionX, -positionY));
    marker:SetSize(vec.New(chunkCellSize, chunkCellSize));
    marker:SetVisible(true);
end

local function AcquireChunkLine() : Panel
    chunkLineCount += 1;
    local line = chunkLinePool[chunkLineCount];
    if line == nil then
        line = chunkGrid:NewPanel(vec.New(0, 0), vec.New(1, 1), 1i, "DebugDarkGrey");
        line:SetAnchor(vec.New(0.0, 1.0));
        line:SetRelativePoint(vec.New(0.0, 1.0));
        line:SetInteractable(false);
        table.insert(chunkLinePool, line);
    end
    line:SetVisible(true);
    return line;
end

local function AcquireChunkOccupancy() : Panel
    chunkOccupancyCount += 1;
    local panel = chunkOccupancyPool[chunkOccupancyCount];
    if panel == nil then
        panel = chunkGrid:NewPanel(vec.New(0, 0), vec.New(1, 1), 2i, "DebugGreen");
        panel:SetAnchor(vec.New(0.0, 1.0));
        panel:SetRelativePoint(vec.New(0.0, 1.0));
        panel:SetAlpha(0.78);
        panel:SetInteractable(false);
        table.insert(chunkOccupancyPool, panel);
    end
    panel:SetVisible(true);
    return panel;
end

local function AcquireHeightFieldPreview() : Panel
    heightFieldPreviewPanelCount += 1;
    local panel = heightFieldPreviewPool[heightFieldPreviewPanelCount];
    if panel == nil then
        panel = chunkGrid:NewPanel(vec.New(0, 0), vec.New(1, 1), 3i, "DebugDarkGrey");
        panel:SetAnchor(vec.New(0.0, 1.0));
        panel:SetRelativePoint(vec.New(0.0, 1.0));
        panel:SetColor(vec.New(0.12, 0.72, 0.92));
        panel:SetAlpha(0.40);
        panel:SetBorderColor(vec.New(0.35, 0.90, 1.0));
        panel:SetBorderSize(1.0);
        panel:SetInteractable(false);
        table.insert(heightFieldPreviewPool, panel);
    end
    panel:SetVisible(true);
    return panel;
end

local function AcquireChunkOperationPreview() : Panel
    chunkOperationPreviewCount += 1;
    local panel = chunkOperationPreviewPool[chunkOperationPreviewCount];
    if panel == nil then
        panel = chunkGrid:NewPanel(vec.New(0, 0), vec.New(1, 1), 4i, "DebugDarkGrey");
        panel:SetAnchor(vec.New(0.0, 1.0));
        panel:SetRelativePoint(vec.New(0.0, 1.0));
        panel:SetAlpha(0.48);
        panel:SetBorderSize(1.0);
        panel:SetInteractable(false);
        table.insert(chunkOperationPreviewPool, panel);
    end
    if chunkOperationMode == "add" then
        panel:SetColor(vec.New(0.18, 0.75, 0.28));
        panel:SetBorderColor(vec.New(0.45, 1.0, 0.55));
    else
        panel:SetColor(vec.New(0.82, 0.18, 0.16));
        panel:SetBorderColor(vec.New(1.0, 0.42, 0.38));
    end
    panel:SetVisible(true);
    return panel;
end

local function IsChunkOperationEligible(mode : string, x : integer, y : integer) : boolean
    local occupied = occupiedChunks[ChunkID(x, y)] == true;
    return if mode == "add" then not occupied else occupied;
end

local function RefreshChunkOperationPreview()
    chunkOperationPreviewCount = 0;
    local mode = chunkOperationMode;
    if mode ~= nil then
        local minimumX = math.min(chunkOperationStartX, chunkOperationEndX);
        local maximumX = math.max(chunkOperationStartX, chunkOperationEndX);
        local minimumY = math.min(chunkOperationStartY, chunkOperationEndY);
        local maximumY = math.max(chunkOperationStartY, chunkOperationEndY);
        for numericY = minimumY, maximumY do
            for numericX = minimumX, maximumX do
                local x = integer.create(numericX);
                local y = integer.create(numericY);
                if not IsChunkOperationEligible(mode, x, y) then continue; end

                local positionX = chunkViewOffsetX + numericX * chunkCellSize;
                local positionY = chunkViewOffsetY + numericY * chunkCellSize;
                if positionX + chunkCellSize <= 0 or positionY + chunkCellSize <= 0 or positionX >= CHUNK_GRID_SIZE or positionY >= CHUNK_GRID_SIZE then continue; end

                local panel = AcquireChunkOperationPreview();
                panel:SetPos(vec.New(positionX, -positionY));
                panel:SetSize(vec.New(chunkCellSize, chunkCellSize));
            end
        end
    end

    for index = chunkOperationPreviewCount + 1, #chunkOperationPreviewPool do chunkOperationPreviewPool[index]:SetVisible(false); end
end

local function ClearChunkOperation()
    chunkOperationMode = nil;
    RefreshChunkOperationPreview();
end

local function AcquireChunkLabel() : TextWidget
    chunkLabelCount += 1;
    local label = chunkLabelPool[chunkLabelCount];
    if label == nil then
        label = chunkGrid:NewText("", vec.New(0, 0), 3i, "DevToolsText");
        label:SetAnchor(vec.New(0.0, 1.0));
        label:SetRelativePoint(vec.New(0.0, 1.0));
        label:SetInteractable(false);
        table.insert(chunkLabelPool, label);
    end
    label:SetVisible(true);
    return label;
end

local function AddChunkGridLine(position : number, vertical : boolean, major : boolean, center : boolean)
    local thickness = if center then 2 else 1;
    local line = AcquireChunkLine();
    line:SetPos(if vertical then vec.New(position, 0) else vec.New(0, -position));
    line:SetSize(if vertical then vec.New(thickness, CHUNK_GRID_SIZE) else vec.New(CHUNK_GRID_SIZE, thickness));
    line:SetColor(if center then vec.New(0.35, 0.70, 1.0) else vec.New(0.72, 0.75, 0.80));
    line:SetAlpha(if center then 0.9 elseif major then 0.48 else 0.16);
end

local function RefreshChunkViewport()
    chunkLineCount = 0;
    chunkOccupancyCount = 0;
    heightFieldPreviewPanelCount = 0;
    chunkLabelCount = 0;
    chunkViewOffsetX = ClampChunkViewOffset(chunkViewOffsetX);
    chunkViewOffsetY = ClampChunkViewOffset(chunkViewOffsetY);

    local firstX = math.max(0, math.floor(-chunkViewOffsetX / chunkCellSize));
    local firstY = math.max(0, math.floor(-chunkViewOffsetY / chunkCellSize));
    local lastX = math.min(chunkGridStrideNumber, math.ceil((CHUNK_GRID_SIZE - chunkViewOffsetX) / chunkCellSize));
    local lastY = math.min(chunkGridStrideNumber, math.ceil((CHUNK_GRID_SIZE - chunkViewOffsetY) / chunkCellSize));
    local showMinorLines = chunkCellSize >= 8;

    for coordinate = firstX, lastX do
        local major = coordinate % 8 == 0;
        if showMinorLines or major then AddChunkGridLine(math.floor(chunkViewOffsetX + coordinate * chunkCellSize), true, major, coordinate == 32); end
    end
    for coordinate = firstY, lastY do
        local major = coordinate % 8 == 0;
        if showMinorLines or major then AddChunkGridLine(math.floor(chunkViewOffsetY + coordinate * chunkCellSize), false, major, coordinate == 32); end
    end

    for _, chunkID in occupiedChunkIDs do
        local numericChunkID = integer.tonumber(chunkID);
        local x = numericChunkID % chunkGridStrideNumber;
        local y = math.floor(numericChunkID / chunkGridStrideNumber);
        local positionX = chunkViewOffsetX + x * chunkCellSize;
        local positionY = chunkViewOffsetY + y * chunkCellSize;
        if positionX + chunkCellSize <= 0 or positionY + chunkCellSize <= 0 or positionX >= CHUNK_GRID_SIZE or positionY >= CHUNK_GRID_SIZE then continue; end
        local panel = AcquireChunkOccupancy();
        panel:SetPos(vec.New(positionX, -positionY));
        panel:SetSize(vec.New(chunkCellSize, chunkCellSize));
    end

    for _, chunkID in heightFieldPreviewChunkIDs do
        local numericChunkID = integer.tonumber(chunkID);
        local x = numericChunkID % chunkGridStrideNumber;
        local y = math.floor(numericChunkID / chunkGridStrideNumber);
        local positionX = chunkViewOffsetX + x * chunkCellSize;
        local positionY = chunkViewOffsetY + y * chunkCellSize;
        if positionX + chunkCellSize <= 0 or positionY + chunkCellSize <= 0 or positionX >= CHUNK_GRID_SIZE or positionY >= CHUNK_GRID_SIZE then continue; end
        local panel = AcquireHeightFieldPreview();
        panel:SetPos(vec.New(positionX, -positionY));
        panel:SetSize(vec.New(chunkCellSize, chunkCellSize));
    end

    local labelStride = if chunkCellSize >= 16 then 4 else 8;
    for coordinate = math.ceil(firstX / labelStride) * labelStride, lastX, labelStride do
        if coordinate >= chunkGridStrideNumber then continue; end
        local label = AcquireChunkLabel();
        label:SetText(tostring(coordinate));
        label:SetPos(vec.New(math.floor(chunkViewOffsetX + coordinate * chunkCellSize) + 2, -2));
    end
    for coordinate = math.ceil(firstY / labelStride) * labelStride, lastY, labelStride do
        if coordinate == 0 or coordinate >= chunkGridStrideNumber then continue; end
        local label = AcquireChunkLabel();
        label:SetText(tostring(coordinate));
        label:SetPos(vec.New(2, -(math.floor(chunkViewOffsetY + coordinate * chunkCellSize) + 2)));
    end

    for index = chunkLineCount + 1, #chunkLinePool do chunkLinePool[index]:SetVisible(false); end
    for index = chunkOccupancyCount + 1, #chunkOccupancyPool do chunkOccupancyPool[index]:SetVisible(false); end
    for index = heightFieldPreviewPanelCount + 1, #heightFieldPreviewPool do heightFieldPreviewPool[index]:SetVisible(false); end
    for index = chunkLabelCount + 1, #chunkLabelPool do chunkLabelPool[index]:SetVisible(false); end

    RefreshChunkOperationPreview();
    PositionChunkMarker(chunkSelection, selectedChunkX, selectedChunkY);
    PositionChunkMarker(chunkHover, hoveredChunkX, hoveredChunkY);
end

local function RefreshChunkGrid()
    local layout = Terrain.GetChunkLayout();
    if layout == nil then return; end
    observedLayoutGeneration = layout.generation;
    CHUNK_GRID_STRIDE = layout.stride;
    chunkGridStrideNumber = integer.tonumber(CHUNK_GRID_STRIDE);
    occupiedChunks = { };
    occupiedChunkIDs = layout.occupiedChunkIDs;
    for _, chunkID in occupiedChunkIDs do occupiedChunks[chunkID] = true; end
    RefreshChunkViewport();
end

local function ConfirmChunkAction(title : string, message : string, action : () -> ())
    chunkConfirmModal:Open({ title = title, message = message, onConfirm = action });
end

local function PreviewHeightFieldImport() : boolean
    local manifestPath = heightFieldPathInput:GetText();
    local preview = Terrain.PreviewHeightFieldImport(manifestPath);
    if not preview.success or preview.occupiedChunkIDs == nil then
        heightFieldPreviewChunkIDs = { };
        heightFieldPreviewPath = nil;
        heightFieldPreviewCount = 0i;
        TerrainEditor.saveStatus = preview.error or "Could not preview the height field";
        RefreshChunkViewport();
        return false;
    end

    heightFieldPreviewChunkIDs = preview.occupiedChunkIDs;
    heightFieldPreviewPath = manifestPath;
    heightFieldPreviewCount = preview.occupiedChunkCount or integer.create(#heightFieldPreviewChunkIDs);
    local width = preview.footprintWidth or 0;
    local height = preview.footprintHeight or 0;
    local pixelsPerPatchX = preview.sourcePixelsPerPatchX or 0;
    local pixelsPerPatchY = preview.sourcePixelsPerPatchY or 0;
    TerrainEditor.saveStatus = `Preview: {heightFieldPreviewCount} chunks, {width}x{height}, {string.format("%.2f", pixelsPerPatchX)}x{string.format("%.2f", pixelsPerPatchY)} px/patch`;
    RefreshChunkViewport();
    return true;
end

heightFieldPreviewButton:SetOnMouseUp(function()
    PreviewHeightFieldImport();
end);

heightFieldImportButton:SetOnMouseUp(function()
    local manifestPath = heightFieldPathInput:GetText();
    if heightFieldPreviewPath ~= manifestPath and not PreviewHeightFieldImport() then return; end

    local minimumHeight = tonumber(heightFieldMinimumInput:GetText());
    local maximumHeight = tonumber(heightFieldMaximumInput:GetText());
    if minimumHeight == nil or maximumHeight == nil or maximumHeight <= minimumHeight then
        TerrainEditor.saveStatus = "Maximum height must be greater than minimum height";
        return;
    end

    ConfirmChunkAction("Replace Entire Terrain", `Replace the entire terrain with {heightFieldPreviewCount} generated chunks? Existing terrain chunk data will be replaced. This is not brush-undoable and remains unsaved until Save Terrain. Old chunk placements or liquids may remain visible until the map is reloaded.`, function()
        TerrainEditor.saveStatus = "Importing height field...";
        local result = Terrain.ImportHeightField(manifestPath, minimumHeight, maximumHeight);
        if result.success then
            TerrainEditor.saveStatus = `Imported {result.importedChunkCount} chunks; use Save Terrain to persist`;
            RefreshChunkGrid();
        else
            TerrainEditor.saveStatus = result.error or "Height-field import failed";
        end
    end);
end);

local function OpenChunkMenu(x : integer, y : integer, mouseX : number, mouseY : number)
    local chunkID = ChunkID(x, y);
    if occupiedChunks[chunkID] then
        chunkMenu:SetItems({
            { label = "Goto", onClick = function() Terrain.GoToChunk(x, y); end },
            { label = "Reset to Default", onClick = function()
                ConfirmChunkAction("Reset Terrain Chunk", `Reset chunk {x}, {y} to flat, untextured terrain?`, function()
                    if Terrain.ResetChunk(x, y) then
                        TerrainEditor.saveStatus = nil;
                        RefreshChunkGrid();
                    else
                        TerrainEditor.saveStatus = "Save or undo this chunk's edits before resetting it";
                    end
                end);
            end },
            { label = "Remove", onClick = function()
                ConfirmChunkAction("Remove Terrain Chunk", `Unlink chunk {x}, {y} from this map? Its chunk file will be preserved.`, function()
                    if Terrain.RemoveChunk(x, y) then
                        TerrainEditor.saveStatus = nil;
                        RefreshChunkGrid();
                    else
                        TerrainEditor.saveStatus = "Save or undo this chunk's edits before removing it";
                    end
                end);
            end },
        });
    else
        chunkMenu:SetItems({
            { label = "Add Chunk", onClick = function()
                if Terrain.AddChunk(x, y) then
                    TerrainEditor.saveStatus = nil;
                    RefreshChunkGrid();
                else
                    TerrainEditor.saveStatus = "Failed to add terrain chunk";
                end
            end },
        });
    end
    chunkMenu:Open(mouseX, mouseY);
end

local function GetChunkAtMouse(mouseX : number, mouseY : number)
    local localX = mouseX - chunkGrid:GetWorldPosX();
    local localTopY = CHUNK_GRID_SIZE - (mouseY - chunkGrid:GetWorldPosY());
    local x = math.floor((localX - chunkViewOffsetX) / chunkCellSize);
    local y = math.floor((localTopY - chunkViewOffsetY) / chunkCellSize);
    if x < 0 or y < 0 or x >= chunkGridStrideNumber or y >= chunkGridStrideNumber then return nil, nil; end
    return integer.create(x), integer.create(y);
end

local function UpdateHoveredChunk(mouseX : number, mouseY : number)
    hoveredChunkX, hoveredChunkY = GetChunkAtMouse(mouseX, mouseY);
    PositionChunkMarker(chunkHover, hoveredChunkX, hoveredChunkY);
end

local function BeginChunkOperation(x : integer, y : integer) : boolean
    local shiftDown = InputAPI.IsDown("TerrainInvertBrush");
    local controlDown = InputAPI.IsDown("TerrainAdjustStrengthModifier");
    if shiftDown == controlDown then return false; end

    chunkOperationMode = if shiftDown then "add" else "remove";
    chunkOperationStartX = integer.tonumber(x);
    chunkOperationStartY = integer.tonumber(y);
    chunkOperationEndX = chunkOperationStartX;
    chunkOperationEndY = chunkOperationStartY;
    RefreshChunkOperationPreview();
    return true;
end

local function UpdateChunkOperation(mouseX : number, mouseY : number)
    if chunkOperationMode == nil then return; end
    local x, y = GetChunkAtMouse(mouseX, mouseY);
    if x == nil or y == nil then return; end

    local numericX = integer.tonumber(x);
    local numericY = integer.tonumber(y);
    if numericX == chunkOperationEndX and numericY == chunkOperationEndY then return; end
    chunkOperationEndX = numericX;
    chunkOperationEndY = numericY;
    RefreshChunkOperationPreview();
end

local function ApplyChunkOperation()
    local mode = chunkOperationMode;
    if mode == nil then return; end

    local minimumX = math.min(chunkOperationStartX, chunkOperationEndX);
    local maximumX = math.max(chunkOperationStartX, chunkOperationEndX);
    local minimumY = math.min(chunkOperationStartY, chunkOperationEndY);
    local maximumY = math.max(chunkOperationStartY, chunkOperationEndY);
    local succeeded = 0;
    local failed = 0;
    for numericY = minimumY, maximumY do
        for numericX = minimumX, maximumX do
            local x = integer.create(numericX);
            local y = integer.create(numericY);
            if not IsChunkOperationEligible(mode, x, y) then continue; end

            local success = if mode == "add" then Terrain.AddChunk(x, y) else Terrain.RemoveChunk(x, y);
            if success then
                succeeded += 1;
            else
                failed += 1;
            end
        end
    end

    ClearChunkOperation();
    if succeeded > 0 then
        TerrainEditor.saveStatus = nil;
        RefreshChunkGrid();
    end
    if failed > 0 then
        TerrainEditor.saveStatus = if mode == "add"
            then `{failed} chunk(s) could not be added`
            else `{failed} chunk(s) could not be removed; save or undo their edits first`;
    end
end

chunkGrid:SetOnHoverHeld(function()
    local mousePosition = UI.GetMousePos();
    UpdateHoveredChunk(mousePosition.x, mousePosition.y);
end);
chunkGrid:SetOnHoverEnd(function()
    chunkPanActive = false;
    hoveredChunkX = nil;
    hoveredChunkY = nil;
    chunkHover:SetVisible(false);
end);
chunkGrid:SetOnMouseDown(function(_, _, button, mouseX, mouseY)
    if button == 2i then
        chunkPanActive = true;
        chunkPanMouseX = mouseX;
        chunkPanMouseY = mouseY;
        chunkPanOffsetX = chunkViewOffsetX;
        chunkPanOffsetY = chunkViewOffsetY;
        return;
    end
    if button ~= 0i then return; end

    local x, y = GetChunkAtMouse(mouseX, mouseY);
    if x == nil or y == nil or not BeginChunkOperation(x, y) then return; end
    selectedChunkX = x;
    selectedChunkY = y;
    PositionChunkMarker(chunkSelection, selectedChunkX, selectedChunkY);
end);
chunkGrid:SetOnMouseHeld(function(_, _, mouseX, mouseY)
    if chunkOperationMode ~= nil then
        UpdateChunkOperation(mouseX, mouseY);
        UpdateHoveredChunk(mouseX, mouseY);
        return;
    end
    if not chunkPanActive then return; end
    chunkViewOffsetX = ClampChunkViewOffset(chunkPanOffsetX + mouseX - chunkPanMouseX);
    chunkViewOffsetY = ClampChunkViewOffset(chunkPanOffsetY - (mouseY - chunkPanMouseY));
    RefreshChunkViewport();
    UpdateHoveredChunk(mouseX, mouseY);
end);
chunkGrid:SetOnMouseUp(function(_, _, button, mouseX, mouseY)
    if button == 2i then
        chunkPanActive = false;
        return;
    end
    if button ~= 0i and button ~= 1i then return; end
    local x, y = GetChunkAtMouse(mouseX, mouseY);
    if button == 0i and chunkOperationMode ~= nil then
        UpdateChunkOperation(mouseX, mouseY);
        if x ~= nil and y ~= nil then
            selectedChunkX = x;
            selectedChunkY = y;
            PositionChunkMarker(chunkSelection, selectedChunkX, selectedChunkY);
        end
        ApplyChunkOperation();
        return;
    end
    if x == nil or y == nil then return; end
    selectedChunkX = x;
    selectedChunkY = y;
    PositionChunkMarker(chunkSelection, selectedChunkX, selectedChunkY);
    if button == 1i then
        OpenChunkMenu(x, y, mouseX, mouseY);
    end
end);
chunkGrid:SetOnMouseScroll(function(_, _, _, scrollY)
    if scrollY == 0 then return; end
    local mousePosition = UI.GetMousePos();
    local localX = mousePosition.x - chunkGrid:GetWorldPosX();
    local localTopY = CHUNK_GRID_SIZE - (mousePosition.y - chunkGrid:GetWorldPosY());
    local worldX = (localX - chunkViewOffsetX) / chunkCellSize;
    local worldY = (localTopY - chunkViewOffsetY) / chunkCellSize;
    local nextZoomIndex = math.clamp(chunkZoomIndex + (if scrollY > 0 then 1 else -1), 1, 4);
    if nextZoomIndex == chunkZoomIndex then return; end
    chunkZoomIndex = nextZoomIndex;
    chunkCellSize = CHUNK_ZOOM_LEVELS[chunkZoomIndex];
    chunkViewOffsetX = ClampChunkViewOffset(localX - worldX * chunkCellSize);
    chunkViewOffsetY = ClampChunkViewOffset(localTopY - worldY * chunkCellSize);
    RefreshChunkViewport();
    UpdateHoveredChunk(mousePosition.x, mousePosition.y);
end);

local fitChunkGridButton = Button.New(chunkGrid, vec.New(-4, -4), vec.New(32, 18), 10i, { text = "Fit", textTemplate = "DevToolsText" });
fitChunkGridButton:SetAnchor(vec.New(1.0, 1.0));
fitChunkGridButton:SetRelativePoint(vec.New(1.0, 1.0));
fitChunkGridButton:SetOnMouseUp(function()
    chunkZoomIndex = 1;
    chunkCellSize = CHUNK_ZOOM_LEVELS[chunkZoomIndex];
    chunkViewOffsetX = ClampChunkViewOffset(0);
    chunkViewOffsetY = ClampChunkViewOffset(0);
    RefreshChunkViewport();
end);
local centerChunkGridButton = Button.New(chunkGrid, vec.New(-40, -4), vec.New(52, 18), 10i, { text = "Center", textTemplate = "DevToolsText" });
centerChunkGridButton:SetAnchor(vec.New(1.0, 1.0));
centerChunkGridButton:SetRelativePoint(vec.New(1.0, 1.0));
centerChunkGridButton:SetOnMouseUp(function()
    local mapExtent = chunkGridStrideNumber * chunkCellSize;
    chunkViewOffsetX = math.floor((CHUNK_GRID_SIZE - mapExtent) / 2);
    chunkViewOffsetY = chunkViewOffsetX;
    RefreshChunkViewport();
end);

local brushRow = UIHorizontalLayout.New(content, vec.New(0, 0), vec.New(PANEL_WIDTH - 20, 22), 1i, { alpha = 0.0, spacing = 4 });
content:SetChildOpts(brushRow, { modeX = "flex", sizeY = 22 });
local brushButtons = { };
for _, brush in TerrainBrushes.GetAll() do
    if brush.id == "texture_paint" or brush.id == "vertex_paint" then continue; end
    local button = Button.New(brushRow, vec.New(0, 0), vec.New(90, 22), 1i, { text = brush.displayName, textTemplate = "DevToolsText" });
    brushRow:SetChildOpts(button, { modeX = "flex", sizeY = 22 });
    button:SetOnMouseUp(function()
        if TerrainEditor.activeStroke ~= nil then return; end
        TerrainEditor.sculptBrush = brush;
        if TerrainEditor.editMode == "sculpt" then TerrainEditor.selectedBrush = brush; end
        for _, candidate in brushButtons do
            candidate.button:SetHighlighted(candidate.brush == TerrainEditor.sculptBrush);
        end
    end);
    table.insert(brushButtons, { button = button, brush = brush });
end
brushButtons[1].button:SetHighlighted(true);

local cursorLayerRow = UIHorizontalLayout.New(content, vec.New(0, 0), vec.New(PANEL_WIDTH - 20, 22), 1i, { alpha = 0.0, spacing = 4 });
content:SetChildOpts(cursorLayerRow, { modeX = "flex", sizeY = 22 });
local cursorLayerButtons = { };
for layerIndex = 0, 4 do
    local targetLayerIndex = integer.create(layerIndex);
    local buttonText = if targetLayerIndex == 0 then "Auto" else `Layer {targetLayerIndex}`;
    local button = Button.New(cursorLayerRow, vec.New(0, 0), vec.New(52, 22), 1i, { text = buttonText, textTemplate = "DevToolsText" });
    cursorLayerRow:SetChildOpts(button, { modeX = "flex", sizeY = 22 });
    button:SetOnMouseUp(function()
        local accepted = TerrainEditor.activeStroke == nil and Terrain.SetPaintLayer(targetLayerIndex);
        if not accepted then return; end
        TerrainEditor.paintTargetLayer = targetLayerIndex;
        for buttonIndex, candidate in cursorLayerButtons do
            candidate:SetHighlighted(integer.create(buttonIndex - 1) == TerrainEditor.paintTargetLayer);
        end
    end);
    table.insert(cursorLayerButtons, button);
end
cursorLayerButtons[1]:SetHighlighted(true);

local function AddSlider(label : string, minimum : number, maximum : number, step : number, initial : number, onChanged : (number) -> ())
    local row = content:NewPanel(vec.New(0, 0), vec.New(PANEL_WIDTH - 20, 34), 1i);
    row:SetAlpha(0.0);
    content:SetChildOpts(row, { modeX = "flex", sizeY = 34 });

    local labelText = row:NewText(label, vec.New(0, 0), 1i, "DevToolsText");
    labelText:SetAnchor(vec.New(0.0, 1.0));
    labelText:SetRelativePoint(vec.New(0.0, 1.0));

    local valueText = row:NewText(string.format("%.2f", initial), vec.New(0, 0), 1i, "DevToolsText");
    valueText:SetAnchor(vec.New(1.0, 1.0));
    valueText:SetRelativePoint(vec.New(1.0, 1.0));

    local slider = Slider.New(row, vec.New(0, -16), vec.New(PANEL_WIDTH - 20, 18), 1i, { Min = minimum, Max = maximum, Step = step });
    slider.root:SetAnchor(vec.New(0.0, 1.0));
    slider.root:SetRelativePoint(vec.New(0.0, 1.0));
    slider:SetOnValueChanged(function(_, value)
        valueText:SetText(string.format("%.2f", value));
        onChanged(value);
    end);
    slider:SetValue(initial);
    return { root = row, slider = slider, label = labelText, value = valueText };
end

local radiusSlider = AddSlider("Radius", 0.5, 200.0, 0.5, settings.radius, function(value) settings.radius = value; end);
local strengthSlider = AddSlider("Strength", 0.1, 100.0, 0.5, settings.strength, function(value) settings.strength = value; end);
local hardnessSlider = AddSlider("Hardness", 0.0, 1.0, 0.01, settings.hardness, function(value) settings.hardness = value; end);
local opacitySlider = AddSlider("Target Opacity", 0.0, 1.0, 0.01, settings.opacity, function(value) settings.opacity = value; end);

TerrainEditor.vertexControls = { };
do
    local vertexControls = TerrainEditor.vertexControls;
    local function RefreshVertexColorPreview()
        if vertexControls.preview == nil then return; end
        vertexControls.preview:SetColor(vec.New(settings.vertexRed / 255.0, settings.vertexGreen / 255.0, settings.vertexBlue / 255.0));
    end

    vertexControls.red = AddSlider("Red", 0.0, 255.0, 1.0, settings.vertexRed, function(value) settings.vertexRed = value; RefreshVertexColorPreview(); end);
    vertexControls.green = AddSlider("Green", 0.0, 255.0, 1.0, settings.vertexGreen, function(value) settings.vertexGreen = value; RefreshVertexColorPreview(); end);
    vertexControls.blue = AddSlider("Blue", 0.0, 255.0, 1.0, settings.vertexBlue, function(value) settings.vertexBlue = value; RefreshVertexColorPreview(); end);
    vertexControls.previewRow = content:NewPanel(vec.New(0, 0), vec.New(PANEL_WIDTH - 20, 28), 1i);
    vertexControls.previewRow:SetAlpha(0.0);
    content:SetChildOpts(vertexControls.previewRow, { modeX = "flex", sizeY = 28 });

    local label = vertexControls.previewRow:NewText("Target Color", vec.New(0, -5), 1i, "DevToolsText");
    label:SetAnchor(vec.New(0.0, 1.0));
    label:SetRelativePoint(vec.New(0.0, 1.0));

    vertexControls.preview = vertexControls.previewRow:NewPanel(vec.New(-2, -2), vec.New(92, 24), 1i, "TerrainTexturePreview");
    vertexControls.preview:SetAnchor(vec.New(1.0, 1.0));
    vertexControls.preview:SetRelativePoint(vec.New(1.0, 1.0));
    vertexControls.preview:SetInteractable(false);
    RefreshVertexColorPreview();
end

local textureButton = Button.New(content, vec.New(0, 0), vec.New(PANEL_WIDTH - 20, 22), 1i, { text = "Choose Texture...", textTemplate = "DevToolsText" });
content:SetChildOpts(textureButton, { modeX = "flex", sizeY = 22 });
TerrainEditor.textureText = content:NewText("No texture selected", vec.New(0, 0), 1i, "DevToolsText");
content:SetChildOpts(TerrainEditor.textureText, { modeX = "flex", sizeY = 18 });

local currentTexturePreviewRow = content:NewPanel(vec.New(0, 0), vec.New(PANEL_WIDTH - 20, 72), 1i);
currentTexturePreviewRow:SetAlpha(0.0);
content:SetChildOpts(currentTexturePreviewRow, { modeX = "flex", sizeY = 72 });
local currentTexturePreview = currentTexturePreviewRow:NewPanel(vec.New(0, 0), vec.New(72, 72), 1i, "TerrainTexturePreview");
currentTexturePreview:SetAnchor(vec.New(0.5, 1.0));
currentTexturePreview:SetRelativePoint(vec.New(0.5, 1.0));
AttachTextureTooltip(currentTexturePreview, function() return TerrainEditor.selectedTexturePath; end);

local recentTextureRows = { };
local recentTextureButtons = { };
for rowIndex = 1, 2 do
    local row = UIHorizontalLayout.New(content, vec.New(0, 0), vec.New(PANEL_WIDTH - 20, 64), 1i, { alpha = 0.0, spacing = 4 });
    content:SetChildOpts(row, { modeX = "flex", sizeY = 64 });
    table.insert(recentTextureRows, row);
    for columnIndex = 1, 4 do
        local recentIndex = (rowIndex - 1) * 4 + columnIndex;
        local button = row:NewPanel(vec.New(0, 0), vec.New(64, 64), 1i, "TerrainTexturePreview");
        row:SetChildOpts(button, { modeX = "flex", sizeY = 64 });
        button:SetInteractable(false);
        button:SetOnMouseUp(function()
            local path = TerrainEditor.recentTexturePaths[recentIndex];
            if path ~= nil and TerrainEditor.SelectPaintTexture ~= nil then TerrainEditor.SelectPaintTexture(path); end
        end);
        AttachTextureTooltip(button, function() return TerrainEditor.recentTexturePaths[recentIndex]; end);
        table.insert(recentTextureButtons, button);
    end
end

local function RefreshRecentTextures()
    for index, button in recentTextureButtons do
        local path = TerrainEditor.recentTexturePaths[index];
        button:SetVisible(TerrainEditor.editMode == "paint");
        button:SetInteractable(path ~= nil);
        button:SetBackground(path);
        if path ~= nil then
            button:SetColor(vec.New(1.0, 1.0, 1.0));
            button:SetBorderColor(if path == TerrainEditor.selectedTexturePath then vec.New(0.25, 0.65, 1.0) else vec.New(0.35, 0.38, 0.42));
            button:SetBorderSize(if path == TerrainEditor.selectedTexturePath then 2.0 else 1.0);
        else
            button:SetColor(vec.New(0.12, 0.13, 0.15));
            button:SetBorderColor(vec.New(0.25, 0.27, 0.30));
            button:SetBorderSize(1.0);
        end
    end
    for _, row in recentTextureRows do row:Invalidate(); end
end

TerrainEditor.SelectPaintTexture = function(path : string) : boolean
    local normalizedPath = path:gsub("\\", "/"):lower():gsub("^/+", "");
    if not Terrain.SetPaintTexture(normalizedPath) then return false; end

    TerrainEditor.selectedTexturePath = normalizedPath;
    TerrainEditor.textureText:SetText(normalizedPath);
    currentTexturePreview:SetBackground(normalizedPath);
    currentTexturePreview:SetColor(vec.New(1.0, 1.0, 1.0));
    for _, entry in quickTextureEntries do
        if entry.path ~= nil then
            entry.preview:SetBorderColor(if entry.path == normalizedPath then vec.New(0.25, 0.65, 1.0) else vec.New(0.35, 0.38, 0.42));
            entry.preview:SetBorderSize(if entry.path == normalizedPath then 2.0 else 1.0);
        end
    end
    for index = #TerrainEditor.recentTexturePaths, 1, -1 do
        if TerrainEditor.recentTexturePaths[index] == normalizedPath then table.remove(TerrainEditor.recentTexturePaths, index); end
    end
    table.insert(TerrainEditor.recentTexturePaths, 1, normalizedPath);
    if #TerrainEditor.recentTexturePaths > 8 then table.remove(TerrainEditor.recentTexturePaths, 9); end
    RefreshRecentTextures();
    return true;
end

local function OpenQuickTextureMenu(layers : { TextureLayer }, mousePosition : vec)
    TerrainEditor.cursorLayers = layers;
    TerrainEditor.RefreshCursorLayerButtons();
    for index, entry in quickTextureEntries do
        local layer = TerrainEditor.cursorLayers[index];
        local path = if layer ~= nil and layer.path ~= "" then layer.path else nil;
        entry.path = path;
        entry.preview:SetBackground(path);
        entry.weightText:SetText(if layer ~= nil then `{math.floor(layer.averageWeight * 100.0 + 0.5)}%` else "0%");
        if path ~= nil then
            entry.preview:SetColor(vec.New(1.0, 1.0, 1.0));
            entry.preview:SetBorderColor(if path == TerrainEditor.selectedTexturePath then vec.New(0.25, 0.65, 1.0) else vec.New(0.35, 0.38, 0.42));
            entry.preview:SetBorderSize(if path == TerrainEditor.selectedTexturePath then 2.0 else 1.0);
        else
            entry.preview:SetColor(vec.New(0.12, 0.13, 0.15));
            entry.preview:SetBorderColor(vec.New(0.25, 0.27, 0.30));
            entry.preview:SetBorderSize(1.0);
        end
    end

    if not TerrainEditor.quickPickPositionInitialized then
        quickTexturePalette:SetPos(GetFloatingPanelPosition(mousePosition, QUICK_TEXTURE_PALETTE_WIDTH, QUICK_TEXTURE_PALETTE_HEIGHT));
        TerrainEditor.quickPickPositionInitialized = true;
    end
    quickTexturePalette:SetVisible(true);
end

local saveButton = Button.New(content, vec.New(0, 0), vec.New(PANEL_WIDTH - 20, 22), 1i, { text = "Save Terrain (Ctrl+S)", textTemplate = "DevToolsText" });
content:SetChildOpts(saveButton, { modeX = "flex", sizeY = 22 });

local actionRow = UIHorizontalLayout.New(content, vec.New(0, 0), vec.New(PANEL_WIDTH - 20, 22), 1i, { alpha = 0.0, spacing = 6 });
content:SetChildOpts(actionRow, { modeX = "flex", sizeY = 22 });
local undoButton = Button.New(actionRow, vec.New(0, 0), vec.New(88, 22), 1i, { text = "Undo", textTemplate = "DevToolsText" });
local redoButton = Button.New(actionRow, vec.New(0, 0), vec.New(88, 22), 1i, { text = "Redo", textTemplate = "DevToolsText" });
local exitButton = Button.New(actionRow, vec.New(0, 0), vec.New(92, 22), 1i, { text = "Exit Mode", textTemplate = "DevToolsText" });
for _, button in { undoButton, redoButton, exitButton } do
    actionRow:SetChildOpts(button, { modeX = "flex", sizeY = 22 });
end

local dirtyText = content:NewText("No unsaved terrain changes", vec.New(0, 0), 1i, "DevToolsText");
content:SetChildOpts(dirtyText, { modeX = "flex", sizeY = 18 });

local function SetSliderVisible(control, visible : boolean)
    control.root:SetVisible(visible);
end

TerrainEditor.RefreshCursorLayerButtons = function()
    for buttonIndex, button in cursorLayerButtons do
        local layerIndex = buttonIndex - 1;
        local layer = if layerIndex > 0 then TerrainEditor.cursorLayers[layerIndex] else nil;
        button:SetVisible(TerrainEditor.editMode == "paint");
        if layerIndex == 0 then
            button:SetText("Auto");
        elseif layer ~= nil then
            button:SetText(`L{layerIndex} {math.floor(layer.averageWeight * 100.0 + 0.5)}%`);
        else
            button:SetText(`Layer {layerIndex}`);
        end
        button:SetHighlighted(integer.create(layerIndex) == TerrainEditor.paintTargetLayer);
    end
    cursorLayerRow:Invalidate();
end;

local function SetEditMode(nextMode : string)
    if TerrainEditor.activeStroke ~= nil or (nextMode ~= "sculpt" and nextMode ~= "paint" and nextMode ~= "vertex" and nextMode ~= "chunks") then return; end
    if nextMode ~= "chunks" then
        chunkPanActive = false;
        ClearChunkOperation();
    end
    TerrainEditor.editMode = nextMode;
    TerrainEditor.selectedBrush = if TerrainEditor.editMode == "paint" then TerrainEditor.paintBrush elseif TerrainEditor.editMode == "vertex" then TerrainEditor.vertexBrush else TerrainEditor.sculptBrush;
    TerrainEditor.modeButtons.sculpt:SetHighlighted(TerrainEditor.editMode == "sculpt");
    TerrainEditor.modeButtons.paint:SetHighlighted(TerrainEditor.editMode == "paint");
    TerrainEditor.modeButtons.vertex:SetHighlighted(TerrainEditor.editMode == "vertex");
    TerrainEditor.modeButtons.chunks:SetHighlighted(TerrainEditor.editMode == "chunks");
    for _, candidate in brushButtons do
        candidate.button:SetVisible(TerrainEditor.editMode == "sculpt");
    end
    brushRow:SetVisible(TerrainEditor.editMode == "sculpt");
    cursorLayerRow:SetVisible(TerrainEditor.editMode == "paint");
    strengthSlider.label:SetText(if TerrainEditor.editMode == "paint" or TerrainEditor.editMode == "vertex" then "Flow" else "Strength");
    SetSliderVisible(opacitySlider, TerrainEditor.editMode == "paint");
    SetSliderVisible(TerrainEditor.vertexControls.red, TerrainEditor.editMode == "vertex");
    SetSliderVisible(TerrainEditor.vertexControls.green, TerrainEditor.editMode == "vertex");
    SetSliderVisible(TerrainEditor.vertexControls.blue, TerrainEditor.editMode == "vertex");
    TerrainEditor.vertexControls.previewRow:SetVisible(TerrainEditor.editMode == "vertex");
    textureButton:SetVisible(TerrainEditor.editMode == "paint");
    TerrainEditor.textureText:SetVisible(TerrainEditor.editMode == "paint");
    currentTexturePreviewRow:SetVisible(TerrainEditor.editMode == "paint");
    for _, row in recentTextureRows do row:SetVisible(TerrainEditor.editMode == "paint"); end
    radiusSlider.root:SetVisible(TerrainEditor.editMode ~= "chunks");
    SetSliderVisible(strengthSlider, TerrainEditor.editMode ~= "chunks");
    SetSliderVisible(hardnessSlider, TerrainEditor.editMode ~= "chunks");
    chunkGrid:SetVisible(TerrainEditor.editMode == "chunks");
    heightFieldPathRow:SetVisible(TerrainEditor.editMode == "chunks");
    heightFieldSettingsRow:SetVisible(TerrainEditor.editMode == "chunks");
    if TerrainEditor.editMode == "chunks" then RefreshChunkGrid(); end
    TerrainEditor.RefreshCursorLayerButtons();
    RefreshRecentTextures();
    content:Invalidate();
end

TerrainEditor.modeButtons.sculpt:SetOnMouseUp(function() SetEditMode("sculpt"); end);
TerrainEditor.modeButtons.paint:SetOnMouseUp(function() SetEditMode("paint"); end);
TerrainEditor.modeButtons.vertex:SetOnMouseUp(function() SetEditMode("vertex"); end);
TerrainEditor.modeButtons.chunks:SetOnMouseUp(function() SetEditMode("chunks"); end);
textureButton:SetOnMouseUp(function()
    if TerrainEditor.activeStroke ~= nil then return; end
    local assetBrowser : any = EditorRegistry.Get("AssetBrowser");
    if assetBrowser == nil or assetBrowser.Pick == nil then return; end
    assetBrowser:Pick("texture", { ".dds", ".png" }, function(path)
        if TerrainEditor.SelectPaintTexture ~= nil then TerrainEditor.SelectPaintTexture(path); end
    end);
end);
SetEditMode("sculpt");

local methods = { };

local function EndActiveStroke(committed : boolean, context : BrushContext)
    if TerrainEditor.activeStroke == nil then return; end
    if committed then Terrain.CommitStroke(); else Terrain.CancelStroke(); end
    if TerrainEditor.selectedBrush.EndStroke then TerrainEditor.selectedBrush.EndStroke(context, committed); end
    TerrainEditor.activeStroke = nil;
    TerrainEditor.lastBrushContext = nil;
end

local MOUSE_BRUSH_ADJUSTMENT_THRESHOLD = 6.0;
local PEN_BRUSH_ADJUSTMENT_THRESHOLD = 16.0;
local MOUSE_AXIS_LOCK_MARGIN = 3.0;
local PEN_AXIS_LOCK_MARGIN = 6.0;
local RADIUS_ADJUSTMENT_PIXELS_PER_DOUBLING = 160.0;
local STRENGTH_ADJUSTMENT_PER_PIXEL = 0.25;
local HARDNESS_ADJUSTMENT_PER_PIXEL = 0.005;

local function ClearBrushAdjustment()
    TerrainEditor.brushAdjustmentArmedModifier = nil;
    TerrainEditor.brushAdjustmentActiveMode = nil;
    TerrainEditor.brushAdjustmentMovement = vec.New(0, 0);
    TerrainEditor.brushAdjustmentCancelRequested = false;
end

local function EndBrushAdjustment(cancelled : boolean)
    if TerrainEditor.brushAdjustmentActiveMode ~= nil then
        if cancelled then
            radiusSlider.slider:SetValue(TerrainEditor.brushAdjustmentStartRadius);
            strengthSlider.slider:SetValue(TerrainEditor.brushAdjustmentStartStrength);
            hardnessSlider.slider:SetValue(TerrainEditor.brushAdjustmentStartHardness);
        end
        InputAPI.EndPointerCapture();
    end
    ClearBrushAdjustment();
end

local function ApplyBrushAdjustment()
    if TerrainEditor.brushAdjustmentActiveMode == "radius" then
        radiusSlider.slider:SetValue(TerrainEditor.brushAdjustmentStartRadius * math.pow(2.0, TerrainEditor.brushAdjustmentMovement.x / RADIUS_ADJUSTMENT_PIXELS_PER_DOUBLING));
    elseif TerrainEditor.brushAdjustmentActiveMode == "strength" then
        strengthSlider.slider:SetValue(TerrainEditor.brushAdjustmentStartStrength + TerrainEditor.brushAdjustmentMovement.x * STRENGTH_ADJUSTMENT_PER_PIXEL);
    elseif TerrainEditor.brushAdjustmentActiveMode == "hardness" then
        hardnessSlider.slider:SetValue(TerrainEditor.brushAdjustmentStartHardness - TerrainEditor.brushAdjustmentMovement.y * HARDNESS_ADJUSTMENT_PER_PIXEL);
    end
end

local function GetBrushAdjustmentStatus() : string?
    if TerrainEditor.brushAdjustmentActiveMode == "radius" then return string.format("Radius: %.2f", settings.radius); end
    if TerrainEditor.brushAdjustmentActiveMode == "strength" then return string.format("%s: %.2f", if TerrainEditor.editMode == "paint" or TerrainEditor.editMode == "vertex" then "Flow" else "Strength", settings.strength); end
    if TerrainEditor.brushAdjustmentActiveMode == "hardness" then return string.format("Hardness: %.2f", settings.hardness); end
    return nil;
end

local function UpdateBrushAdjustment(state) : string?
    local altDown = InputAPI.IsDown("TerrainAdjustRadiusModifier");
    local controlDown = InputAPI.IsDown("TerrainAdjustStrengthModifier");
    local altPointerDown = InputAPI.IsDown("TerrainQuickPickTexture");
    local controlPointerDown = InputAPI.IsDown("TerrainPickLayers");
    local penInRange = InputAPI.IsPenInRange();
    local pointerInContact = not penInRange or InputAPI.IsPenInContact();

    if TerrainEditor.brushAdjustmentActiveMode ~= nil then
        if TerrainEditor.brushAdjustmentCancelRequested then
            EndBrushAdjustment(true);
            return nil;
        end

        local modifierHeld = if TerrainEditor.brushAdjustmentActiveMode == "radius" then altDown else controlDown;
        local pointerHeld = if TerrainEditor.brushAdjustmentActiveMode == "radius" then altPointerDown else controlPointerDown;
        if not modifierHeld or not pointerHeld or not pointerInContact then
            EndBrushAdjustment(false);
            return nil;
        end

        TerrainEditor.brushAdjustmentMovement += InputAPI.GetPointerDelta();
        ApplyBrushAdjustment();
        return GetBrushAdjustmentStatus();
    end

    local armedModifier : string? = nil;
    if altDown and not controlDown then
        armedModifier = "alt";
    elseif controlDown and not altDown then
        armedModifier = "control";
    end

    local pointerDown = if armedModifier == "alt" then altPointerDown else controlPointerDown;
    local canAdjust = armedModifier ~= nil and pointerDown and pointerInContact and TerrainEditor.editMode ~= "chunks" and state.cursorHit and TerrainEditor.activeStroke == nil;
    if not canAdjust then
        ClearBrushAdjustment();
        return nil;
    end

    if TerrainEditor.brushAdjustmentArmedModifier ~= armedModifier then
        TerrainEditor.brushAdjustmentArmedModifier = armedModifier;
        TerrainEditor.brushAdjustmentMovement = vec.New(0, 0);
        TerrainEditor.brushAdjustmentStartRadius = settings.radius;
        TerrainEditor.brushAdjustmentStartStrength = settings.strength;
        TerrainEditor.brushAdjustmentStartHardness = settings.hardness;
        return nil;
    end

    TerrainEditor.brushAdjustmentMovement += InputAPI.GetPointerDelta();
    local adjustmentThreshold = if penInRange then PEN_BRUSH_ADJUSTMENT_THRESHOLD else MOUSE_BRUSH_ADJUSTMENT_THRESHOLD;
    if armedModifier == "alt" then
        if math.abs(TerrainEditor.brushAdjustmentMovement.x) < adjustmentThreshold then return nil; end
        TerrainEditor.brushAdjustmentActiveMode = "radius";
    else
        local horizontalMovement = math.abs(TerrainEditor.brushAdjustmentMovement.x);
        local verticalMovement = math.abs(TerrainEditor.brushAdjustmentMovement.y);
        local axisLockMargin = if penInRange then PEN_AXIS_LOCK_MARGIN else MOUSE_AXIS_LOCK_MARGIN;
        if math.max(horizontalMovement, verticalMovement) < adjustmentThreshold or math.abs(horizontalMovement - verticalMovement) < axisLockMargin then return nil; end
        TerrainEditor.brushAdjustmentActiveMode = if horizontalMovement >= verticalMovement then "strength" else "hardness";
    end

    if TerrainEditor.brushAdjustmentActiveMode == "radius" then
        TerrainEditor.quickPickPending = false;
    else
        TerrainEditor.layerPickPending = false;
    end

    if not InputAPI.BeginPointerCapture() then
        ClearBrushAdjustment();
        return nil;
    end

    ApplyBrushAdjustment();
    return GetBrushAdjustmentStatus();
end

local function SetModeEnabled(enabled : boolean) : boolean
    if enabled == TerrainEditor.modeEnabled then return true; end
    if not enabled then EndBrushAdjustment(true); end
    if not enabled and TerrainEditor.activeStroke ~= nil then
        if TerrainEditor.lastBrushContext ~= nil then
            EndActiveStroke(false, TerrainEditor.lastBrushContext);
        else
            Terrain.CancelStroke();
            TerrainEditor.activeStroke = nil;
        end
    end
    if not Terrain.SetEnabled(enabled) then return false; end

    TerrainEditor.modeEnabled = enabled;
    root:SetVisible(enabled);
    InputAPI.SetContextActive(CONTEXT_NAME, enabled);
    if not enabled then
        textureTooltip:Hide();
        quickTexturePalette:SetVisible(false);
        chunkPanActive = false;
        ClearChunkOperation();
        TerrainEditor.activeStroke = nil;
        TerrainEditor.lastBrushContext = nil;
        TerrainEditor.previousSculptDown = false;
        TerrainEditor.previousPickDown = false;
        TerrainEditor.layerPickPending = false;
        TerrainEditor.layerPickLayers = { };
        TerrainEditor.quickPickPending = false;
        TerrainEditor.quickPickOpenRequested = false;
        TerrainEditor.quickPickLayers = { };
    end
    return true;
end

function methods:Show()
    SetModeEnabled(true);
end

function methods:Hide()
    SetModeEnabled(false);
end

function methods:IsVisible() : boolean
    return TerrainEditor.modeEnabled;
end

local function SaveTerrain() : boolean
    local saved = Terrain.Save();
    TerrainEditor.saveStatus = if saved then "Terrain saved to Pact staging" else "Terrain save failed; changes remain unsaved";
    return saved;
end

local function UndoTerrain() : boolean
    local undone = Terrain.Undo();
    if undone then TerrainEditor.saveStatus = nil; end
    return undone;
end

local function RedoTerrain() : boolean
    local redone = Terrain.Redo();
    if redone then TerrainEditor.saveStatus = nil; end
    return redone;
end

saveButton:SetOnMouseUp(function() SaveTerrain(); end);
undoButton:SetOnMouseUp(function() UndoTerrain(); end);
redoButton:SetOnMouseUp(function() RedoTerrain(); end);
exitButton:SetOnMouseUp(function() methods:Hide(); end);
window.closeButton:SetOnMouseUp(function() methods:Hide(); end);

EditorRegistry.Register("TerrainEditor", methods);

InputAPI.CreateContext(CONTEXT_NAME, GameInputPriority.Editor);
InputAPI.SetContextActive(CONTEXT_NAME, false);

InputAPI.RegisterAction("TerrainAdjustRadiusModifier", "Adjust Terrain Brush Radius", "Terrain Editor", InputAPI.Keyboard(InputAPI.KeyValues.LEFT_ALT, InputModifier.None, InputModifierMatch.Any), {
    context = CONTEXT_NAME,
    defaultReply = InputReply.Handled,
    rebindable = false,
    secondaryBinding = InputAPI.Keyboard(InputAPI.KeyValues.RIGHT_ALT, InputModifier.None, InputModifierMatch.Any),
    sortOrder = 5i,
});
InputAPI.RegisterAction("TerrainAdjustStrengthModifier", "Adjust Terrain Brush Strength", "Terrain Editor", InputAPI.Keyboard(InputAPI.KeyValues.LEFT_CONTROL, InputModifier.None, InputModifierMatch.Any), {
    context = CONTEXT_NAME,
    defaultReply = InputReply.Handled,
    rebindable = false,
    secondaryBinding = InputAPI.Keyboard(InputAPI.KeyValues.RIGHT_CONTROL, InputModifier.None, InputModifierMatch.Any),
    sortOrder = 6i,
});
InputAPI.RegisterAction("TerrainSculpt", "Apply Terrain Brush", "Terrain Editor", InputAPI.Mouse(0i, InputModifier.None, InputModifierMatch.Any), {
    context = CONTEXT_NAME,
    defaultReply = InputReply.Consumed,
    sortOrder = 10i,
});
InputAPI.RegisterAction("TerrainInvertBrush", "Invert Terrain Brush", "Terrain Editor", InputAPI.Keyboard(InputAPI.KeyValues.LEFT_SHIFT, InputModifier.None, InputModifierMatch.Any), {
    context = CONTEXT_NAME,
    defaultReply = InputReply.Handled,
    secondaryBinding = InputAPI.Keyboard(InputAPI.KeyValues.RIGHT_SHIFT, InputModifier.None, InputModifierMatch.Any),
    sortOrder = 20i,
});
InputAPI.RegisterAction("TerrainPickLayers", "Pick Terrain Texture Layers", "Terrain Editor", InputAPI.Mouse(0i, InputModifier.Control, InputModifierMatch.Exact), {
    context = CONTEXT_NAME,
    defaultReply = InputReply.Consumed,
    sortOrder = 21i,
});
local quickPickConnection = InputAPI.Bind("TerrainQuickPickTexture", "Quick Pick Terrain Texture", "Terrain Editor", InputAPI.Mouse(0i, InputModifier.Alt, InputModifierMatch.Exact), function(phase)
    if phase == InputPhase.Pressed then
        TerrainEditor.quickPickPending = TerrainEditor.editMode == "paint" and TerrainEditor.activeStroke == nil and TerrainEditor.brushAdjustmentActiveMode == nil;
        if TerrainEditor.quickPickPending then
            TerrainEditor.quickPickLayers = Terrain.GetCursorLayers();
            TerrainEditor.quickPickMousePosition = UI.GetMousePos();
        end
    elseif phase == InputPhase.Released then
        TerrainEditor.quickPickOpenRequested = TerrainEditor.quickPickPending and TerrainEditor.brushAdjustmentActiveMode == nil;
        TerrainEditor.quickPickPending = false;
    elseif phase == InputPhase.Canceled then
        TerrainEditor.quickPickPending = false;
    end
    return InputReply.Consumed;
end, {
    context = CONTEXT_NAME,
    defaultReply = InputReply.Consumed,
    sortOrder = 22i,
});
if quickPickConnection == 0i then error("TerrainEditor: failed to bind Alt+Left Mouse quick texture picker"); end

InputAPI.Bind("ToggleTerrainEditor", "Toggle Terrain Editor", "Terrain Editor", InputAPI.Keyboard(InputAPI.KeyValues.T, InputModifier.Control, InputModifierMatch.Exact), function(phase)
    if phase ~= InputPhase.Pressed then return InputReply.Ignored; end
    if TerrainEditor.brushAdjustmentActiveMode ~= nil then return InputReply.Consumed; end
    return if SetModeEnabled(not TerrainEditor.modeEnabled) then InputReply.Consumed else InputReply.Ignored;
end, { context = "Global", defaultReply = InputReply.Ignored, sortOrder = 1i });

InputAPI.Bind("ExitTerrainEditor", "Exit Terrain Editor", "Terrain Editor", InputAPI.Keyboard(InputAPI.KeyValues.ESCAPE, InputModifier.None, InputModifierMatch.Any), function(phase)
    if phase ~= InputPhase.Pressed or not TerrainEditor.modeEnabled then return InputReply.Ignored; end
    if TerrainEditor.brushAdjustmentActiveMode ~= nil then
        TerrainEditor.brushAdjustmentCancelRequested = true;
        return InputReply.Consumed;
    end
    if InputAPI.IsMouseCaptured() then return InputReply.Ignored; end
    methods:Hide();
    return InputReply.Consumed;
end, { context = CONTEXT_NAME, defaultReply = InputReply.Ignored, rebindable = false, sortOrder = 2i });

local function HandleUndoTerrainShortcut(phase) : integer
    if phase ~= InputPhase.Pressed then return InputReply.Ignored; end
    if TerrainEditor.brushAdjustmentActiveMode ~= nil then return InputReply.Consumed; end
    return if UndoTerrain() then InputReply.Consumed else InputReply.Handled;
end

local function HandleRedoTerrainShortcut(phase) : integer
    if phase ~= InputPhase.Pressed then return InputReply.Ignored; end
    if TerrainEditor.brushAdjustmentActiveMode ~= nil then return InputReply.Consumed; end
    return if RedoTerrain() then InputReply.Consumed else InputReply.Handled;
end

local undoBinding = InputAPI.Keyboard(InputAPI.KeyValues.Z, InputModifier.Control, InputModifierMatch.Exact);
InputAPI.RegisterAction("UndoTerrainEditor", "Undo Terrain Stroke", "Terrain Editor", undoBinding,
    { context = CONTEXT_NAME, defaultReply = InputReply.Handled, rebindable = false, sortOrder = 30i });
InputAPI.SetBinding("UndoTerrainEditor", 1i, undoBinding, InputBindingConflictPolicy.Allow);
InputAPI.ClearBinding("UndoTerrainEditor", 2i, InputBindingConflictPolicy.Allow);
local undoConnection = InputAPI.Connect("UndoTerrainEditor", HandleUndoTerrainShortcut);
if undoConnection == 0i then error("TerrainEditor: failed to connect undo shortcut"); end

local redoBinding = InputAPI.Keyboard(InputAPI.KeyValues.Z, integer.bor(InputModifier.Control, InputModifier.Shift), InputModifierMatch.Exact);
InputAPI.RegisterAction("RedoTerrainEditor", "Redo Terrain Stroke", "Terrain Editor", redoBinding,
    { context = CONTEXT_NAME, defaultReply = InputReply.Handled, rebindable = false, sortOrder = 31i });
InputAPI.SetBinding("RedoTerrainEditor", 1i, redoBinding, InputBindingConflictPolicy.Allow);
InputAPI.ClearBinding("RedoTerrainEditor", 2i, InputBindingConflictPolicy.Allow);
local redoConnection = InputAPI.Connect("RedoTerrainEditor", HandleRedoTerrainShortcut);
if redoConnection == 0i then error("TerrainEditor: failed to connect redo shortcut"); end

InputAPI.Bind("SaveTerrainEditor", "Save Terrain", "Terrain Editor", InputAPI.Keyboard(InputAPI.KeyValues.S, InputModifier.Control, InputModifierMatch.AtLeast), function(phase)
    if phase ~= InputPhase.Pressed then return InputReply.Ignored; end
    if TerrainEditor.brushAdjustmentActiveMode ~= nil then return InputReply.Consumed; end
    return if SaveTerrain() then InputReply.Consumed else InputReply.Handled;
end, { context = CONTEXT_NAME, defaultReply = InputReply.Handled, sortOrder = 32i });

RegisterEvent(GameEvent.Updated, function(_, data)
    local state = Terrain.GetState();
    if state == nil then
        EndBrushAdjustment(true);
        return;
    end

    if TerrainEditor.modeEnabled and not state.enabled then
        if TerrainEditor.activeStroke ~= nil and TerrainEditor.lastBrushContext ~= nil then
            EndActiveStroke(false, TerrainEditor.lastBrushContext);
        end
        EndBrushAdjustment(true);
        TerrainEditor.modeEnabled = false;
        root:SetVisible(false);
        InputAPI.SetContextActive(CONTEXT_NAME, false);
        TerrainEditor.activeStroke = nil;
    end

    if not TerrainEditor.modeEnabled then return; end

    local brushAdjustmentStatus = UpdateBrushAdjustment(state);
    Terrain.SetPreviewRadius(settings.radius);

    local readyToPaint = TerrainEditor.editMode ~= "paint" or TerrainEditor.selectedTexturePath ~= nil;
    if brushAdjustmentStatus ~= nil then
        statusText:SetText(brushAdjustmentStatus);
    elseif TerrainEditor.editMode == "chunks" and not state.layoutAvailable then
        statusText:SetText("Waiting for a loaded map");
    elseif TerrainEditor.editMode == "chunks" then
        local statusX = hoveredChunkX or selectedChunkX;
        local statusY = hoveredChunkY or selectedChunkY;
        if statusX ~= nil and statusY ~= nil then
            local occupancy = if occupiedChunks[ChunkID(statusX, statusY)] then "Occupied" else "Empty";
            statusText:SetText(`Chunk {statusX}, {statusY} - {occupancy} - Shift drag add, Ctrl drag remove`);
        else
            local firstX = math.max(0, math.floor(-chunkViewOffsetX / chunkCellSize));
            local firstY = math.max(0, math.floor(-chunkViewOffsetY / chunkCellSize));
            local lastX = math.min(chunkGridStrideNumber - 1, math.ceil((CHUNK_GRID_SIZE - chunkViewOffsetX) / chunkCellSize) - 1);
            local lastY = math.min(chunkGridStrideNumber - 1, math.ceil((CHUNK_GRID_SIZE - chunkViewOffsetY) / chunkCellSize) - 1);
            statusText:SetText(`X {firstX}-{lastX}, Y {firstY}-{lastY} - Shift drag add, Ctrl drag remove`);
        end
    elseif not state.available then
        statusText:SetText("Waiting for loaded terrain");
    elseif not state.cursorHit then
        statusText:SetText("Point at loaded terrain");
    elseif TerrainEditor.editMode == "paint" and not readyToPaint then
        statusText:SetText("Choose a texture to begin painting");
    elseif TerrainEditor.editMode == "paint" and integer.gt(state.blockedPaintCellCount, 0i) then
        statusText:SetText(if TerrainEditor.paintTargetLayer == 0 then `{state.blockedPaintCellCount} cell(s) already use four active texture layers` else `{state.blockedPaintCellCount} cell(s) need earlier texture layers assigned first`);
    else
        statusText:SetText(TerrainEditor.selectedBrush.displayName);
    end
    if TerrainEditor.saveStatus ~= nil then
        dirtyText:SetText(TerrainEditor.saveStatus);
    else
        dirtyText:SetText(if state.dirtyChunkCount == 0i and not state.topologyDirty then "No unsaved terrain changes" else `{state.dirtyChunkCount} unsaved chunk file(s){if state.topologyDirty then ", map layout changed" else ""}`);
    end
    saveButton:SetInteractable((state.dirtyChunkCount ~= 0i or state.topologyDirty) and not state.strokeActive);
    undoButton:SetInteractable(state.canUndo);
    redoButton:SetInteractable(state.canRedo);

    local pickDown = InputAPI.IsDown("TerrainPickLayers");
    if pickDown and not TerrainEditor.previousPickDown then
        TerrainEditor.layerPickPending = TerrainEditor.editMode == "paint" and TerrainEditor.activeStroke == nil and state.cursorHit;
        if TerrainEditor.layerPickPending then TerrainEditor.layerPickLayers = Terrain.GetCursorLayers(); end
    elseif not pickDown and TerrainEditor.previousPickDown then
        if TerrainEditor.layerPickPending and TerrainEditor.brushAdjustmentActiveMode == nil then
            TerrainEditor.cursorLayers = TerrainEditor.layerPickLayers;
            TerrainEditor.RefreshCursorLayerButtons();
        end
        TerrainEditor.layerPickPending = false;
    end

    local quickPickDown = InputAPI.IsDown("TerrainQuickPickTexture");
    if TerrainEditor.quickPickOpenRequested then
        TerrainEditor.quickPickOpenRequested = false;
        OpenQuickTextureMenu(TerrainEditor.quickPickLayers, TerrainEditor.quickPickMousePosition);
    end

    if TerrainEditor.editMode == "chunks" and state.layoutGeneration ~= observedLayoutGeneration then RefreshChunkGrid(); end

    local sculptDown = TerrainEditor.brushAdjustmentArmedModifier == nil and TerrainEditor.brushAdjustmentActiveMode == nil and TerrainEditor.editMode ~= "chunks" and InputAPI.IsDown("TerrainSculpt") and not pickDown and not quickPickDown and not TerrainEditor.quickPickPending and state.cursorHit and readyToPaint;
    local penPressure = if InputAPI.IsPenInContact() then InputAPI.GetPenPressure() else 1.0;
    local context =
    {
        position = state.cursorPosition,
        deltaTime = data.deltaTime,
        radius = settings.radius,
        strength = settings.strength * penPressure,
        hardness = settings.hardness,
        targetOpacity = settings.opacity,
        targetColor = vec.New(settings.vertexRed / 255.0, settings.vertexGreen / 255.0, settings.vertexBlue / 255.0),
        inverted = InputAPI.IsDown("TerrainInvertBrush"),
        stroke = TerrainEditor.activeStroke or { },
    };
    TerrainEditor.lastBrushContext = context;

    if sculptDown and not TerrainEditor.previousSculptDown and state.cursorHit then
        TerrainEditor.saveStatus = nil;
        TerrainEditor.activeStroke = { };
        context.stroke = TerrainEditor.activeStroke;
        if Terrain.BeginStroke(TerrainEditor.selectedBrush.displayName) then
            if TerrainEditor.selectedBrush.BeginStroke then TerrainEditor.selectedBrush.BeginStroke(context); end
        else
            TerrainEditor.activeStroke = nil;
        end
    end

    if sculptDown and TerrainEditor.activeStroke ~= nil and state.cursorHit then
        context.stroke = TerrainEditor.activeStroke;
        TerrainEditor.selectedBrush.Apply(context);
    elseif not sculptDown and TerrainEditor.previousSculptDown then
        EndActiveStroke(true, context);
    elseif TerrainEditor.activeStroke ~= nil and not state.available then
        EndActiveStroke(false, context);
    end

    TerrainEditor.previousSculptDown = sculptDown;
    TerrainEditor.previousPickDown = pickDown;
end);
