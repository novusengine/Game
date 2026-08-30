export type InputBinding =
{
    device : integer,
    code : integer,
    modifiers : integer?,
    modifierMatch : integer?,
}

export type InputBindingConflict =
{
    action : string,
    slot : integer,
    kind : integer,
}

export type InputBindingMutation =
{
    action : string,
    slot : integer,
    previousBinding : InputBinding?,
    binding : InputBinding?,
}

export type InputBindingChangeResult =
{
    status : integer,
    succeeded : boolean,
    requestedPolicy : integer,
    appliedPolicy : integer,
    conflicts : { InputBindingConflict },
    mutations : { InputBindingMutation },
}

export type InputActionInfo =
{
    name : string,
    displayName : string,
    category : string,
    context : string,
    defaultReply : integer,
    categorySortOrder : integer,
    sortOrder : integer,
    rebindable : boolean,
    defaultBindings : { [number] : InputBinding },
    bindings : { [number] : InputBinding },
}

export type InputActionOptions =
{
    context : string?,
    defaultReply : integer?,
    rebindable : boolean?,
    categorySortOrder : integer?,
    sortOrder : integer?,
    secondaryBinding : InputBinding?,
}

export type InputContextInfo =
{
    name : string,
    priority : integer,
    sortOrder : integer,
    active : boolean,
}

export type InputActionCallback = (phase : integer, device : integer, code : integer, modifiers : integer, value : number) -> integer
export type InputBindingCaptureCallback = (binding : InputBinding?) -> ()

type InputTableAPI =
{
    keyNames : { [integer] : string },
    KeyValues : { [string] : integer },
    GetKeyName : (keyID : integer) -> string,
    GetKeyValue : (keyName : string) -> integer,
    Keyboard : (keyID : integer, modifiers : integer?, modifierMatch : integer?) -> InputBinding,
    Mouse : (buttonID : integer, modifiers : integer?, modifierMatch : integer?) -> InputBinding,
    MouseWheel : (direction : integer, modifiers : integer?, modifierMatch : integer?) -> InputBinding,
    RegisterAction : (name : string, displayName : string, category : string, binding : InputBinding, options : InputActionOptions?) -> boolean,
    Bind : (name : string, displayName : string, category : string, binding : InputBinding, callback : InputActionCallback, options : InputActionOptions?) -> integer,
    Connect : (name : string, callback : InputActionCallback) -> integer,
    Disconnect : (connection : integer) -> boolean,
    SetBinding : (name : string, slot : integer, binding : InputBinding, policy : integer?) -> InputBindingChangeResult,
    ClearBinding : (name : string, slot : integer, policy : integer?) -> InputBindingChangeResult,
    ResetBinding : (name : string, slot : integer, policy : integer?) -> InputBindingChangeResult,
    GetBinding : (name : string, slot : integer) -> InputBinding?,
    GetActions : () -> { InputActionInfo },
    IsDown : (name : string) -> boolean,
    WasPressed : (name : string) -> boolean,
    WasReleased : (name : string) -> boolean,
    FindBindingConflicts : (name : string, slot : integer, binding : InputBinding) -> InputBindingChangeResult,
    FormatBinding : (binding : InputBinding?) -> string,
    BeginBindingCapture : (callback : InputBindingCaptureCallback) -> boolean,
    CancelBindingCapture : () -> boolean,
    IsBindingCaptureActive : () -> boolean,
    SaveBindings : () -> boolean,
    CreateContext : (name : string, priority : integer, sortOrder : integer?) -> boolean,
    SetContextActive : (name : string, active : boolean) -> boolean,
    IsContextActive : (name : string) -> boolean,
    GetContexts : () -> { InputContextInfo },
    IsMouseCaptured : () -> boolean,
    GetPenPressure : () -> number,
    IsPenInRange : () -> boolean,
    IsPenInContact : () -> boolean,
    GetPointerDelta : () -> vec,
    BeginPointerCapture : () -> boolean,
    EndPointerCapture : () -> boolean,
}

