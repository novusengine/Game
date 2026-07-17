export type InputBinding =
{
    device : number,
    code : number,
    modifiers : number?,
    modifierMatch : number?,
}

export type InputBindingConflict =
{
    action : string,
    slot : number,
    kind : number,
}

export type InputBindingMutation =
{
    action : string,
    slot : number,
    previousBinding : InputBinding?,
    binding : InputBinding?,
}

export type InputBindingChangeResult =
{
    status : number,
    succeeded : boolean,
    requestedPolicy : number,
    appliedPolicy : number,
    conflicts : { InputBindingConflict },
    mutations : { InputBindingMutation },
}

export type InputActionInfo =
{
    name : string,
    displayName : string,
    category : string,
    context : string,
    defaultReply : number,
    categorySortOrder : number,
    sortOrder : number,
    rebindable : boolean,
    defaultBindings : { [number] : InputBinding },
    bindings : { [number] : InputBinding },
}

export type InputActionOptions =
{
    context : string?,
    defaultReply : number?,
    rebindable : boolean?,
    categorySortOrder : number?,
    sortOrder : number?,
    secondaryBinding : InputBinding?,
}

export type InputContextInfo =
{
    name : string,
    priority : number,
    sortOrder : number,
    active : boolean,
}

export type InputActionCallback = (phase : number, device : number, code : number, modifiers : number, value : number) -> number
export type InputBindingCaptureCallback = (binding : InputBinding?) -> ()

type InputTableAPI =
{
    keyNames : { [number] : string },
    KeyValues : { [string] : number },
    GetKeyName : (keyID : number) -> string,
    GetKeyValue : (keyName : string) -> number,
    Keyboard : (keyID : number, modifiers : number?, modifierMatch : number?) -> InputBinding,
    Mouse : (buttonID : number, modifiers : number?, modifierMatch : number?) -> InputBinding,
    MouseWheel : (direction : number, modifiers : number?, modifierMatch : number?) -> InputBinding,
    RegisterAction : (name : string, displayName : string, category : string, binding : InputBinding, options : InputActionOptions?) -> boolean,
    Bind : (name : string, displayName : string, category : string, binding : InputBinding, callback : InputActionCallback, options : InputActionOptions?) -> number,
    Connect : (name : string, callback : InputActionCallback) -> number,
    Disconnect : (connection : number) -> boolean,
    SetBinding : (name : string, slot : number, binding : InputBinding, policy : number?) -> InputBindingChangeResult,
    ClearBinding : (name : string, slot : number, policy : number?) -> InputBindingChangeResult,
    ResetBinding : (name : string, slot : number, policy : number?) -> InputBindingChangeResult,
    GetBinding : (name : string, slot : number) -> InputBinding?,
    GetActions : () -> { InputActionInfo },
    IsDown : (name : string) -> boolean,
    WasPressed : (name : string) -> boolean,
    WasReleased : (name : string) -> boolean,
    FindBindingConflicts : (name : string, slot : number, binding : InputBinding) -> InputBindingChangeResult,
    FormatBinding : (binding : InputBinding?) -> string,
    BeginBindingCapture : (callback : InputBindingCaptureCallback) -> boolean,
    CancelBindingCapture : () -> boolean,
    IsBindingCaptureActive : () -> boolean,
    SaveBindings : () -> boolean,
    CreateContext : (name : string, priority : number, sortOrder : number?) -> boolean,
    SetContextActive : (name : string, active : boolean) -> boolean,
    IsContextActive : (name : string) -> boolean,
    GetContexts : () -> { InputContextInfo },
}

local inputTableAPI : InputTableAPI = { };

