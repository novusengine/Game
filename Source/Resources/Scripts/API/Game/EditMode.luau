local EditModeDefaults = require("@src/API/Game/EditModeDefaults")
local EditModeProfile = require("@src/API/Game/EditModeProfile")
local EditModeOverlay = require("@src/API/Game/EditModeOverlay")
local FrameManager = require("@src/API/Game/FrameManager")

export type EditableElementRegistration =
{
    id          : EditModeDefaults.ElementID,
    size        : vec,
    getPos      : () -> vec,
    setPos      : (vec) -> (),
    isEnabled   : () -> boolean,
    setEnabled  : (boolean) -> (),
}

export type EditableElement = EditableElementRegistration &
{
    category    : EditModeDefaults.CategoryID,
    displayName : string,
}

export type EditModeAPI =
{
    Enter               : () -> (),
    Exit                : (discardChanges : boolean?) -> boolean,
    IsActive            : () -> boolean,
    Register            : (EditableElementRegistration) -> (),
    Unregister          : (EditModeDefaults.ElementID) -> (),
    GetElement          : (EditModeDefaults.ElementID) -> EditableElement?,
    GetElementsByCategory : (EditModeDefaults.CategoryID) -> { EditableElement },
    GetAllElements      : () -> { EditableElement },
    IsElementEnabled    : (EditModeDefaults.ElementID) -> boolean,
    SetElementEnabled   : (EditModeDefaults.ElementID, boolean) -> (),
    GetEnabledElements  : () -> { EditModeDefaults.ElementID },
    GetCanvas             : () -> Canvas?,
    GetRoot               : () -> Panel?,
    GetOverlay          : (EditModeDefaults.ElementID) -> EditModeOverlay.Overlay?,
    OnOverlayDragged    : (EditModeDefaults.ElementID, vec) -> (),
    ApplyActiveProfile  : () -> (),
    ApplyDefaultLayout  : () -> (),
    SaveActiveProfile   : () -> boolean,
    HasUnsavedChanges   : () -> boolean,
    CanSaveActiveProfile : () -> boolean,
    SetExitRequestHandler : (handler : (() -> boolean)?) -> (),
    RegisterOnEnter     : (callback : () -> ()) -> (),
    RegisterOnExit      : (callback : () -> ()) -> (),
    GetProfiles         : () -> EditModeProfile.EditModeProfileAPI,
    GetDefaults         : () -> EditModeDefaults.EditModeDefaultsAPI,
}

local editModeAPI : EditModeAPI = { }

local active : boolean = false
local elements : { [EditModeDefaults.ElementID] : EditableElement } = {}
local enabledElements : { [EditModeDefaults.ElementID] : boolean } = {}
local overlays : { [EditModeDefaults.ElementID] : EditModeOverlay.Overlay } = {}
local wasVisible : { [EditModeDefaults.ElementID] : boolean } = {}
local editCanvas : Canvas? = nil
local editRoot : Panel? = nil
local exitRequestHandler : (() -> boolean)? = nil
local bypassCloseGuard : boolean = false
local enterCallbacks : { () -> () } = {}
local exitCallbacks : { () -> () } = {}

local function GetRegisteredElementsInDefaultOrder() : { EditableElement }
    local result : { EditableElement } = {}
    for _, elementDefault in EditModeDefaults.Elements do
        local element = elements[elementDefault.id]
        if element ~= nil then
            table.insert(result, element)
        end
    end
    return result
end

local function OnOverlayDragged(elementID : EditModeDefaults.ElementID, newPos : vec)
    editModeAPI.OnOverlayDragged(elementID, newPos)
end

local function ApplyLayouts(layouts : { [EditModeDefaults.ElementID] : EditModeProfile.ElementLayout })
    for _, element in GetRegisteredElementsInDefaultOrder() do
        local layout = layouts[element.id]
        if layout ~= nil then
            local position = vec.New(layout.posX, layout.posY)
            element.setPos(position)
            local overlay = overlays[element.id]
            if overlay ~= nil then
                EditModeOverlay.SetPosition(overlay, position)
            end
        end
    end
end

local function CreateOverlays()
    local root = editRoot
    if root == nil then
        error("EditMode: cannot create overlays without an edit root")
    end
    for _, element in GetRegisteredElementsInDefaultOrder() do
        if enabledElements[element.id] ~= false then
            local pos = element.getPos()
            local catDef = EditModeDefaults.GetCategory(element.category)
            local color = if catDef ~= nil then catDef.color else vec.New(0.5, 0.5, 0.5)
            local overlay = EditModeOverlay.Create(root, element.id, element.displayName, element.category, pos, color, element.size, OnOverlayDragged)
            overlays[element.id] = overlay
        end
    end
end

local function DestroyOverlays()
    for elementID, overlay in overlays do
        EditModeOverlay.Destroy(overlay)
    end
    overlays = {}
end

local function EnsureCanvas()
    if editCanvas ~= nil then
        return
    end
    local canvas = UI.GetCanvas("EditMode", vec.New(0, 0), vec.New(1920, 1080))
    canvas:SetLayer(100i)
    editCanvas = canvas
end

