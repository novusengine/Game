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

-- Helper function to get the byte position from a character position in a UTF-8 string
local function GetBytePositionFromCharacterPosition(s, charPos)
    if charPos == 0 then
        return 1;
    else
        return utf8.offset(s, charPos + 1);
    end
end

local function RefreshText(inputBoxTable)
    local text = inputBoxTable.text;
    if (type(text) ~= "string") then
        text = tostring(text);
    end

    local cursorPos = inputBoxTable.cursorPos;

    local isShowingPlaceholder = text == ""
        and not inputBoxTable.isFocused
        and inputBoxTable.placeholderText ~= "";
    local displayText = if isShowingPlaceholder then inputBoxTable.placeholderText else text;
    local cursorCharPos = cursorPos;

    -- Calculate the total width of the text
    local totalTextWidth = UI.CalculateTextSize(displayText, inputBoxTable.textTemplate).x;

    -- Initialize variables for text clipping
    local startCharPos = 0;
    local endCharPos = utf8.len(text);
    local textInsetX = inputBoxTable.textInsetX or 0;
    local maxVisibleWidth = inputBoxTable.inputBoxWidth - textInsetX * 2;

    if inputBoxTable.multiline then
        if inputBoxTable.cursorBlinkState and not isShowingPlaceholder then
            local cursorBytePos = GetBytePositionFromCharacterPosition(displayText, cursorPos);
            displayText = displayText:sub(1, cursorBytePos - 1) .. "|" .. displayText:sub(cursorBytePos);
        end
        inputBoxTable.button:SetText(displayText);
        inputBoxTable.button.text:SetWrapWidth(maxVisibleWidth);
        inputBoxTable.button.text:SetAlpha(isShowingPlaceholder and 0.55 or 1.0);
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
            local charWidth = UI.CalculateTextSize(char, inputBoxTable.textTemplate).x;
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

    -- Calculate displayTextWidth using UI.CalculateTextSize
    local displayTextWidth = UI.CalculateTextSize(displayText, inputBoxTable.textTemplate).x;

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
        local charWidth = UI.CalculateTextSize(char, inputBoxTable.textTemplate).x;
        table.insert(inputBoxTable.charPositions, cumulativeWidth);
        cumulativeWidth = cumulativeWidth + charWidth;
    end
    -- Add the end position
    table.insert(inputBoxTable.charPositions, cumulativeWidth);

    -- Keep the caret in the same widget. Inputs that need vertical centering use a fixed,
    -- bottom-anchored baseline so the taller caret glyph cannot move the text.
    if inputBoxTable.cursorBlinkState then
        local cursorBytePosInDisplayText = GetBytePositionFromCharacterPosition(displayText, cursorCharPos);
        local textBeforeCursor = displayText:sub(1, cursorBytePosInDisplayText - 1);
        local textAfterCursor = displayText:sub(cursorBytePosInDisplayText);
        displayText = textBeforeCursor .. "|" .. textAfterCursor;
    end

    inputBoxTable.button:SetText(displayText);
    inputBoxTable.button.text:SetAlpha(isShowingPlaceholder and 0.55 or 1.0);
end