inputTableAPI.keyNames =
{
    [32]  = "SPACE",
    [39]  = "APOSTROPHE",
    [44]  = "COMMA",
    [45]  = "MINUS",
    [46]  = "PERIOD",
    [47]  = "SLASH",
    [48]  = "0",
    [49]  = "1",
    [50]  = "2",
    [51]  = "3",
    [52]  = "4",
    [53]  = "5",
    [54]  = "6",
    [55]  = "7",
    [56]  = "8",
    [57]  = "9",
    [59]  = "SEMICOLON",
    [61]  = "EQUAL",
    [65]  = "A",
    [66]  = "B",
    [67]  = "C",
    [68]  = "D",
    [69]  = "E",
    [70]  = "F",
    [71]  = "G",
    [72]  = "H",
    [73]  = "I",
    [74]  = "J",
    [75]  = "K",
    [76]  = "L",
    [77]  = "M",
    [78]  = "N",
    [79]  = "O",
    [80]  = "P",
    [81]  = "Q",
    [82]  = "R",
    [83]  = "S",
    [84]  = "T",
    [85]  = "U",
    [86]  = "V",
    [87]  = "W",
    [88]  = "X",
    [89]  = "Y",
    [90]  = "Z",
    [91]  = "LEFT_BRACKET",
    [92]  = "BACKSLASH",
    [93]  = "RIGHT_BRACKET",
    [96]  = "GRAVE_ACCENT",
    [161] = "WORLD_1",
    [162] = "WORLD_2",
    [256] = "ESCAPE",
    [257] = "ENTER",
    [258] = "TAB",
    [259] = "BACKSPACE",
    [260] = "INSERT",
    [261] = "DELETE",
    [262] = "RIGHT",
    [263] = "LEFT",
    [264] = "DOWN",
    [265] = "UP",
    [266] = "PAGE_UP",
    [267] = "PAGE_DOWN",
    [268] = "HOME",
    [269] = "END",
    [280] = "CAPS_LOCK",
    [281] = "SCROLL_LOCK",
    [282] = "NUM_LOCK",
    [283] = "PRINT_SCREEN",
    [284] = "PAUSE",
    [290] = "F1",
    [291] = "F2",
    [292] = "F3",
    [293] = "F4",
    [294] = "F5",
    [295] = "F6",
    [296] = "F7",
    [297] = "F8",
    [298] = "F9",
    [299] = "F10",
    [300] = "F11",
    [301] = "F12",
    [302] = "F13",
    [303] = "F14",
    [304] = "F15",
    [305] = "F16",
    [306] = "F17",
    [307] = "F18",
    [308] = "F19",
    [309] = "F20",
    [310] = "F21",
    [311] = "F22",
    [312] = "F23",
    [313] = "F24",
    [314] = "F25",
    [320] = "KP_0",
    [321] = "KP_1",
    [322] = "KP_2",
    [323] = "KP_3",
    [324] = "KP_4",
    [325] = "KP_5",
    [326] = "KP_6",
    [327] = "KP_7",
    [328] = "KP_8",
    [329] = "KP_9",
    [330] = "KP_DECIMAL",
    [331] = "KP_DIVIDE",
    [332] = "KP_MULTIPLY",
    [333] = "KP_SUBTRACT",
    [334] = "KP_ADD",
    [335] = "KP_ENTER",
    [336] = "KP_EQUAL",
    [340] = "LEFT_SHIFT",
    [341] = "LEFT_CONTROL",
    [342] = "LEFT_ALT",
    [343] = "LEFT_SUPER",
    [344] = "RIGHT_SHIFT",
    [345] = "RIGHT_CONTROL",
    [346] = "RIGHT_ALT",
    [347] = "RIGHT_SUPER",
    [348] = "MENU",
}

inputTableAPI.KeyValues = {};
for keyID, keyName in inputTableAPI.keyNames do
    inputTableAPI.KeyValues[keyName] = keyID;
end

function inputTableAPI.GetKeyName(keyID : number) : string
    return inputTableAPI.keyNames[keyID] or "";
end

function inputTableAPI.GetKeyValue(keyName : string) : number
    return inputTableAPI.KeyValues[keyName] or -1;
end

function inputTableAPI.Keyboard(keyID : number, modifiers : number?, modifierMatch : number?) : InputBinding
    return {
        device = InputDevice.Keyboard,
        code = keyID,
        modifiers = modifiers or InputModifier.None,
        modifierMatch = modifierMatch or InputModifierMatch.Exact,
    };
end

function inputTableAPI.Mouse(buttonID : number, modifiers : number?, modifierMatch : number?) : InputBinding
    return {
        device = InputDevice.Mouse,
        code = buttonID,
        modifiers = modifiers or InputModifier.None,
        modifierMatch = modifierMatch or InputModifierMatch.Exact,
    };
end

function inputTableAPI.MouseWheel(direction : number, modifiers : number?, modifierMatch : number?) : InputBinding
    return {
        device = InputDevice.MouseWheel,
        code = direction,
        modifiers = modifiers or InputModifier.None,
        modifierMatch = modifierMatch or InputModifierMatch.Exact,
    };
end

function inputTableAPI.RegisterAction(
    name : string,
    displayName : string,
    category : string,
    binding : InputBinding,
    options : InputActionOptions?
) : boolean
    return Input.RegisterAction(name, displayName, category, binding, options);
end

function inputTableAPI.Bind(
    name : string,
    displayName : string,
    category : string,
    binding : InputBinding,
    callback : InputActionCallback,
    options : InputActionOptions?
) : number
    if not inputTableAPI.RegisterAction(name, displayName, category, binding, options) then
        return 0;
    end

    return Input.Connect(name, callback);
end

function inputTableAPI.Connect(name : string, callback : InputActionCallback) : number
    return Input.Connect(name, callback);
end

function inputTableAPI.Disconnect(connection : number) : boolean
    return Input.Disconnect(connection);
end

function inputTableAPI.SetBinding(name : string, slot : number, binding : InputBinding, policy : number?) : InputBindingChangeResult
    return Input.SetBinding(name, slot, binding, policy or InputBindingConflictPolicy.Reject);
end