local function CreateEditModeRoot()
    local canvas = editCanvas
    if canvas == nil then
        error("EditMode: cannot create an edit root without a canvas")
    end
    local root = canvas:NewPanel(vec.New(0, 0), vec.New(1920, 1080), 0i, "DebugDarkGrey")
    root:SetAlpha(0.0)
    editRoot = root

    -- Blocker: full-screen invisible panel that consumes all input
    local blocker = root:NewPanel(vec.New(0, 0), vec.New(1920, 1080), 0i, "DebugDarkGrey")
    blocker:SetAlpha(0.0)

    local function ConsumeInput()
    end

    blocker:SetOnMouseDown(ConsumeInput)
    blocker:SetOnMouseUp(ConsumeInput)
    blocker:SetOnMouseHeld(ConsumeInput)
    blocker:SetOnMouseScroll(ConsumeInput)
    blocker:SetOnHoverBegin(ConsumeInput)
end

local function DestroyEditModeRoot()
    if editRoot ~= nil then
        UI.DestroyWidget(editRoot)
        editRoot = nil
    end
end

local function Activate()
    if active then
        return
    end

    active = true

    -- Snapshot current visibility
    for _, element in GetRegisteredElementsInDefaultOrder() do
        wasVisible[element.id] = element.isEnabled()
    end

    EditModeProfile.ApplyActiveLayout(ApplyLayouts)

    -- Force-show enabled elements
    for _, element in GetRegisteredElementsInDefaultOrder() do
        if enabledElements[element.id] ~= false then
            element.setEnabled(true)
        end
    end

    -- Create canvas and root with blocker
    EnsureCanvas()
    CreateEditModeRoot()

    -- Create overlays at layer 1 (above blocker at layer 0)
    CreateOverlays()

    for _, callback in enterCallbacks do
        callback()
    end
end

local function Deactivate()
    if not active then
        return
    end

    -- Leaving EditMode always restores the last committed profile layout.
    EditModeProfile.ApplyActiveLayout(ApplyLayouts)
    active = false

    for _, callback in exitCallbacks do
        callback()
    end

    -- Destroy overlays and root (cascades to all children, but keeps canvas alive)
    DestroyOverlays()
    DestroyEditModeRoot()

    -- Restore visibility
    for _, element in GetRegisteredElementsInDefaultOrder() do
        local was = wasVisible[element.id]
        if was ~= nil then
            element.setEnabled(was)
        end
    end

    wasVisible = {}
end

function editModeAPI.Enter()
    if active then
        return
    end
    FrameManager.Open("EditMode")
end

function editModeAPI.Exit(discardChanges : boolean?) : boolean
    bypassCloseGuard = discardChanges == true
    local closed = FrameManager.Close("EditMode")
    bypassCloseGuard = false
    return closed
end

function editModeAPI.IsActive() : boolean
    return active
end

function editModeAPI.Register(registration : EditableElementRegistration)
    local elementDefault = EditModeDefaults.GetElement(registration.id)
    if elementDefault == nil then
        error(`EditMode: element '{registration.id}' is not defined in EditModeDefaults`)
    end
    if elements[registration.id] ~= nil then
        error(`EditMode: element '{registration.id}' is already registered`)
    end

    local element : EditableElement = {
        id = registration.id,
        category = elementDefault.category,
        displayName = elementDefault.displayName,
        size = registration.size,
        getPos = registration.getPos,
        setPos = registration.setPos,
        isEnabled = registration.isEnabled,
        setEnabled = registration.setEnabled,
    }
    elements[element.id] = element
    enabledElements[element.id] = elementDefault.defaultEnabled

    if not active then
        return
    end

    wasVisible[element.id] = element.isEnabled()
    local layout = EditModeProfile.GetActiveProfile().layouts[element.id]
    if layout ~= nil then
        element.setPos(vec.New(layout.posX, layout.posY))
    end
    if enabledElements[element.id] then
        element.setEnabled(true)
        local root = editRoot
        if root == nil then
            error("EditMode: cannot register an active overlay without an edit root")
        end
        local categoryDefault = EditModeDefaults.GetCategory(element.category)
        local color = if categoryDefault ~= nil then categoryDefault.color else vec.New(0.5, 0.5, 0.5)
        overlays[element.id] = EditModeOverlay.Create(
            root,
            element.id,
            element.displayName,
            element.category,
            element.getPos(),
            color,
            element.size,
            OnOverlayDragged)
    end
end

function editModeAPI.Unregister(elementID : EditModeDefaults.ElementID)
    if active and overlays[elementID] ~= nil then
        EditModeOverlay.Destroy(overlays[elementID])
        overlays[elementID] = nil
    end
    local element = elements[elementID]
    if active and element ~= nil and wasVisible[elementID] ~= nil then
        element.setEnabled(wasVisible[elementID])
    end
    elements[elementID] = nil
    enabledElements[elementID] = nil
    wasVisible[elementID] = nil
end

function editModeAPI.GetElement(elementID : EditModeDefaults.ElementID) : EditableElement?
    return elements[elementID]
end