local function SetupInputBoxMethods(inputBoxTable)
    inputBoxTable.SetOnMouseDown = function(table, func)
        table.button:SetOnMouseDown(func);
    end

    inputBoxTable.SetOnMouseUp = function(table, func)
        table.button:SetOnMouseUp(func);
    end

    inputBoxTable.SetOnMouseHeld = function(table, func)
        table.button:SetOnMouseHeld(func);
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

    inputBoxTable.AddUnicodeToInput = function(table, unicode)
        local char = utf8.char(unicode);
        if inputBoxTable.numericOnly then
            if not char:match("%d") then
                return; -- Do not add non-digit characters
            end
        end
        local cursorPos = inputBoxTable.cursorPos;
        local text = inputBoxTable.text;
        if (type(text) ~= "string") then
            text = tostring(text);
        end

        local cursorBytePos = GetBytePositionFromCharacterPosition(text, cursorPos);
        local textBeforeCursor = text:sub(1, cursorBytePos - 1);
        local textAfterCursor = text:sub(cursorBytePos);
        inputBoxTable.text = textBeforeCursor .. char .. textAfterCursor;

        inputBoxTable.cursorPos = inputBoxTable.cursorPos + 1;

        RefreshText(inputBoxTable);
        FireTextChanged(inputBoxTable);
    end

    inputBoxTable.Backspace = function()
        local cursorPos = inputBoxTable.cursorPos;
        if cursorPos > 0 then
            local text = inputBoxTable.text;
            if (type(text) ~= "string") then
                text = tostring(text);
            end

            -- Get byte index of character at cursorPos
            local cursorBytePos = GetBytePositionFromCharacterPosition(text, cursorPos);

            -- Get byte index of character before cursorPos
            local prevCursorBytePos = GetBytePositionFromCharacterPosition(text, cursorPos - 1);

            -- Remove character before cursor
            local textBefore = text:sub(1, prevCursorBytePos - 1);
            local textAfter = text:sub(cursorBytePos);
            inputBoxTable.text = textBefore .. textAfter;

            -- Move cursor back one position
            inputBoxTable.cursorPos = cursorPos - 1;

            RefreshText(inputBoxTable);
            FireTextChanged(inputBoxTable);
        end
    end

    -- Delete Key Functionality
    inputBoxTable.Delete = function()
        local cursorPos = inputBoxTable.cursorPos;
        local textLength = utf8.len(inputBoxTable.text);
        if cursorPos < textLength then
            local text = inputBoxTable.text;

            -- Get byte index of character at cursorPos
            local cursorBytePos = GetBytePositionFromCharacterPosition(text, cursorPos);

            -- Get byte index of character after cursorPos
            local nextCursorBytePos = GetBytePositionFromCharacterPosition(text, cursorPos + 1);

            -- Remove character at cursorPos
            local textBefore = text:sub(1, cursorBytePos - 1);
            local textAfter = text:sub(nextCursorBytePos);
            inputBoxTable.text = textBefore .. textAfter;

            -- Cursor position remains the same

            RefreshText(inputBoxTable);
            FireTextChanged(inputBoxTable);
        end
    end

    -- Home Key Functionality
    inputBoxTable.Home = function()
        inputBoxTable.cursorPos = 0;

        inputBoxTable.timer = 0.0;
        inputBoxTable.cursorBlinkState = true;

        RefreshText(inputBoxTable);
    end

    -- End Key Functionality
    inputBoxTable.End = function()
        inputBoxTable.cursorPos = utf8.len(inputBoxTable.text);

        inputBoxTable.timer = 0.0;
        inputBoxTable.cursorBlinkState = true;

        RefreshText(inputBoxTable);
    end

    inputBoxTable.Submit = function()
        if inputBoxTable.onSubmit then
            local shouldUnfocus = inputBoxTable.onSubmit(inputBoxTable) or false;
            if (shouldUnfocus) then
                UI.UnfocusWidget(inputBoxTable.button.root);

                inputBoxTable.cursorBlinkState = false;
                RefreshText(inputBoxTable);
            end
        end
    end

    inputBoxTable.Tab = function()
        if inputBoxTable.onTab then
            inputBoxTable.onTab(inputBoxTable);
        end
    end

    inputBoxTable.Left = function()
        if inputBoxTable.cursorPos > 0 then
            inputBoxTable.cursorPos = inputBoxTable.cursorPos - 1;

            inputBoxTable.timer = 0.0;
            inputBoxTable.cursorBlinkState = true;

            RefreshText(inputBoxTable);
        end
    end

    inputBoxTable.Right = function()
        if inputBoxTable.cursorPos < utf8.len(inputBoxTable.text) then
            inputBoxTable.cursorPos = inputBoxTable.cursorPos + 1;

            inputBoxTable.timer = 0.0;
            inputBoxTable.cursorBlinkState = true;

            RefreshText(inputBoxTable);
        end
    end

    inputBoxTable.GetText = function(table)
        return table.text;
    end

    inputBoxTable.SetText = function(table, text)
        table.text = text;

        table.timer = 0.0;
        table.cursorBlinkState = false;
        table.cursorPos = utf8.len(text);

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
    inputBoxTable.isFocused = false;

    inputBoxTable.timer = 0.0;
    inputBoxTable.cursorBlinkState = false;
    inputBoxTable.cursorPos = utf8.len(defaultText);
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

    inputBoxTable.button:SetOnMouseDown(function(eventID, widget, button, mouseX, mouseY)
        if inputBoxTable.multiline then
            inputBoxTable.cursorPos = utf8.len(inputBoxTable.text);
            RefreshText(inputBoxTable);
            return;
        end

        local buttonWorldPosX = inputBoxTable.button:GetWorldPosX();
        local localX = mouseX - buttonWorldPosX;

        -- Ensure startCharPos and endCharPos are valid
        local startCharPos = inputBoxTable.startCharPos or 0;
        local endCharPos = inputBoxTable.endCharPos or utf8.len(inputBoxTable.text);

        local textStartX = inputBoxTable.textStartX or 0;

        -- Adjust localX by subtracting textStartX due to alignment
        localX = localX - textStartX;

        -- Use charPositions to find the clicked character index
        local charPositions = inputBoxTable.charPositions or {};
        local newCursorPos = startCharPos;
        for i = 1, #charPositions - 1 do
            local pos1 = charPositions[i];
            local pos2 = charPositions[i + 1];
            if localX >= pos1 and localX < pos2 then
                newCursorPos = startCharPos + i - 1;
                break;
            end
        end
        if localX >= (charPositions[#charPositions] or 0) then
            newCursorPos = endCharPos;
        end

        -- Ensure newCursorPos is within valid range
        if newCursorPos < 0 then newCursorPos = 0 end
        local textLength = utf8.len(inputBoxTable.text);
        if newCursorPos > textLength then newCursorPos = textLength end;

        inputBoxTable.cursorPos = newCursorPos;
        RefreshText(inputBoxTable);
    end)

    inputBoxTable.button:SetOnFocusBegin(function(eventID, widget)
        inputBoxTable.isFocused = true;
        inputBoxTable.timer = 1.0;
        inputBoxTable.cursorBlinkState = false;
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

            if (isPressed) then
                if (keyID == KeyValues.ESCAPE) then
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
                elseif (keyID == KeyValues.HOME) then
                    inputBoxTable:Home();
                    return true;
                elseif (keyID == KeyValues.END) then
                    inputBoxTable:End();
                    return true;
                end
            end

            if (isPressed or isRepeated) then
                if (keyID == KeyValues.BACKSPACE) then
                    inputBoxTable:Backspace();
                    return true;
                elseif (keyID == KeyValues.DELETE) then
                    inputBoxTable:Delete();
                    return true;
                elseif (keyID == KeyValues.LEFT) then
                    inputBoxTable:Left();
                    return true;
                elseif (keyID == KeyValues.RIGHT) then
                    inputBoxTable:Right();
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
