local UIButton = require("@src/API/UI/Button");
local KeyValues = require("@src/API/Input/Input").KeyValues;
local RootedWidget = require("@src/API/UI/RootedWidget");

export type Options =
{
    panelTemplate : string?,
    textTemplate : string?,
    defaultText : string?,
    defaultTextFontSize : number?,
    placeholderText : string?,
    placeholderTextColor : vec?,
    numericOnly : boolean?,
    secure : boolean?,
    multiline : boolean?,
    textInsetX : number?,
}

export type InputBox =
{
    root : Panel,
    button : UIButton.Button,
    text : string,
    GetText : (self : InputBox) -> string,
    SetText : (self : InputBox, text : string) -> (),
    Focus : (self : InputBox) -> (),
    Unfocus : (self : InputBox) -> (),
    ForceRefresh : (self : InputBox) -> (),
    SetAnchor : (self : InputBox, anchor : vec) -> (),
    SetRelativePoint : (self : InputBox, point : vec) -> (),
    SetPos : (self : InputBox, position : vec) -> (),
    SetPosX : (self : InputBox, x : number) -> (),
    SetPosY : (self : InputBox, y : number) -> (),
    SetTextVerticallyCentered : (self : InputBox) -> (),
    SetOnSubmit : (self : InputBox, callback : (InputBox) -> boolean?) -> (),
    SetOnTab : (self : InputBox, callback : (InputBox) -> ()) -> (),
    SetOnFocusBegin : (self : InputBox, callback : (InputBox) -> ()) -> (),
    SetOnFocusEnd : (self : InputBox, callback : (InputBox) -> ()) -> (),
    SetOnTextChanged : (self : InputBox, callback : (InputBox) -> ()) -> (),
    SetOnMouseDown : (self : InputBox, callback : MouseEventCallback?) -> (),
    SetOnMouseUp : (self : InputBox, callback : MouseEventCallback?) -> (),
    SetOnMouseHeld : (self : InputBox, callback : MouseEventCallback?) -> (),
}

export type InputBoxAPI =
{
    New : (parent : Widget, position : vec, size : vec, layer : integer, options : Options?) -> InputBox,
}

local inputBoxAPI : InputBoxAPI = { };
--[[
usage:
.New(panel, vec.New(0, -170), vec.New(200, 40), 0, {
        panelTemplate = "DialogBox",
        textTemplate = "DefaultButtonText",
        defaultText = "Test",
        numericOnly = true,
        secure = true,
        multiline = false,
    });
]]