function inputTableAPI.ClearBinding(name : string, slot : number, policy : number?) : InputBindingChangeResult
    return Input.ClearBinding(name, slot, policy or InputBindingConflictPolicy.Reject);
end

function inputTableAPI.ResetBinding(name : string, slot : number, policy : number?) : InputBindingChangeResult
    return Input.ResetBinding(name, slot, policy or InputBindingConflictPolicy.Reject);
end

function inputTableAPI.GetBinding(name : string, slot : number) : InputBinding?
    return Input.GetBinding(name, slot);
end

function inputTableAPI.GetActions() : { InputActionInfo }
    return Input.GetActions();
end

function inputTableAPI.IsDown(name : string) : boolean
    return Input.IsDown(name);
end

function inputTableAPI.WasPressed(name : string) : boolean
    return Input.WasPressed(name);
end

function inputTableAPI.WasReleased(name : string) : boolean
    return Input.WasReleased(name);
end

function inputTableAPI.FindBindingConflicts(name : string, slot : number, binding : InputBinding) : InputBindingChangeResult
    return Input.FindBindingConflicts(name, slot, binding);
end

local DISPLAY_KEY_NAMES = {
    SPACE = "Space",
    APOSTROPHE = "Apostrophe",
    COMMA = ",",
    MINUS = "-",
    PERIOD = ".",
    SLASH = "/",
    SEMICOLON = ";",
    EQUAL = "=",
    LEFT_BRACKET = "[",
    BACKSLASH = "\\",
    RIGHT_BRACKET = "]",
    GRAVE_ACCENT = "`",
    PAGE_UP = "Page Up",
    PAGE_DOWN = "Page Down",
    CAPS_LOCK = "Caps Lock",
    SCROLL_LOCK = "Scroll Lock",
    NUM_LOCK = "Num Lock",
    PRINT_SCREEN = "Print Screen",
    KP_DECIMAL = "Numpad .",
    KP_DIVIDE = "Numpad /",
    KP_MULTIPLY = "Numpad *",
    KP_SUBTRACT = "Numpad -",
    KP_ADD = "Numpad +",
    KP_ENTER = "Numpad Enter",
    KP_EQUAL = "Numpad =",
};

local function HasModifier(modifiers : number, modifier : number) : boolean
    return bit32.band(modifiers, modifier) ~= 0;
end

function inputTableAPI.FormatBinding(binding : InputBinding?) : string
    if binding == nil then
        return "Unbound";
    end

    local parts = {};
    local modifiers = binding.modifiers or InputModifier.None;
    if HasModifier(modifiers, InputModifier.Control) then table.insert(parts, "Ctrl"); end
    if HasModifier(modifiers, InputModifier.Shift) then table.insert(parts, "Shift"); end
    if HasModifier(modifiers, InputModifier.Alt) then table.insert(parts, "Alt"); end
    if HasModifier(modifiers, InputModifier.Super) then table.insert(parts, "Super"); end

    local controlName = "Unknown";
    if binding.device == InputDevice.Keyboard then
        local keyName = inputTableAPI.GetKeyName(binding.code);
        controlName = DISPLAY_KEY_NAMES[keyName] or string.gsub(keyName, "_", " ");
        if string.sub(controlName, 1, 3) == "KP " then
            controlName = `Numpad {string.sub(controlName, 4)}`;
        end
    elseif binding.device == InputDevice.Mouse then
        local mouseNames = { "Left Mouse", "Right Mouse", "Middle Mouse", "Mouse 4", "Mouse 5", "Mouse 6", "Mouse 7", "Mouse 8" };
        controlName = mouseNames[binding.code + 1] or "Unknown Mouse";
    elseif binding.device == InputDevice.MouseWheel then
        local wheelNames = { "Wheel Up", "Wheel Down", "Wheel Left", "Wheel Right" };
        controlName = wheelNames[binding.code + 1] or "Unknown Wheel";
    end

    table.insert(parts, controlName);
    return table.concat(parts, "+");
end

function inputTableAPI.BeginBindingCapture(callback : InputBindingCaptureCallback) : boolean
    return Input.BeginBindingCapture(callback);
end

function inputTableAPI.CancelBindingCapture() : boolean
    return Input.CancelBindingCapture();
end

function inputTableAPI.IsBindingCaptureActive() : boolean
    return Input.IsBindingCaptureActive();
end

function inputTableAPI.SaveBindings() : boolean
    return Input.SaveBindings();
end

function inputTableAPI.CreateContext(name : string, priority : number, sortOrder : number?) : boolean
    return Input.CreateContext(name, priority, sortOrder or 0);
end

function inputTableAPI.SetContextActive(name : string, active : boolean) : boolean
    return Input.SetContextActive(name, active);
end

function inputTableAPI.IsContextActive(name : string) : boolean
    return Input.IsContextActive(name);
end

function inputTableAPI.GetContexts() : { InputContextInfo }
    return Input.GetContexts();
end

return inputTableAPI;
