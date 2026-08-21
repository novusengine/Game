local Button = require("@src/API/UI/Button");
local FrameManager = require("@src/API/Game/FrameManager");
local RootedWidget = require("@src/API/UI/RootedWidget");

export type OpenOptions =
{
    title : string?,
    message : string?,
    onConfirm : (() -> ())?,
    onCancel : (() -> ())?,
}

export type Options =
{
    name : string,
    title : string?,
    message : string?,
    confirmText : string?,
    cancelText : string?,
    showActions : boolean?,
    onConfirm : (() -> ())?,
    onCancel : (() -> ())?,
    dismissible : boolean?,

    shieldSize : vec?,
    shieldTemplate : string?,
    shieldColor : vec?,
    shieldAlpha : number?,

    dialogTemplate : string?,
    dialogColor : vec?,
    dialogAlpha : number?,
    dialogBorderColor : vec?,
    dialogBorderSize : number?,

    titleTemplate : string?,
    titlePosition : vec?,
    messageTemplate : string?,
    messagePosition : vec?,
    messageWidth : number?,

    buttonSize : vec?,
    confirmButtonOptions : Button.Options?,
    cancelButtonOptions : Button.Options?,
}

export type Modal = RootedWidget.RootedPanel<
{
    root : Panel,
    dialog : Panel,
    title : TextWidget?,
    message : TextWidget?,
    confirmButton : Button.Button?,
    cancelButton : Button.Button?,

    Open : (self : Modal, options : OpenOptions?) -> boolean,
    Close : (self : Modal) -> boolean,
    Confirm : (self : Modal) -> boolean,
    Cancel : (self : Modal) -> boolean,
    IsOpen : (self : Modal) -> boolean,
    SetTitle : (self : Modal, title : string) -> (),
    SetMessage : (self : Modal, message : string) -> (),
    SetOnConfirm : (self : Modal, callback : (() -> ())?) -> (),
    SetOnCancel : (self : Modal, callback : (() -> ())?) -> (),
}>

export type ModalAPI =
{
    New : (parent : Widget, position : vec, size : vec, layer : integer, options : Options) -> Modal,
}

local modalAPI : ModalAPI = { };

local function BuildButtonOptions(text : string, source : Button.Options?) : Button.Options
    local result = table.clone(source or { }) :: any;
    result.text = text;
    return result;
end

