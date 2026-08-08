local Button = require("@src/API/UI/Button");
local EditorRegistry = require("@src/API/Editor/Registry");
local EditorWindow = require("@src/API/Editor/UI/Window");
local InputAPI = require("@src/API/Input/Input");
local UIHorizontalLayout = require("@src/API/UI/HorizontalLayout");
local Slider = require("@src/API/UI/Slider");
local Terrain = require("@src/API/Editor/Terrain");
local TerrainBrushes = require("@src/API/Editor/TerrainBrushes");
local UIVerticalLayout = require("@src/API/UI/VerticalLayout");

type Brush = TerrainBrushes.Brush;
type BrushContext = TerrainBrushes.Context;
type StrokeState = TerrainBrushes.StrokeState;
type TextureLayer = Terrain.TextureLayer;

TerrainBrushes.Register(require("@src/API/Editor/TerrainBrushes/RaiseLower"));
TerrainBrushes.Register(require("@src/API/Editor/TerrainBrushes/Flatten"));
TerrainBrushes.Register(require("@src/API/Editor/TerrainBrushes/Smooth"));
TerrainBrushes.Register(require("@src/API/Editor/TerrainBrushes/TexturePaint"));

local CONTEXT_NAME = "TerrainEditor";
local PANEL_WIDTH = 300;
local TITLE_HEIGHT = 20;
local LAYER = 1200i;

local settings =
{
    radius = 12.0,
    strength = 10.0,
    hardness = 0.5,
    opacity = 1.0,
};

local defaultBrush = TerrainBrushes.Get("raise_lower");
if defaultBrush == nil then error("TerrainEditor: the default raise/lower brush is not registered"); end
local selectedBrush : Brush = defaultBrush;
local sculptBrush : Brush = defaultBrush;
local textureBrush = TerrainBrushes.Get("texture_paint");
if textureBrush == nil then error("TerrainEditor: the texture paint brush is not registered"); end
local paintBrush : Brush = textureBrush;
local editMode = "sculpt";
local selectedTexturePath : string? = nil;
local cursorLayers : { TextureLayer } = { };
local activeStroke : StrokeState? = nil;
local lastBrushContext : BrushContext? = nil;
local saveStatus : string? = nil;
local previousSculptDown = false;
local previousPickDown = false;
local modeEnabled = false;
local textureText : any = nil;

local canvas = UI.GetCanvas("TerrainEditor", vec.New(0, 0), vec.New(1920, 1080), false);
local window = EditorWindow.New(canvas, vec.New(1580, -70), vec.New(PANEL_WIDTH, 450), LAYER, { title = "Terrain Editor", titleHeight = TITLE_HEIGHT });
local root = window.root;
root:SetVisible(false);

