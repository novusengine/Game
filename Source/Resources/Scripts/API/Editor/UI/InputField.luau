local InputBox = require("@src/API/UI/InputBox");
local Theme = require("@src/API/Editor/UI/Theme");

export type InputFieldAPI =
{
    New : (parent : Widget, position : vec, size : vec, layer : integer, options : InputBox.Options?) -> InputBox.InputBox,
    Style : (input : InputBox.InputBox) -> InputBox.InputBox,
}

local inputFieldAPI : InputFieldAPI = { };

local BASE_COLOR = vec.New(0.055, 0.065, 0.078);
local HOVER_COLOR = vec.New(0.075, 0.10, 0.135);
local FOCUSED_COLOR = vec.New(0.08, 0.13, 0.20);
local BASE_BORDER_COLOR = vec.New(0.24, 0.28, 0.34);

function inputFieldAPI.Style(input : InputBox.InputBox) : InputBox.InputBox
    local hovered = false;
    local focused = false;
    local dynamicInput : any = input;
    local previousFocusBegin = dynamicInput.onFocusBegin;
    local previousFocusEnd = dynamicInput.onFocusEnd;

    local function RefreshVisuals()
        if focused then
            input.root:SetColor(FOCUSED_COLOR);
            input.root:SetBorderColor(Theme.selectedColor);
        elseif hovered then
            input.root:SetColor(HOVER_COLOR);
            input.root:SetBorderColor(Theme.borderColor);
        else
            input.root:SetColor(BASE_COLOR);
            input.root:SetBorderColor(BASE_BORDER_COLOR);
        end
        input.root:SetAlpha(1.0);
        input.root:SetBorderSize(1.0);
    end

    input.button:SetOnHoverBegin(function(...)
        hovered = true;
        RefreshVisuals();
    end);
    input.button:SetOnHoverEnd(function(...)
        hovered = false;
        RefreshVisuals();
    end);
    input:SetOnFocusBegin(function(currentInput)
        focused = true;
        RefreshVisuals();
        if previousFocusBegin then previousFocusBegin(currentInput); end
    end);
    input:SetOnFocusEnd(function(currentInput)
        focused = false;
        RefreshVisuals();
        if previousFocusEnd then previousFocusEnd(currentInput); end
    end);
    RefreshVisuals();
    return input;
end

function inputFieldAPI.New(parent : Widget, position : vec, size : vec, layer : integer, options : InputBox.Options?) : InputBox.InputBox
    local resolvedOptions = options or { };
    if resolvedOptions.panelTemplate == nil then resolvedOptions.panelTemplate = "DebugDarkGrey"; end
    if resolvedOptions.textTemplate == nil then resolvedOptions.textTemplate = Theme.textTemplate; end
    return inputFieldAPI.Style(InputBox.New(parent, position, size, layer, resolvedOptions));
end

return inputFieldAPI;