function modalAPI.New(parent : Widget, position : vec, size : vec, layer : integer, options : Options) : Modal
    local shield = parent:NewPanel(
        vec.New(0, 0),
        options.shieldSize or vec.New(1920, 1080),
        layer,
        options.shieldTemplate or "DebugDarkGrey"
    );
    shield:SetAnchor(vec.New(0.0, 1.0));
    shield:SetRelativePoint(vec.New(0.0, 1.0));
    shield:SetColor(options.shieldColor or vec.New(0.02, 0.025, 0.03));
    shield:SetAlpha(if options.shieldAlpha ~= nil then options.shieldAlpha else 0.55);
    shield:SetBorderSize(0.0);
    shield:SetInteractable(true);
    shield:SetOnMouseDown(function() end);
    shield:SetOnMouseScroll(function() end);

    local dialog = shield:NewPanel(position, size, 1i, options.dialogTemplate or "DialogBox");
    dialog:SetAnchor(vec.New(0.5, 0.5));
    dialog:SetRelativePoint(vec.New(0.5, 0.5));
    if options.dialogColor ~= nil then dialog:SetColor(options.dialogColor); end
    if options.dialogAlpha ~= nil then dialog:SetAlpha(options.dialogAlpha); end
    if options.dialogBorderColor ~= nil then dialog:SetBorderColor(options.dialogBorderColor); end
    if options.dialogBorderSize ~= nil then dialog:SetBorderSize(options.dialogBorderSize); end

    local self = {
        root = shield,
        dialog = dialog,
        title = nil,
        message = nil,
        confirmButton = nil,
        cancelButton = nil,
        _defaultOnConfirm = options.onConfirm,
        _defaultOnCancel = options.onCancel,
        _onConfirm = options.onConfirm,
        _onCancel = options.onCancel,
        _closeReason = nil,
    } :: any;
    RootedWidget.Attach(self);

    local function CreateTitle(value : string) : TextWidget
        local title = dialog:NewText(
            value,
            options.titlePosition or vec.New(12, -12),
            1i,
            options.titleTemplate or "DefaultButtonText"
        );
        title:SetAnchor(vec.New(0.0, 1.0));
        title:SetRelativePoint(vec.New(0.0, 1.0));
        self.title = title;
        return title;
    end

    local function CreateMessage(value : string) : TextWidget
        local message = dialog:NewText(
            value,
            options.messagePosition or vec.New(12, -42),
            1i,
            options.messageTemplate or "DefaultButtonText"
        );
        message:SetAnchor(vec.New(0.0, 1.0));
        message:SetRelativePoint(vec.New(0.0, 1.0));
        message:SetWrapWidth(options.messageWidth or size.x - 24);
        self.message = message;
        return message;
    end

    if options.title ~= nil then CreateTitle(options.title); end
    if options.message ~= nil then CreateMessage(options.message); end

    function self:SetTitle(value : string)
        local title = self.title or CreateTitle(value);
        title:SetText(value);
    end

    function self:SetMessage(value : string)
        local message = self.message or CreateMessage(value);
        message:SetText(value);
    end

    function self:SetOnConfirm(callback : (() -> ())?)
        self._defaultOnConfirm = callback;
        if not FrameManager.IsOpen(options.name) then self._onConfirm = callback; end
    end

    function self:SetOnCancel(callback : (() -> ())?)
        self._defaultOnCancel = callback;
        if not FrameManager.IsOpen(options.name) then self._onCancel = callback; end
    end

    function self:IsOpen() : boolean
        return FrameManager.IsOpen(options.name);
    end

    function self:Open(openOptions : OpenOptions?) : boolean
        if openOptions ~= nil then
            if openOptions.title ~= nil then self:SetTitle(openOptions.title); end
            if openOptions.message ~= nil then self:SetMessage(openOptions.message); end
            self._onConfirm = openOptions.onConfirm or self._defaultOnConfirm;
            self._onCancel = openOptions.onCancel or self._defaultOnCancel;
        else
            self._onConfirm = self._defaultOnConfirm;
            self._onCancel = self._defaultOnCancel;
        end
        self._closeReason = nil;
        return FrameManager.Open(options.name);
    end

    function self:Confirm() : boolean
        self._closeReason = "confirm";
        return FrameManager.Close(options.name);
    end

    function self:Cancel() : boolean
        self._closeReason = "cancel";
        return FrameManager.Close(options.name);
    end

    function self:Close() : boolean
        return self:Cancel();
    end

    if options.showActions ~= false then
        local buttonSize = options.buttonSize or vec.New(96, 28);
        local confirm = Button.New(
            dialog,
            vec.New(-(buttonSize.x + 20), 12),
            buttonSize,
            1i,
            BuildButtonOptions(options.confirmText or "Confirm", options.confirmButtonOptions)
        );
        confirm:SetAnchor(vec.New(1.0, 0.0));
        confirm:SetRelativePoint(vec.New(1.0, 0.0));
        confirm:SetOnMouseUp(function(_, _, mouseButton)
            if mouseButton == 0i then self:Confirm(); end
        end);
        self.confirmButton = confirm;

        local cancel = Button.New(
            dialog,
            vec.New(-12, 12),
            buttonSize,
            1i,
            BuildButtonOptions(options.cancelText or "Cancel", options.cancelButtonOptions)
        );
        cancel:SetAnchor(vec.New(1.0, 0.0));
        cancel:SetRelativePoint(vec.New(1.0, 0.0));
        cancel:SetOnMouseUp(function(_, _, mouseButton)
            if mouseButton == 0i then self:Cancel(); end
        end);
        self.cancelButton = cancel;
    end

    FrameManager.Register(options.name, {
        root = shield,
        role = "Modal",
        dismissible = options.dismissible,
        onClose = function()
            shield:SetEnabled(false);
            local closeReason = self._closeReason or "cancel";
            local callback = if closeReason == "confirm" then self._onConfirm else self._onCancel;
            self._closeReason = nil;
            self._onConfirm = self._defaultOnConfirm;
            self._onCancel = self._defaultOnCancel;
            if callback ~= nil then callback(); end
        end,
    });

    return self :: Modal;
end

return modalAPI;