local inputTableAPI : InputTableAPI = { };

inputTableAPI.keyNames =
{
    [32i]  = "SPACE",
    [39i]  = "APOSTROPHE",
    [44i]  = "COMMA",
    [45i]  = "MINUS",
    [46i]  = "PERIOD",
    [47i]  = "SLASH",
    [48i]  = "0",
    [49i]  = "1",
    [50i]  = "2",
    [51i]  = "3",
    [52i]  = "4",
    [53i]  = "5",
    [54i]  = "6",
    [55i]  = "7",
    [56i]  = "8",
    [57i]  = "9",
    [59i]  = "SEMICOLON",
    [61i]  = "EQUAL",
    [65i]  = "A",
    [66i]  = "B",
    [67i]  = "C",
    [68i]  = "D",
    [69i]  = "E",
    [70i]  = "F",
    [71i]  = "G",
    [72i]  = "H",
    [73i]  = "I",
    [74i]  = "J",
    [75i]  = "K",
    [76i]  = "L",
    [77i]  = "M",
    [78i]  = "N",
    [79i]  = "O",
    [80i]  = "P",
    [81i]  = "Q",
    [82i]  = "R",
    [83i]  = "S",
    [84i]  = "T",
    [85i]  = "U",
    [86i]  = "V",
    [87i]  = "W",
    [88i]  = "X",
    [89i]  = "Y",
    [90i]  = "Z",
    [91i]  = "LEFT_BRACKET",
    [92i]  = "BACKSLASH",
    [93i]  = "RIGHT_BRACKET",
    [96i]  = "GRAVE_ACCENT",
    [161i] = "WORLD_1",
    [162i] = "WORLD_2",
    [256i] = "ESCAPE",
    [257i] = "ENTER",
    [258i] = "TAB",
    [259i] = "BACKSPACE",
    [260i] = "INSERT",
    [261i] = "DELETE",
    [262i] = "RIGHT",
    [263i] = "LEFT",
    [264i] = "DOWN",
    [265i] = "UP",
    [266i] = "PAGE_UP",
    [267i] = "PAGE_DOWN",
    [268i] = "HOME",
    [269i] = "END",
    [280i] = "CAPS_LOCK",
    [281i] = "SCROLL_LOCK",
    [282i] = "NUM_LOCK",
    [283i] = "PRINT_SCREEN",
    [284i] = "PAUSE",
    [290i] = "F1",
    [291i] = "F2",
    [292i] = "F3",
    [293i] = "F4",
    [294i] = "F5",
    [295i] = "F6",
    [296i] = "F7",
    [297i] = "F8",
    [298i] = "F9",
    [299i] = "F10",
    [300i] = "F11",
    [301i] = "F12",
    [302i] = "F13",
    [303i] = "F14",
    [304i] = "F15",
    [305i] = "F16",
    [306i] = "F17",
    [307i] = "F18",
    [308i] = "F19",
    [309i] = "F20",
    [310i] = "F21",
    [311i] = "F22",
    [312i] = "F23",
    [313i] = "F24",
    [314i] = "F25",
    [320i] = "KP_0",
    [321i] = "KP_1",
    [322i] = "KP_2",
    [323i] = "KP_3",
    [324i] = "KP_4",
    [325i] = "KP_5",
    [326i] = "KP_6",
    [327i] = "KP_7",
    [328i] = "KP_8",
    [329i] = "KP_9",
    [330i] = "KP_DECIMAL",
    [331i] = "KP_DIVIDE",
    [332i] = "KP_MULTIPLY",
    [333i] = "KP_SUBTRACT",
    [334i] = "KP_ADD",
    [335i] = "KP_ENTER",
    [336i] = "KP_EQUAL",
    [340i] = "LEFT_SHIFT",
    [341i] = "LEFT_CONTROL",
    [342i] = "LEFT_ALT",
    [343i] = "LEFT_SUPER",
    [344i] = "RIGHT_SHIFT",
    [345i] = "RIGHT_CONTROL",
    [346i] = "RIGHT_ALT",
    [347i] = "RIGHT_SUPER",
    [348i] = "MENU",
}