local function GetBytePositionFromCharacterPosition(s : string, charPos : number) : number
    if charPos == 0 then
        return 1;
    end

    return utf8.offset(s, charPos + 1) or (#s + 1);
end

local function GetText(inputBoxTable) : string
    local text = inputBoxTable.text;
    return if type(text) == "string" then text else tostring(text);
end

local function GetTextLength(inputBoxTable) : number
    return utf8.len(GetText(inputBoxTable)) or 0;
end

local function MeasureText(inputBoxTable, text : string) : vec
    return UI.CalculateTextSize(text, inputBoxTable.textTemplate, inputBoxTable.button.text:GetFontSize());
end

local function MeasureTextAdvance(inputBoxTable, text : string) : number
    return math.max(0, MeasureText(inputBoxTable, text).x - inputBoxTable.textMeasurementPaddingX);
end

local function GetCaretHeight(inputBoxTable) : number
    return math.max(1, math.ceil(inputBoxTable.button.text:GetFontSize()));
end

local function GetLineHeight(inputBoxTable) : number
    return math.max(1, MeasureText(inputBoxTable, "Ag\nAg").y - MeasureText(inputBoxTable, "Ag").y);
end

local function GetSelectionRange(inputBoxTable) : (number?, number?)
    local anchor = inputBoxTable.selectionAnchor;
    if anchor == nil or anchor == inputBoxTable.cursorPos then return nil, nil; end

    return math.min(anchor, inputBoxTable.cursorPos), math.max(anchor, inputBoxTable.cursorPos);
end

local function HideSelection(inputBoxTable)
    for _, panel in inputBoxTable.selectionPanels do
        panel:SetVisible(false);
    end
end

local function GetSelectionPanel(inputBoxTable, index : number) : Panel
    local panel = inputBoxTable.selectionPanels[index];
    if panel ~= nil then return panel; end

    panel = inputBoxTable.button.root:NewPanel(vec.New(0, 0), vec.New(1, 1), 0i);
    panel:SetColor(vec.New(0.18, 0.48, 0.90));
    panel:SetAlpha(0.55);
    panel:SetInteractable(false);
    table.insert(inputBoxTable.selectionPanels, panel);
    return panel;
end

local function GetWrappedVisualPosition(inputBoxTable, originalText : string, wrappedText : string, characterPosition : number) : (number, number)
    local targetByteCount = GetBytePositionFromCharacterPosition(originalText, characterPosition) - 1;
    local originalBytePosition = 1;
    local wrappedBytePosition = 1;

    while originalBytePosition <= targetByteCount and wrappedBytePosition <= #wrappedText do
        local wrappedCharacter = wrappedText:sub(wrappedBytePosition, wrappedBytePosition);
        local originalCharacter = originalText:sub(originalBytePosition, originalBytePosition);
        if wrappedCharacter == "\n" and originalCharacter ~= "\n" then
            wrappedBytePosition += 1;
        else
            wrappedBytePosition += 1;
            originalBytePosition += 1;
        end
    end

    local wrappedPrefix = wrappedText:sub(1, wrappedBytePosition - 1);
    local lineIndex = 1;
    for _ in wrappedPrefix:gmatch("\n") do
        lineIndex += 1;
    end

    local linePrefix = wrappedPrefix:match("([^\n]*)$") or "";
    return lineIndex, MeasureTextAdvance(inputBoxTable, linePrefix);
end

local function GetWrappedLine(wrappedText : string, targetLine : number) : string
    local lineIndex = 1;
    for line in (wrappedText .. "\n"):gmatch("(.-)\n") do
        if lineIndex == targetLine then return line; end
        lineIndex += 1;
    end

    return "";
end

local function RefreshText(inputBoxTable)
    local text = GetText(inputBoxTable);

    local cursorPos = inputBoxTable.cursorPos;

    local isShowingPlaceholder = text == ""
        and not inputBoxTable.isFocused
        and inputBoxTable.placeholderText ~= "";
    local displayText = if isShowingPlaceholder then inputBoxTable.placeholderText else text;
    local cursorCharPos = cursorPos;

    if inputBoxTable.placeholderTextColor ~= nil then
        if isShowingPlaceholder then
            if not inputBoxTable.isShowingPlaceholder then inputBoxTable.activeTextColor = inputBoxTable.button.text:GetColor(); end
            inputBoxTable.button.text:SetColor(inputBoxTable.placeholderTextColor);
        elseif inputBoxTable.isShowingPlaceholder and inputBoxTable.activeTextColor ~= nil then
            inputBoxTable.button.text:SetColor(inputBoxTable.activeTextColor);
        end
    end
    inputBoxTable.isShowingPlaceholder = isShowingPlaceholder;
    inputBoxTable.caret:SetColor(inputBoxTable.button.text:GetColor());
    inputBoxTable.textMeasurementPaddingX = MeasureText(inputBoxTable, "").x;

    local totalTextWidth = MeasureText(inputBoxTable, displayText).x;

    -- Initialize variables for text clipping
    local startCharPos = 0;
    local endCharPos = utf8.len(text);
    local textInsetX = inputBoxTable.textInsetX or 0;
    local maxVisibleWidth = inputBoxTable.inputBoxWidth - textInsetX * 2;

    if inputBoxTable.multiline then
        if inputBoxTable.secure and not isShowingPlaceholder then
            displayText = string.rep("*", utf8.len(displayText) or 0);
        end

        inputBoxTable.button:SetText(displayText);
        inputBoxTable.button.text:SetWrapWidth(maxVisibleWidth);
        inputBoxTable.button.text:SetAlpha(isShowingPlaceholder and 0.55 or 1.0);

        HideSelection(inputBoxTable);
        inputBoxTable.caret:SetVisible(false);
        if isShowingPlaceholder then return; end

        local wrappedText = UI.WrapText(displayText, inputBoxTable.textTemplate, maxVisibleWidth, inputBoxTable.button.text:GetFontSize());
        local lineHeight = GetLineHeight(inputBoxTable);
        local caretHeight = GetCaretHeight(inputBoxTable);
        local caretLine, caretX = GetWrappedVisualPosition(inputBoxTable, displayText, wrappedText, cursorPos);
        inputBoxTable.caret:SetAnchor(vec.New(0.0, 1.0));
        inputBoxTable.caret:SetRelativePoint(vec.New(0.0, 1.0));
        inputBoxTable.caret:SetPos(vec.New(textInsetX + caretX, -6 - (caretLine - 1) * lineHeight - (lineHeight - caretHeight) / 2));
        inputBoxTable.caret:SetSize(vec.New(1, caretHeight));
        inputBoxTable.caret:SetVisible(inputBoxTable.isFocused and inputBoxTable.cursorBlinkState);

        local selectionStart, selectionEnd = GetSelectionRange(inputBoxTable);
        if not inputBoxTable.isFocused or selectionStart == nil or selectionEnd == nil then return; end

        local startLine, startX = GetWrappedVisualPosition(inputBoxTable, displayText, wrappedText, selectionStart);
        local endLine, endX = GetWrappedVisualPosition(inputBoxTable, displayText, wrappedText, selectionEnd);
        local panelIndex = 1;
        for lineIndex = startLine, endLine do
            local lineStartX = if lineIndex == startLine then startX else 0;
            local lineEndX = if lineIndex == endLine then endX else MeasureTextAdvance(inputBoxTable, GetWrappedLine(wrappedText, lineIndex));
            local panel = GetSelectionPanel(inputBoxTable, panelIndex);
            panel:SetAnchor(vec.New(0.0, 1.0));
            panel:SetRelativePoint(vec.New(0.0, 1.0));
            panel:SetPos(vec.New(textInsetX + lineStartX, -6 - (lineIndex - 1) * lineHeight));
            panel:SetSize(vec.New(math.max(1, lineEndX - lineStartX), lineHeight));
            panel:SetVisible(true);
            panelIndex += 1;
        end
        return;
    end

    -- Handle text clipping based on pixel width
    if totalTextWidth > maxVisibleWidth and not isShowingPlaceholder then
        -- Build an array of character widths and cumulative widths
        inputBoxTable.charWidths = {};
        local cumulativeWidths = {};
        local cumulativeWidth = 0;
        for i = 1, utf8.len(text) do
            local charBytePosStart = utf8.offset(text, i);
            local charBytePosEnd = (utf8.offset(text, i + 1) or (#text + 1)) - 1;
            local char = text:sub(charBytePosStart, charBytePosEnd);
            local charWidth = MeasureTextAdvance(inputBoxTable, char);
            table.insert(inputBoxTable.charWidths, charWidth);
            table.insert(cumulativeWidths, cumulativeWidth);
            cumulativeWidth = cumulativeWidth + charWidth;
        end
        table.insert(cumulativeWidths, cumulativeWidth); -- Add the end position

        -- Find the range of characters that fit within the max visible width around the cursor
        local cursorPixelPos = cumulativeWidths[cursorPos + 1] or 0;
        local halfVisibleWidth = maxVisibleWidth / 2;
        local startPixelPos = cursorPixelPos - halfVisibleWidth;
        local endPixelPos = cursorPixelPos + halfVisibleWidth;

        -- Adjust start and end positions if they exceed the text boundaries
        if startPixelPos < 0 then
            endPixelPos = endPixelPos - startPixelPos;
            startPixelPos = 0;
        end
        if endPixelPos > cumulativeWidth then
            startPixelPos = startPixelPos - (endPixelPos - cumulativeWidth);
            endPixelPos = cumulativeWidth;
            if startPixelPos < 0 then
                startPixelPos = 0;
            end
        end

        -- Find startCharPos and endCharPos based on pixel positions
        for i = 1, #cumulativeWidths do
            if cumulativeWidths[i] >= startPixelPos then
                startCharPos = i - 1;
                break;
            end
        end
        for i = startCharPos + 1, #cumulativeWidths do
            if cumulativeWidths[i] >= endPixelPos then
                endCharPos = i - 1;
                break;
            end
        end

        -- Get byte positions for start and end
        local startBytePos = GetBytePositionFromCharacterPosition(text, startCharPos);
        local endBytePos = GetBytePositionFromCharacterPosition(text, endCharPos);
        displayText = text:sub(startBytePos, endBytePos - 1);
        cursorCharPos = cursorPos - startCharPos;
    else
        cursorCharPos = cursorPos;
    end

    -- If secure is true, replace displayText with asterisks
    if inputBoxTable.secure and not isShowingPlaceholder then
        local len = utf8.len(displayText);
        displayText = string.rep("*", len);
    end

    -- Store startCharPos and endCharPos for use in mouse click handling
    inputBoxTable.startCharPos = startCharPos;
    inputBoxTable.endCharPos = endCharPos;

    local displayTextWidth = MeasureText(inputBoxTable, displayText).x;

    -- Get text alignment from inputBoxTable
    local buttonTextRelativePointX = inputBoxTable.buttonTextRelativePointX or 0.0;

    -- Calculate textStartX based on alignment
    local textStartX = textInsetX + (maxVisibleWidth - displayTextWidth) * buttonTextRelativePointX;
    inputBoxTable.textStartX = textStartX;

    -- Build an array of character positions for the displayed text
    inputBoxTable.charPositions = {};
    local cumulativeWidth = 0;
    for i = 1, utf8.len(displayText) do
        local charBytePosStart = utf8.offset(displayText, i);
        local charBytePosEnd = (utf8.offset(displayText, i + 1) or (#displayText + 1)) - 1;
        local char = displayText:sub(charBytePosStart, charBytePosEnd);
        local charWidth = MeasureTextAdvance(inputBoxTable, char);
        table.insert(inputBoxTable.charPositions, cumulativeWidth);
        cumulativeWidth = cumulativeWidth + charWidth;
    end
    -- Add the end position
    table.insert(inputBoxTable.charPositions, cumulativeWidth);

    inputBoxTable.button:SetText(displayText);
    inputBoxTable.button.text:SetAlpha(isShowingPlaceholder and 0.55 or 1.0);

    local lineHeight = GetLineHeight(inputBoxTable);
    local caretHeight = GetCaretHeight(inputBoxTable);
    local caretX = textStartX + (inputBoxTable.charPositions[cursorCharPos + 1] or 0);
    inputBoxTable.caret:SetAnchor(vec.New(0.0, 0.5));
    inputBoxTable.caret:SetRelativePoint(vec.New(0.0, 0.5));
    inputBoxTable.caret:SetPos(vec.New(caretX, 0));
    inputBoxTable.caret:SetSize(vec.New(1, caretHeight));
    inputBoxTable.caret:SetVisible(inputBoxTable.isFocused and inputBoxTable.cursorBlinkState and not isShowingPlaceholder);

    HideSelection(inputBoxTable);
    local selectionStart, selectionEnd = GetSelectionRange(inputBoxTable);
    if not inputBoxTable.isFocused or selectionStart == nil or selectionEnd == nil then return; end

    local visibleSelectionStart = math.max(selectionStart, startCharPos);
    local visibleSelectionEnd = math.min(selectionEnd, endCharPos);
    if visibleSelectionStart >= visibleSelectionEnd then return; end

    local selectionX = textStartX + (inputBoxTable.charPositions[visibleSelectionStart - startCharPos + 1] or 0);
    local selectionEndX = textStartX + (inputBoxTable.charPositions[visibleSelectionEnd - startCharPos + 1] or 0);
    local panel = GetSelectionPanel(inputBoxTable, 1);
    panel:SetAnchor(vec.New(0.0, 0.5));
    panel:SetRelativePoint(vec.New(0.0, 0.5));
    panel:SetPos(vec.New(selectionX, 0));
    panel:SetSize(vec.New(math.max(1, selectionEndX - selectionX), lineHeight));
    panel:SetVisible(true);
end

local function SetupInputBoxMethods(inputBoxTable)
    inputBoxTable.SetOnMouseDown = function(table, func)
        table.onMouseDown = func;
    end

    inputBoxTable.SetOnMouseUp = function(table, func)
        table.onMouseUp = func;
    end

    inputBoxTable.SetOnMouseHeld = function(table, func)
        table.onMouseHeld = func;
    end

    inputBoxTable.SetOnSubmit = function(table, func)
        inputBoxTable.onSubmit = func;
    end

    inputBoxTable.SetOnTab = function(table, func)
        inputBoxTable.onTab = func;
    end

    inputBoxTable.SetOnFocusBegin = function(table, func)
        inputBoxTable.onFocusBegin = func;
    end
    inputBoxTable.SetOnFocusEnd = function(table, func)
        inputBoxTable.onFocusEnd = func;
    end

    inputBoxTable.SetTextVerticallyCentered = function(table)
        inputBoxTable.button.text:SetAnchor(vec.New(0.0, 0.5));
        inputBoxTable.button.text:SetRelativePoint(vec.New(0.0, 0.5));
        inputBoxTable.button.text:SetPosY(0);
        RefreshText(inputBoxTable);
    end

    inputBoxTable.Focus = function(table)
        UI.FocusWidget(table.button.root);
    end

    inputBoxTable.Unfocus = function(table)
        if (not UI.IsFocusedWidget(table.button.root)) then
            return;
        end

        UI.UnfocusWidget(table.button.root);
    end

    local function FireTextChanged(t)
        if t.onTextChanged ~= nil then t.onTextChanged(t) end
    end

    local function CaptureState(table)
        return { text = GetText(table), cursorPos = table.cursorPos, selectionAnchor = table.selectionAnchor };
    end

    local function PushHistory(stack, state)
        table.insert(stack, state);
        if #stack > 100 then table.remove(stack, 1); end
    end

    local function RecordUndo(table)
        PushHistory(table.undoStack, CaptureState(table));
        table.redoStack = {};
    end

    local function RestoreState(table, state)
        table.text = state.text;
        table.cursorPos = state.cursorPos;
        table.selectionAnchor = state.selectionAnchor;
        table.timer = 0.0;
        table.cursorBlinkState = true;
        RefreshText(table);
        FireTextChanged(table);
    end

    local function SanitizeInsertedText(table, text : string) : string
        if not table.multiline then
            text = text:gsub("[\r\n]", "");
        end

        if table.numericOnly then
            text = text:gsub("[^%d]", "");
        end

        return text;
    end

    local function ReplaceRange(table, rangeStart : number, rangeEnd : number, replacement : string)
        local rawReplacement = replacement;
        replacement = SanitizeInsertedText(table, replacement);
        if rawReplacement ~= "" and replacement == "" then return; end
        if rangeStart == rangeEnd and replacement == "" then return; end

        local text = GetText(table);
        local startBytePosition = GetBytePositionFromCharacterPosition(text, rangeStart);
        local endBytePosition = GetBytePositionFromCharacterPosition(text, rangeEnd);
        RecordUndo(table);

        table.text = text:sub(1, startBytePosition - 1) .. replacement .. text:sub(endBytePosition);
        table.cursorPos = rangeStart + (utf8.len(replacement) or 0);
        table.selectionAnchor = nil;
        table.timer = 0.0;
        table.cursorBlinkState = true;
        RefreshText(table);
        FireTextChanged(table);
    end

    local function ReplaceSelection(table, replacement : string)
        local selectionStart, selectionEnd = GetSelectionRange(table);
        ReplaceRange(table, selectionStart or table.cursorPos, selectionEnd or table.cursorPos, replacement);
    end

    local function GetCharacter(text : string, characterPosition : number) : string
        if characterPosition < 0 or characterPosition >= (utf8.len(text) or 0) then return ""; end

        local startBytePosition = GetBytePositionFromCharacterPosition(text, characterPosition);
        local endBytePosition = GetBytePositionFromCharacterPosition(text, characterPosition + 1);
        return text:sub(startBytePosition, endBytePosition - 1);
    end

    local function IsWordCharacter(character : string) : boolean
        return character:match("[%w_]") ~= nil;
    end

    local function GetPreviousWordPosition(table) : number
        local text = GetText(table);
        local position = table.cursorPos;
        while position > 0 and not IsWordCharacter(GetCharacter(text, position - 1)) do position -= 1; end
        while position > 0 and IsWordCharacter(GetCharacter(text, position - 1)) do position -= 1; end
        return position;
    end

    local function GetNextWordPosition(table) : number
        local text = GetText(table);
        local textLength = utf8.len(text) or 0;
        local position = table.cursorPos;
        while position < textLength and IsWordCharacter(GetCharacter(text, position)) do position += 1; end
        while position < textLength and not IsWordCharacter(GetCharacter(text, position)) do position += 1; end
        return position;
    end

    local function MoveCursor(table, position : number, isSelecting : boolean)
        if isSelecting then
            if table.selectionAnchor == nil then table.selectionAnchor = table.cursorPos; end
        else
            table.selectionAnchor = nil;
        end

        table.cursorPos = math.clamp(position, 0, GetTextLength(table));
        table.timer = 0.0;
        table.cursorBlinkState = true;
        RefreshText(table);
    end

    inputBoxTable.AddUnicodeToInput = function(table, unicode)
        ReplaceSelection(table, utf8.char(unicode));
    end

    inputBoxTable.SelectAll = function(table)
        table.selectionAnchor = 0;
        table.cursorPos = GetTextLength(table);
        table.timer = 0.0;
        table.cursorBlinkState = true;
        RefreshText(table);
    end

    inputBoxTable.Copy = function(table)
        local selectionStart, selectionEnd = GetSelectionRange(table);
        if selectionStart == nil or selectionEnd == nil then return; end

        local text = GetText(table);
        local startBytePosition = GetBytePositionFromCharacterPosition(text, selectionStart);
        local endBytePosition = GetBytePositionFromCharacterPosition(text, selectionEnd);
        UI.SetClipboardText(text:sub(startBytePosition, endBytePosition - 1));
    end

    inputBoxTable.Cut = function(table)
        local selectionStart, selectionEnd = GetSelectionRange(table);
        if selectionStart == nil or selectionEnd == nil then return; end

        table:Copy();
        ReplaceRange(table, selectionStart, selectionEnd, "");
    end

    inputBoxTable.Paste = function(table)
        ReplaceSelection(table, UI.GetClipboardText());
    end

    inputBoxTable.Undo = function(table)
        local state = table.undoStack[#table.undoStack];
        if state == nil then return; end
        table.undoStack[#table.undoStack] = nil;

        PushHistory(table.redoStack, CaptureState(table));
        RestoreState(table, state);
    end

    inputBoxTable.Redo = function(table)
        local state = table.redoStack[#table.redoStack];
        if state == nil then return; end
        table.redoStack[#table.redoStack] = nil;

        PushHistory(table.undoStack, CaptureState(table));
        RestoreState(table, state);
    end

    inputBoxTable.Backspace = function(table, byWord : boolean?)
        local selectionStart, selectionEnd = GetSelectionRange(table);
        if selectionStart ~= nil and selectionEnd ~= nil then
            ReplaceRange(table, selectionStart, selectionEnd, "");
        elseif table.cursorPos > 0 then
            ReplaceRange(table, if byWord then GetPreviousWordPosition(table) else table.cursorPos - 1, table.cursorPos, "");
        end
    end

    inputBoxTable.Delete = function(table, byWord : boolean?)
        local selectionStart, selectionEnd = GetSelectionRange(table);
        if selectionStart ~= nil and selectionEnd ~= nil then
            ReplaceRange(table, selectionStart, selectionEnd, "");
        elseif table.cursorPos < GetTextLength(table) then
            ReplaceRange(table, table.cursorPos, if byWord then GetNextWordPosition(table) else table.cursorPos + 1, "");
        end
    end

    inputBoxTable.Home = function(table, isSelecting : boolean?)
        MoveCursor(table, 0, isSelecting or false);
    end

    inputBoxTable.End = function(table, isSelecting : boolean?)
        MoveCursor(table, GetTextLength(table), isSelecting or false);
    end

    inputBoxTable.Submit = function()
        local shouldUnfocus = true;
        if inputBoxTable.onSubmit then
            local submitResult = inputBoxTable:onSubmit();
            if submitResult ~= nil then
                shouldUnfocus = submitResult;
            end
        end

        if shouldUnfocus then
            local focusedWidget = UI.GetFocusedWidget();
            UI.UnfocusWidget(focusedWidget);
        end
    end

    inputBoxTable.Tab = function()
        if inputBoxTable.onTab then
            inputBoxTable.onTab(inputBoxTable);
        end
    end

    inputBoxTable.Left = function(table, isSelecting : boolean?, byWord : boolean?)
        local selectionStart = GetSelectionRange(table);
        if not isSelecting and selectionStart ~= nil then
            MoveCursor(table, selectionStart, false);
            return;
        end

        MoveCursor(table, if byWord then GetPreviousWordPosition(table) else table.cursorPos - 1, isSelecting or false);
    end

    inputBoxTable.Right = function(table, isSelecting : boolean?, byWord : boolean?)
        local _, selectionEnd = GetSelectionRange(table);
        if not isSelecting and selectionEnd ~= nil then
            MoveCursor(table, selectionEnd, false);
            return;
        end

        MoveCursor(table, if byWord then GetNextWordPosition(table) else table.cursorPos + 1, isSelecting or false);
    end

    inputBoxTable.GetText = function(table)
        return table.text;
    end

    inputBoxTable.SetText = function(table, text)
        table.text = if type(text) == "string" then text else tostring(text);

        table.timer = 0.0;
        table.cursorBlinkState = false;
        table.cursorPos = utf8.len(table.text) or 0;
        table.selectionAnchor = nil;
        table.undoStack = {};
        table.redoStack = {};

        RefreshText(table);
        FireTextChanged(table);
    end

    inputBoxTable.SetOnTextChanged = function(table, fn)
        table.onTextChanged = fn;
    end

    inputBoxTable.SetAnchor = function(table, anchor)
        table.button:SetAnchor(anchor);
    end
    inputBoxTable.SetRelativePoint = function(table, point)
        table.button:SetRelativePoint(point);
    end

    inputBoxTable.GetPos = function(table)
        return table.button:GetPos();
    end
    inputBoxTable.GetPosX = function(table)
        return table.button:GetPosX();
    end
    inputBoxTable.GetPosY = function(table)
        return table.button:GetPosY();
    end

    inputBoxTable.SetPos = function(table, position)
        table.button:SetPos(position);
    end
    inputBoxTable.SetPosX = function(table, x)
        table.button:SetPosX(x);
    end
    inputBoxTable.SetPosY = function(table, y)
        table.button:SetPosY(y);
    end

    inputBoxTable.ForceRefresh = function(table)
        table.button:ForceRefresh();
    end

    return inputBoxTable;
end

function inputBoxAPI.New(parent : Widget, position : vec, size : vec, layer : integer, inputBoxTemplateTable : Options?)
    local inputBoxTable = {};

    inputBoxTemplateTable = inputBoxTemplateTable or {};
    local panelTemplate = inputBoxTemplateTable["panelTemplate"] or "DialogBox";
    local textTemplate = inputBoxTemplateTable["textTemplate"] or "DefaultButtonText";
    inputBoxTable.textTemplate = textTemplate; -- Store textTemplate for use in calculations
    local defaultText = inputBoxTemplateTable["defaultText"] or "";
    local defaultTextFontSize = inputBoxTemplateTable["defaultTextFontSize"];

    inputBoxTable.text = defaultText;
    inputBoxTable.placeholderText = inputBoxTemplateTable["placeholderText"] or "";
    inputBoxTable.placeholderTextColor = inputBoxTemplateTable["placeholderTextColor"];
    inputBoxTable.isFocused = false;

    inputBoxTable.timer = 0.0;
    inputBoxTable.cursorBlinkState = false;
    inputBoxTable.cursorPos = utf8.len(defaultText) or 0;
    inputBoxTable.selectionAnchor = nil;
    inputBoxTable.selectionPanels = {};
    inputBoxTable.undoStack = {};
    inputBoxTable.redoStack = {};
    inputBoxTable.isMouseSelecting = false;
    inputBoxTable.onSubmit = nil;
    inputBoxTable.onTab = nil;
    inputBoxTable.onFocusBegin = nil;
    inputBoxTable.onFocusEnd = nil;

    inputBoxTable.numericOnly = inputBoxTemplateTable["numericOnly"] or false;
    inputBoxTable.secure = inputBoxTemplateTable["secure"] or false;
    inputBoxTable.multiline = inputBoxTemplateTable["multiline"] or false;

    -- Store input box dimensions and text layout defaults before the first text refresh.
    inputBoxTable.inputBoxWidth = size.x;
    inputBoxTable.textInsetX = inputBoxTemplateTable["textInsetX"] or (inputBoxTable.multiline and 6 or 5);
    inputBoxTable.buttonTextRelativePointX = 0.0;

    inputBoxTable.button = UIButton.New(parent, position, size, layer, {
        panelTemplate = panelTemplate,
        textTemplate = textTemplate
    });

    inputBoxTable.root = inputBoxTable.button.root;
    RootedWidget.Attach(inputBoxTable);
    inputBoxTable.button:SetText(defaultText);
    if defaultTextFontSize then
        inputBoxTable.button:SetFontSize(defaultTextFontSize);
    end
    inputBoxTable.button:SetFocusable(true);

    inputBoxTable.caret = inputBoxTable.button.root:NewPanel(vec.New(0, 0), vec.New(1, 1), 2i);
    inputBoxTable.caret:SetInteractable(false);
    inputBoxTable.caret:SetVisible(false);

    if inputBoxTable.multiline then
        inputBoxTable.button.root:SetClipChildren(true);
        inputBoxTable.button.text:SetAnchor(vec.New(0.0, 1.0));
        inputBoxTable.button.text:SetRelativePoint(vec.New(0.0, 1.0));
        inputBoxTable.button.text:SetPos(vec.New(inputBoxTable.textInsetX, -6));
    else
        inputBoxTable.button.text:SetAnchor(vec.New(0.0, 0.5));
        inputBoxTable.button.text:SetRelativePoint(vec.New(0.0, 0.5));
        inputBoxTable.button.text:SetPos(vec.New(inputBoxTable.textInsetX, 0));
    end

    local function GetCursorPositionFromMouse(mouseX : number) : number
        if inputBoxTable.multiline then
            return GetTextLength(inputBoxTable);
        end

        local buttonWorldPosX = inputBoxTable.button:GetWorldPosX();
        local localX = mouseX - buttonWorldPosX;
        local startCharPos = inputBoxTable.startCharPos or 0;
        local endCharPos = inputBoxTable.endCharPos or GetTextLength(inputBoxTable);
        localX -= inputBoxTable.textStartX or 0;

        local charPositions = inputBoxTable.charPositions or {};
        local newCursorPos = startCharPos;
        for i = 1, #charPositions - 1 do
            local pos1 = charPositions[i];
            local pos2 = charPositions[i + 1];
            if localX >= pos1 and localX < pos2 then
                newCursorPos = startCharPos + i - 1 + (if localX >= (pos1 + pos2) / 2 then 1 else 0);
                break;
            end
        end
        if localX >= (charPositions[#charPositions] or 0) then newCursorPos = endCharPos; end

        return math.clamp(newCursorPos, 0, GetTextLength(inputBoxTable));
    end

    inputBoxTable.button:SetOnMouseDown(function(eventID, widget, button, mouseX, mouseY)
        if button == 0i then
            inputBoxTable.cursorPos = GetCursorPositionFromMouse(mouseX);
            inputBoxTable.selectionAnchor = inputBoxTable.cursorPos;
            inputBoxTable.isMouseSelecting = true;
            inputBoxTable.timer = 0.0;
            inputBoxTable.cursorBlinkState = true;
        end
        RefreshText(inputBoxTable);
        if inputBoxTable.onMouseDown ~= nil then inputBoxTable.onMouseDown(eventID, widget, button, mouseX, mouseY); end
    end);

    inputBoxTable.button:SetOnMouseHeld(function(eventID, widget, mouseX, mouseY)
        if inputBoxTable.isMouseSelecting and not inputBoxTable.multiline then
            inputBoxTable.cursorPos = GetCursorPositionFromMouse(mouseX);
            inputBoxTable.timer = 0.0;
            inputBoxTable.cursorBlinkState = true;
            RefreshText(inputBoxTable);
        end
        if inputBoxTable.onMouseHeld ~= nil then inputBoxTable.onMouseHeld(eventID, widget, mouseX, mouseY); end
    end);

    inputBoxTable.button:SetOnMouseUp(function(eventID, widget, button, mouseX, mouseY)
        if button == 0i then
            inputBoxTable.isMouseSelecting = false;
            if inputBoxTable.selectionAnchor == inputBoxTable.cursorPos then inputBoxTable.selectionAnchor = nil; end
            RefreshText(inputBoxTable);
        end
        if inputBoxTable.onMouseUp ~= nil then inputBoxTable.onMouseUp(eventID, widget, button, mouseX, mouseY); end
    end);

    inputBoxTable.button:SetOnFocusBegin(function(eventID, widget)
        inputBoxTable.isFocused = true;
        inputBoxTable.timer = 0.0;
        inputBoxTable.cursorBlinkState = true;
        RefreshText(inputBoxTable);
        if inputBoxTable.onFocusBegin ~= nil then
            inputBoxTable.onFocusBegin(inputBoxTable);
        end
    end);

    inputBoxTable.button:SetOnFocusEnd(function(eventID, widget)
        inputBoxTable.isFocused = false;
        inputBoxTable.cursorBlinkState = false;

        RefreshText(inputBoxTable);
        if inputBoxTable.onFocusEnd ~= nil then
            inputBoxTable.onFocusEnd(inputBoxTable);
        end
    end);

    inputBoxTable.button:SetOnFocusHeld(function(eventID, widget, deltaTime)
        inputBoxTable.timer = inputBoxTable.timer + deltaTime;

        if (inputBoxTable.timer > 1.0) then
            inputBoxTable.timer = inputBoxTable.timer - 1.0;
            inputBoxTable.cursorBlinkState = not inputBoxTable.cursorBlinkState;

            RefreshText(inputBoxTable);
        end
    end);

    inputBoxTable.button:SetOnKeyboard(function(widget, eventID, ...)
        if (eventID == UIKeyboardEvent.Key) then
            local keyID, action, modifiers = ...;

            local isPressed = action == InputPhase.Pressed;
            local isRepeated = action == InputPhase.Repeated;
            local isShiftPressed = integer.btest(modifiers or InputModifier.None, InputModifier.Shift);
            local isPrimaryModifierPressed = integer.btest(modifiers or InputModifier.None, InputModifier.Control) or integer.btest(modifiers or InputModifier.None, InputModifier.Super);

            if (isPressed) then
                if isPrimaryModifierPressed and keyID == KeyValues.A then
                    inputBoxTable:SelectAll();
                    return true;
                elseif isPrimaryModifierPressed and keyID == KeyValues.C then
                    inputBoxTable:Copy();
                    return true;
                elseif isPrimaryModifierPressed and keyID == KeyValues.X then
                    inputBoxTable:Cut();
                    return true;
                elseif isPrimaryModifierPressed and keyID == KeyValues.V then
                    inputBoxTable:Paste();
                    return true;
                elseif isPrimaryModifierPressed and keyID == KeyValues.Z then
                    if isShiftPressed then inputBoxTable:Redo() else inputBoxTable:Undo(); end
                    return true;
                elseif isPrimaryModifierPressed and keyID == KeyValues.Y then
                    inputBoxTable:Redo();
                    return true;
                elseif (keyID == KeyValues.ESCAPE) then
                    inputBoxTable:Unfocus();
                    return true;
                elseif (keyID == KeyValues.ENTER) then
                    if inputBoxTable.multiline and not isShiftPressed then
                        inputBoxTable:AddUnicodeToInput(10);
                    else
                        inputBoxTable:Submit();
                    end
                    return true;
                elseif (keyID == KeyValues.TAB) then
                    inputBoxTable.Tab();
                    return true;
                end
            end

            if (isPressed or isRepeated) then
                if (keyID == KeyValues.BACKSPACE) then
                    inputBoxTable:Backspace(isPrimaryModifierPressed);
                    return true;
                elseif (keyID == KeyValues.DELETE) then
                    inputBoxTable:Delete(isPrimaryModifierPressed);
                    return true;
                elseif (keyID == KeyValues.LEFT) then
                    inputBoxTable:Left(isShiftPressed, isPrimaryModifierPressed);
                    return true;
                elseif (keyID == KeyValues.RIGHT) then
                    inputBoxTable:Right(isShiftPressed, isPrimaryModifierPressed);
                    return true;
                elseif (keyID == KeyValues.HOME) then
                    inputBoxTable:Home(isShiftPressed);
                    return true;
                elseif (keyID == KeyValues.END) then
                    inputBoxTable:End(isShiftPressed);
                    return true;
                end
            end
        elseif (eventID == UIKeyboardEvent.Unicode) then
            local unicode = ...;
            inputBoxTable:AddUnicodeToInput(integer.tonumber(unicode));
            return true;
        end
        
        return true;
    end);

    inputBoxTable = SetupInputBoxMethods(inputBoxTable);

    -- Call RefreshText to initialize startCharPos and endCharPos
    RefreshText(inputBoxTable);

    return inputBoxTable;
end

return inputBoxAPI;