function editModeAPI.GetElementsByCategory(categoryID : EditModeDefaults.CategoryID) : { EditableElement }
    local result : { EditableElement } = {}
    for _, elementDefault in EditModeDefaults.GetElementsByCategory(categoryID) do
        local element = elements[elementDefault.id]
        if element ~= nil then
            table.insert(result, element)
        end
    end
    return result
end

function editModeAPI.GetAllElements() : { EditableElement }
    return GetRegisteredElementsInDefaultOrder()
end

function editModeAPI.IsElementEnabled(elementID : EditModeDefaults.ElementID) : boolean
    return enabledElements[elementID] == true
end

function editModeAPI.SetElementEnabled(elementID : EditModeDefaults.ElementID, enabled : boolean)
    local element = elements[elementID]
    if element == nil then
        return
    end
    local wasEnabled = enabledElements[elementID] == true
    enabledElements[elementID] = enabled

    if not active then
        return
    end

    if enabled and not wasEnabled then
        -- Enable: force-show element, create overlay
        element.setEnabled(true)
        local root = editRoot
        if root == nil then
            error("EditMode: cannot enable an overlay without an edit root")
        end
        local pos = element.getPos()
        local catDef = EditModeDefaults.GetCategory(element.category)
        local color = if catDef ~= nil then catDef.color else vec.New(0.5, 0.5, 0.5)
        local overlay = EditModeOverlay.Create(root, element.id, element.displayName, element.category, pos, color, element.size, OnOverlayDragged)
        overlays[elementID] = overlay
    elseif not enabled and wasEnabled then
        -- Disable: destroy overlay, restore original visibility
        if overlays[elementID] ~= nil then
            EditModeOverlay.Destroy(overlays[elementID])
            overlays[elementID] = nil
        end
        local was = wasVisible[elementID]
        if was ~= nil then
            element.setEnabled(was)
        end
    end
end

function editModeAPI.GetEnabledElements() : { EditModeDefaults.ElementID }
    local result : { EditModeDefaults.ElementID } = {}
    for _, elementDefault in EditModeDefaults.Elements do
        if enabledElements[elementDefault.id] then
            table.insert(result, elementDefault.id)
        end
    end
    return result
end

function editModeAPI.GetCanvas() : Canvas?
    return editCanvas
end

function editModeAPI.GetRoot() : Panel?
    return editRoot
end

function editModeAPI.GetOverlay(elementID : EditModeDefaults.ElementID) : EditModeOverlay.Overlay?
    return overlays[elementID]
end

function editModeAPI.OnOverlayDragged(elementID : EditModeDefaults.ElementID, newPos : vec)
    local element = elements[elementID]
    if element ~= nil then
        element.setPos(newPos)
    end
end

function editModeAPI.ApplyActiveProfile()
    EditModeProfile.ApplyActiveLayout(ApplyLayouts)
end

function editModeAPI.ApplyDefaultLayout()
    ApplyLayouts(EditModeProfile.GenerateDefaultProfile().layouts)
end

function editModeAPI.SaveActiveProfile() : boolean
    if not editModeAPI.CanSaveActiveProfile() then
        return false
    end

    local layouts : { [EditModeDefaults.ElementID] : EditModeProfile.ElementLayout } = {}
    for _, element in GetRegisteredElementsInDefaultOrder() do
        local position = element.getPos()
        layouts[element.id] = { posX = position.x, posY = position.y }
    end
    return EditModeProfile.SaveActiveLayout(layouts)
end

function editModeAPI.HasUnsavedChanges() : boolean
    local profile = EditModeProfile.GetActiveProfile()
    for _, element in GetRegisteredElementsInDefaultOrder() do
        local layout = profile.layouts[element.id]
        if layout ~= nil then
            local position = element.getPos()
            if math.abs(position.x - layout.posX) > 0.001 or math.abs(position.y - layout.posY) > 0.001 then
                return true
            end
        end
    end
    return false
end

function editModeAPI.CanSaveActiveProfile() : boolean
    return not EditModeProfile.IsDefaultProfile(EditModeProfile.GetActiveProfileName())
end

function editModeAPI.SetExitRequestHandler(handler : (() -> boolean)?)
    exitRequestHandler = handler
end

function editModeAPI.RegisterOnEnter(callback : () -> ())
    table.insert(enterCallbacks, callback)
end

function editModeAPI.RegisterOnExit(callback : () -> ())
    table.insert(exitCallbacks, callback)
end

function editModeAPI.GetProfiles() : EditModeProfile.EditModeProfileAPI
    return EditModeProfile
end

function editModeAPI.GetDefaults() : EditModeDefaults.EditModeDefaultsAPI
    return EditModeDefaults
end

local function CanClose() : boolean
    if bypassCloseGuard or not active or not editModeAPI.HasUnsavedChanges() then
        return true
    end
    if exitRequestHandler ~= nil then
        return exitRequestHandler()
    end
    return false
end

local function OnGameLoaded(eventID : integer, data : any)
    FrameManager.Register("EditMode", {
        role = "System",
        dismissible = true,
        canClose = CanClose,
        onOpen = Activate,
        onClose = Deactivate,
    })
end

RegisterEvent(GameEvent.Loaded, OnGameLoaded)

return editModeAPI