inputTableAPI.KeyValues = {};
for keyID, keyName in inputTableAPI.keyNames do
    inputTableAPI.KeyValues[keyName] = keyID;
end

function inputTableAPI.GetKeyName(keyID : integer) : string
    return inputTableAPI.keyNames[keyID] or "";
end

function inputTableAPI.GetKeyValue(keyName : string) : integer
    return inputTableAPI.KeyValues[keyName] or -1i;
end

function inputTableAPI.Keyboard(keyID : integer, modifiers : integer?, modifierMatch : integer?) : InputBinding
    return {
        device = InputDevice.Keyboard,
        code = keyID,
        modifiers = modifiers or InputModifier.None,
        modifierMatch = modifierMatch or InputModifierMatch.Exact,
    };
end

function inputTableAPI.Mouse(buttonID : integer, modifiers : integer?, modifierMatch : integer?) : InputBinding
    return {
        device = InputDevice.Mouse,
        code = buttonID,
        modifiers = modifiers or InputModifier.None,
        modifierMatch = modifierMatch or InputModifierMatch.Exact,
    };
end

function inputTableAPI.MouseWheel(direction : integer, modifiers : integer?, modifierMatch : integer?) : InputBinding
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
) : integer
    if not inputTableAPI.RegisterAction(name, displayName, category, binding, options) then
        return 0i;
    end

    return Input.Connect(name, callback);
end

function inputTableAPI.Connect(name : string, callback : InputActionCallback) : integer
    return Input.Connect(name, callback);
end

function inputTableAPI.Disconnect(connection : integer) : boolean
    return Input.Disconnect(connection);
end

function inputTableAPI.SetBinding(name : string, slot : integer, binding : InputBinding, policy : integer?) : InputBindingChangeResult
    return Input.SetBinding(name, slot, binding, policy or InputBindingConflictPolicy.Reject);
end

function inputTableAPI.ClearBinding(name : string, slot : integer, policy : integer?) : InputBindingChangeResult
    return Input.ClearBinding(name, slot, policy or InputBindingConflictPolicy.Reject);
end

function inputTableAPI.ResetBinding(name : string, slot : integer, policy : integer?) : InputBindingChangeResult
    return Input.ResetBinding(name, slot, policy or InputBindingConflictPolicy.Reject);
end

function inputTableAPI.GetBinding(name : string, slot : integer) : InputBinding?
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

function inputTableAPI.FindBindingConflicts(name : string, slot : integer, binding : InputBinding) : InputBindingChangeResult
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

local function HasModifier(modifiers : integer, modifier : integer) : boolean
    return integer.btest(modifiers, modifier);
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
        controlName = mouseNames[integer.tonumber(integer.add(binding.code, 1i))] or "Unknown Mouse";
    elseif binding.device == InputDevice.MouseWheel then
        local wheelNames = { "Wheel Up", "Wheel Down", "Wheel Left", "Wheel Right" };
        controlName = wheelNames[integer.tonumber(integer.add(binding.code, 1i))] or "Unknown Wheel";
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

function inputTableAPI.CreateContext(name : string, priority : integer, sortOrder : integer?) : boolean
    return Input.CreateContext(name, priority, sortOrder or 0i);
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

function inputTableAPI.IsMouseCaptured() : boolean
    return Input.IsMouseCaptured();
end

function inputTableAPI.GetPenPressure() : number
    return Input.GetPenPressure();
end

function inputTableAPI.IsPenInRange() : boolean
    return Input.IsPenInRange();
end

function inputTableAPI.IsPenInContact() : boolean
    return Input.IsPenInContact();
end

function inputTableAPI.GetPointerDelta() : vec
    return Input.GetPointerDelta();
end

function inputTableAPI.BeginPointerCapture() : boolean
    return Input.BeginPointerCapture();
end

function inputTableAPI.EndPointerCapture() : boolean
    return Input.EndPointerCapture();
end

return inputTableAPI;
