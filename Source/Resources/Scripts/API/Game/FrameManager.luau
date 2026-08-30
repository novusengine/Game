export type Role = "ManagedPanel" | "Auxiliary" | "WidePanel" | "Fullscreen" | "Modal" | "System";

export type Options =
{
    root : Widget?,
    role : Role,
    dismissible : boolean?,
    canClose : (() -> boolean)?,
    onOpen : (() -> ())?,
    onClose : (() -> ())?,
}

export type Frame =
{
    name : string,
    root : Widget?,
    role : Role,
    dismissible : boolean,
    canClose : (() -> boolean)?,
    onOpen : (() -> ())?,
    onClose : (() -> ())?,
    isOpen : boolean,
}

export type FrameManagerAPI =
{
    Register : (name : string, options : Options) -> Frame,
    Get : (name : string) -> Frame?,
    IsOpen : (name : string) -> boolean,
    IsRoleOpen : (role : Role) -> boolean,
    Open : (name : string) -> boolean,
    Close : (name : string) -> boolean,
    Toggle : (name : string) -> boolean,
    CloseTop : () -> boolean,
    CloseDismissible : () -> (),
    HandleEscape : (fallbackFrame : string?) -> boolean,
}

local frames : { [string] : Frame } = { };
local openOrder : { string } = { };
local frameManagerAPI : FrameManagerAPI = { };

local function RemoveFromOpenOrder(name : string)
    for index = #openOrder, 1, -1 do
        if openOrder[index] == name then
            table.remove(openOrder, index);
            return;
        end
    end
end

local function RolesConflict(openRole : Role, newRole : Role) : boolean
    if newRole == "System" then return true; end
    if newRole == "Modal" then return openRole == "Modal"; end
    if newRole == "Fullscreen" then
        return openRole == "ManagedPanel" or openRole == "Auxiliary" or openRole == "WidePanel" or openRole == "Fullscreen";
    end
    if newRole == "WidePanel" then
        return openRole == "ManagedPanel" or openRole == "WidePanel" or openRole == "Fullscreen";
    end
    if newRole == "ManagedPanel" then
        return openRole == "WidePanel" or openRole == "Fullscreen";
    end
    if newRole == "Auxiliary" then
        return openRole == "Fullscreen";
    end
    return false;
end

function frameManagerAPI.Register(name : string, options : Options) : Frame
    if frames[name] ~= nil then error(`FrameManager: frame '{name}' is already registered`); end
    local frame : Frame = {
        name = name,
        root = options.root,
        role = options.role,
        dismissible = options.dismissible ~= false,
        canClose = options.canClose,
        onOpen = options.onOpen,
        onClose = options.onClose,
        isOpen = false,
    };
    frames[name] = frame;
    if frame.root ~= nil then frame.root:SetEnabled(false); end
    return frame;
end

function frameManagerAPI.Get(name : string) : Frame?
    return frames[name];
end

function frameManagerAPI.IsOpen(name : string) : boolean
    local frame = frames[name];
    return frame ~= nil and frame.isOpen;
end

function frameManagerAPI.IsRoleOpen(role : Role) : boolean
    for _, name in openOrder do
        local frame = frames[name];
        if frame ~= nil and frame.isOpen and frame.role == role then return true; end
    end
    return false;
end

function frameManagerAPI.Close(name : string) : boolean
    local frame = frames[name];
    if frame == nil or not frame.isOpen then return false; end
    if frame.canClose ~= nil and not frame.canClose() then return false; end
    frame.isOpen = false;
    RemoveFromOpenOrder(name);
    if frame.onClose ~= nil then frame.onClose(); elseif frame.root ~= nil then frame.root:SetEnabled(false); end
    return true;
end

function frameManagerAPI.Open(name : string) : boolean
    local frame = frames[name];
    if frame == nil then error(`FrameManager: frame '{name}' is not registered`); end
    if frame.isOpen then
        RemoveFromOpenOrder(name);
        table.insert(openOrder, name);
        return true;
    end

    for index = #openOrder, 1, -1 do
        local openFrame = frames[openOrder[index]];
        if openFrame ~= nil and (frame.role == "System" or openFrame.dismissible) and RolesConflict(openFrame.role, frame.role) then
            if not frameManagerAPI.Close(openFrame.name) then return false; end
        end
    end

    if frame.role == "ManagedPanel" then
        local managedFrames : { Frame } = { };
        for _, openName in openOrder do
            local openFrame = frames[openName];
            if openFrame ~= nil and openFrame.role == "ManagedPanel" then table.insert(managedFrames, openFrame); end
        end
        if #managedFrames >= 2 then frameManagerAPI.Close(managedFrames[1].name); end
    end

    frame.isOpen = true;
    table.insert(openOrder, name);
    if frame.onOpen ~= nil then frame.onOpen(); elseif frame.root ~= nil then frame.root:SetEnabled(true); end
    return true;
end

function frameManagerAPI.Toggle(name : string) : boolean
    if frameManagerAPI.IsOpen(name) then return frameManagerAPI.Close(name); end
    return frameManagerAPI.Open(name);
end

function frameManagerAPI.CloseTop() : boolean
    for index = #openOrder, 1, -1 do
        local frame = frames[openOrder[index]];
        if frame ~= nil and frame.dismissible then return frameManagerAPI.Close(frame.name); end
    end
    return false;
end

function frameManagerAPI.CloseDismissible()
    for index = #openOrder, 1, -1 do
        local frame = frames[openOrder[index]];
        if frame ~= nil and frame.dismissible then frameManagerAPI.Close(frame.name); end
    end
end

function frameManagerAPI.HandleEscape(fallbackFrame : string?) : boolean
    if frameManagerAPI.CloseTop() then return true; end
    if fallbackFrame ~= nil then return frameManagerAPI.Open(fallbackFrame); end
    return false;
end

return frameManagerAPI;