local content = UIVerticalLayout.New(root, vec.New(0, -TITLE_HEIGHT), vec.New(PANEL_WIDTH, 430), 1i, {
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
local sculptModeButton = Button.New(modeRow, vec.New(0, 0), vec.New(136, 22), 1i, { text = "Sculpt", textTemplate = "DevToolsText" });
local paintModeButton = Button.New(modeRow, vec.New(0, 0), vec.New(136, 22), 1i, { text = "Texture Paint", textTemplate = "DevToolsText" });
modeRow:SetChildOpts(sculptModeButton, { modeX = "flex", sizeY = 22 });
modeRow:SetChildOpts(paintModeButton, { modeX = "flex", sizeY = 22 });
sculptModeButton:SetHighlighted(true);

local brushRow = UIHorizontalLayout.New(content, vec.New(0, 0), vec.New(PANEL_WIDTH - 20, 22), 1i, { alpha = 0.0, spacing = 4 });
content:SetChildOpts(brushRow, { modeX = "flex", sizeY = 22 });
local brushButtons = { };
for _, brush in TerrainBrushes.GetAll() do
    if brush.id == "texture_paint" then continue; end
    local button = Button.New(brushRow, vec.New(0, 0), vec.New(90, 22), 1i, { text = brush.displayName, textTemplate = "DevToolsText" });
    brushRow:SetChildOpts(button, { modeX = "flex", sizeY = 22 });
    button:SetOnMouseUp(function()
        if activeStroke ~= nil then return; end
        sculptBrush = brush;
        if editMode == "sculpt" then selectedBrush = brush; end
        for _, candidate in brushButtons do
            candidate.button:SetHighlighted(candidate.brush == sculptBrush);
        end
    end);
    table.insert(brushButtons, { button = button, brush = brush });
end
brushButtons[1].button:SetHighlighted(true);

local cursorLayerRow = UIHorizontalLayout.New(content, vec.New(0, 0), vec.New(PANEL_WIDTH - 20, 22), 1i, { alpha = 0.0, spacing = 4 });
content:SetChildOpts(cursorLayerRow, { modeX = "flex", sizeY = 22 });
local cursorLayerButtons = { };
for index = 1, 4 do
    local button = Button.New(cursorLayerRow, vec.New(0, 0), vec.New(66, 22), 1i, { text = `Layer {index}`, textTemplate = "DevToolsText" });
    cursorLayerRow:SetChildOpts(button, { modeX = "flex", sizeY = 22 });
    button:SetVisible(false);
    button:SetOnMouseUp(function()
        if activeStroke ~= nil then return; end
        local layer = cursorLayers[index];
        if layer == nil or layer.path == "" or not Terrain.SetPaintTexture(layer.path) then return; end
        selectedTexturePath = layer.path;
        textureText:SetText(layer.path:match("([^/]+)$") or layer.path);
    end);
    table.insert(cursorLayerButtons, button);
end

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

local textureButton = Button.New(content, vec.New(0, 0), vec.New(PANEL_WIDTH - 20, 22), 1i, { text = "Choose Texture...", textTemplate = "DevToolsText" });
content:SetChildOpts(textureButton, { modeX = "flex", sizeY = 22 });
textureText = content:NewText("No texture selected", vec.New(0, 0), 1i, "DevToolsText");
content:SetChildOpts(textureText, { modeX = "flex", sizeY = 18 });

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

local function RefreshCursorLayerButtons()
    for index, button in cursorLayerButtons do
        local layer = cursorLayers[index];
        button:SetVisible(editMode == "paint" and layer ~= nil);
        if layer ~= nil then
            button:SetText(`L{index} {math.floor(layer.averageWeight * 100.0 + 0.5)}%`);
            button:SetHighlighted(selectedTexturePath == layer.path);
        end
    end
    cursorLayerRow:Invalidate();
end

local function SetEditMode(nextMode : string)
    if activeStroke ~= nil or (nextMode ~= "sculpt" and nextMode ~= "paint") then return; end
    editMode = nextMode;
    selectedBrush = if editMode == "paint" then paintBrush else sculptBrush;
    sculptModeButton:SetHighlighted(editMode == "sculpt");
    paintModeButton:SetHighlighted(editMode == "paint");
    for _, candidate in brushButtons do
        candidate.button:SetVisible(editMode == "sculpt");
    end
    brushRow:SetVisible(editMode == "sculpt");
    cursorLayerRow:SetVisible(editMode == "paint");
    strengthSlider.label:SetText(if editMode == "paint" then "Pressure" else "Strength");
    SetSliderVisible(opacitySlider, editMode == "paint");
    textureButton:SetVisible(editMode == "paint");
    textureText:SetVisible(editMode == "paint");
    RefreshCursorLayerButtons();
    content:Invalidate();
end

sculptModeButton:SetOnMouseUp(function() SetEditMode("sculpt"); end);
paintModeButton:SetOnMouseUp(function() SetEditMode("paint"); end);
textureButton:SetOnMouseUp(function()
    if activeStroke ~= nil then return; end
    local assetBrowser : any = EditorRegistry.Get("AssetBrowser");
    if assetBrowser == nil or assetBrowser.Pick == nil then return; end
    assetBrowser:Pick("texture", ".dds", function(path)
        if not Terrain.SetPaintTexture(path) then return; end
        selectedTexturePath = path;
        textureText:SetText(path:match("([^/]+)$") or path);
        RefreshCursorLayerButtons();
    end);
end);
SetEditMode("sculpt");

local methods = { };

local function EndActiveStroke(committed : boolean, context : BrushContext)
    if activeStroke == nil then return; end
    if committed then Terrain.CommitStroke(); else Terrain.CancelStroke(); end
    if selectedBrush.EndStroke then selectedBrush.EndStroke(context, committed); end
    activeStroke = nil;
    lastBrushContext = nil;
end

local function SetModeEnabled(enabled : boolean) : boolean
    if enabled == modeEnabled then return true; end
    if not enabled and activeStroke ~= nil then
        if lastBrushContext ~= nil then
            EndActiveStroke(false, lastBrushContext);
        else
            Terrain.CancelStroke();
            activeStroke = nil;
        end
    end
    if not Terrain.SetEnabled(enabled) then return false; end

    modeEnabled = enabled;
    root:SetVisible(enabled);
    InputAPI.SetContextActive(CONTEXT_NAME, enabled);
    if not enabled then
        activeStroke = nil;
        lastBrushContext = nil;
        previousSculptDown = false;
        previousPickDown = false;
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
    return modeEnabled;
end

local function SaveTerrain() : boolean
    local saved = Terrain.Save();
    saveStatus = if saved then "Terrain saved to Pact staging" else "Terrain save failed; changes remain unsaved";
    return saved;
end

local function UndoTerrain() : boolean
    local undone = Terrain.Undo();
    if undone then saveStatus = nil; end
    return undone;
end

local function RedoTerrain() : boolean
    local redone = Terrain.Redo();
    if redone then saveStatus = nil; end
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

InputAPI.RegisterAction("TerrainSculpt", "Sculpt Terrain", "Terrain Editor", InputAPI.Mouse(0i, InputModifier.None, InputModifierMatch.Any), {
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

InputAPI.Bind("ToggleTerrainEditor", "Toggle Terrain Editor", "Terrain Editor", InputAPI.Keyboard(InputAPI.KeyValues.T, InputModifier.Control, InputModifierMatch.Exact), function(phase)
    if phase ~= InputPhase.Pressed then return InputReply.Ignored; end
    return if SetModeEnabled(not modeEnabled) then InputReply.Consumed else InputReply.Ignored;
end, { context = "Global", defaultReply = InputReply.Ignored, sortOrder = 1i });

InputAPI.Bind("ExitTerrainEditor", "Exit Terrain Editor", "Terrain Editor", InputAPI.Keyboard(InputAPI.KeyValues.ESCAPE, InputModifier.None, InputModifierMatch.Any), function(phase)
    if phase ~= InputPhase.Pressed or not modeEnabled then return InputReply.Ignored; end
    if InputAPI.IsMouseCaptured() then return InputReply.Ignored; end
    methods:Hide();
    return InputReply.Consumed;
end, { context = CONTEXT_NAME, defaultReply = InputReply.Ignored, rebindable = false, sortOrder = 2i });

InputAPI.Bind("UndoTerrainEditor", "Undo Terrain Stroke", "Terrain Editor", InputAPI.Keyboard(InputAPI.KeyValues.Z, InputModifier.Control, InputModifierMatch.AtLeast), function(phase)
    if phase ~= InputPhase.Pressed then return InputReply.Ignored; end
    return if UndoTerrain() then InputReply.Consumed else InputReply.Handled;
end, { context = CONTEXT_NAME, defaultReply = InputReply.Handled, sortOrder = 30i });

InputAPI.Bind("RedoTerrainEditor", "Redo Terrain Stroke", "Terrain Editor", InputAPI.Keyboard(InputAPI.KeyValues.Y, InputModifier.Control, InputModifierMatch.AtLeast), function(phase)
    if phase ~= InputPhase.Pressed then return InputReply.Ignored; end
    return if RedoTerrain() then InputReply.Consumed else InputReply.Handled;
end, { context = CONTEXT_NAME, defaultReply = InputReply.Handled, sortOrder = 31i });

InputAPI.Bind("SaveTerrainEditor", "Save Terrain", "Terrain Editor", InputAPI.Keyboard(InputAPI.KeyValues.S, InputModifier.Control, InputModifierMatch.AtLeast), function(phase)
    if phase ~= InputPhase.Pressed then return InputReply.Ignored; end
    return if SaveTerrain() then InputReply.Consumed else InputReply.Handled;
end, { context = CONTEXT_NAME, defaultReply = InputReply.Handled, sortOrder = 32i });

RegisterEvent(GameEvent.Updated, function(_, data)
    local state = Terrain.GetState();
    if state == nil then return; end

    if modeEnabled and not state.enabled then
        if activeStroke ~= nil and lastBrushContext ~= nil then
            EndActiveStroke(false, lastBrushContext);
        end
        modeEnabled = false;
        root:SetVisible(false);
        InputAPI.SetContextActive(CONTEXT_NAME, false);
        activeStroke = nil;
    end

    if not modeEnabled then return; end

    Terrain.SetPreviewRadius(settings.radius);

    local readyToPaint = editMode ~= "paint" or selectedTexturePath ~= nil;
    if not state.available then
        statusText:SetText("Waiting for a loaded map");
    elseif not state.cursorHit then
        statusText:SetText("Point at loaded terrain");
    elseif editMode == "paint" and not readyToPaint then
        statusText:SetText("Choose a texture to begin painting");
    elseif editMode == "paint" and integer.gt(state.blockedPaintCellCount, 0i) then
        statusText:SetText(`{state.blockedPaintCellCount} cell(s) already use four painted layers`);
    else
        statusText:SetText(selectedBrush.displayName);
    end
    if saveStatus ~= nil then
        dirtyText:SetText(saveStatus);
    else
        dirtyText:SetText(if state.dirtyChunkCount == 0i then "No unsaved terrain changes" else `{state.dirtyChunkCount} unsaved terrain chunk(s)`);
    end
    saveButton:SetInteractable(state.dirtyChunkCount ~= 0i and not state.strokeActive);
    undoButton:SetInteractable(state.canUndo);
    redoButton:SetInteractable(state.canRedo);

    local pickDown = editMode == "paint" and InputAPI.IsDown("TerrainPickLayers") and state.cursorHit;
    if pickDown and not previousPickDown then
        cursorLayers = Terrain.GetCursorLayers();
        RefreshCursorLayerButtons();
    end

    local sculptDown = InputAPI.IsDown("TerrainSculpt") and not pickDown and state.cursorHit and readyToPaint;
    local context =
    {
        position = state.cursorPosition,
        deltaTime = data.deltaTime,
        radius = settings.radius,
        strength = settings.strength,
        hardness = settings.hardness,
        targetOpacity = settings.opacity,
        inverted = InputAPI.IsDown("TerrainInvertBrush"),
        stroke = activeStroke or { },
    };
    lastBrushContext = context;

    if sculptDown and not previousSculptDown and state.cursorHit then
        saveStatus = nil;
        activeStroke = { };
        context.stroke = activeStroke;
        if Terrain.BeginStroke(selectedBrush.displayName) then
            if selectedBrush.BeginStroke then selectedBrush.BeginStroke(context); end
        else
            activeStroke = nil;
        end
    end

    if sculptDown and activeStroke ~= nil and state.cursorHit then
        context.stroke = activeStroke;
        selectedBrush.Apply(context);
    elseif not sculptDown and previousSculptDown then
        EndActiveStroke(true, context);
    elseif activeStroke ~= nil and not state.available then
        EndActiveStroke(false, context);
    end

    previousSculptDown = sculptDown;
    previousPickDown = pickDown;
end);
