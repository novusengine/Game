local ItemUtil = require("@src/API/Game/ItemUtil")
local GameTooltip = require("@src/API/Game/UI/GameTooltip")
local RootedWidget = require("@src/API/UI/RootedWidget")

export type Container =
{
    HandleCursorCommit : (self : Container, slotIndex : number) -> boolean,
    UseItem : (self : Container, slotIndex : number) -> boolean,
}

export type UIItemSlot =
{
    root : Panel,
    container : Container,
    slotIndex : number,
    itemID : integer,
    defaultTexture : string,
    cursorCommitted : boolean,
    selectionBorder : Panel,

    SetPos : (self : UIItemSlot, position : vec) -> (),
    SetAnchor : (self : UIItemSlot, anchor : vec) -> (),
    SetRelativePoint : (self : UIItemSlot, point : vec) -> (),
    SetItem : (self : UIItemSlot, itemID : integer) -> (),
    SetSelected : (self : UIItemSlot, selected : boolean) -> (),

    UseItem : (self : UIItemSlot) -> boolean,
}

export type ItemSlotAPI = { New : (parent : (Canvas | Widget)?, container : Container, bagIndex : number, equipType : integer?, size : vec?) -> UIItemSlot }
local itemSlotAPI : ItemSlotAPI & { __index : any } = { } :: any
itemSlotAPI.__index = itemSlotAPI
local SELECTION_BORDER_COLOR = vec.New(1.0, 0.82, 0.15)
local SELECTION_BORDER_SIZE = 2.0

-- Static Helper Function
local function HandleCursorCommit(uiItemSlot : UIItemSlot)
    uiItemSlot.container:HandleCursorCommit(uiItemSlot.slotIndex)
    uiItemSlot.cursorCommitted = false
end

-- Constructor
function itemSlotAPI.New(parent : (Canvas | Widget)?, bag : Container, bagIndex : number, equipType : integer?, size : vec?) : UIItemSlot
    local texture = if equipType then ItemUtil.GetEquipTypeTexture(equipType) else "texture/ui/icons/itemslots/slot_empty.png"
    local uiParent = parent or UI.GetCanvas("ItemSlot", vec.New(0, 0), vec.New(1920, 1080))
    local slotSize = size or vec.New(48, 48)

    local self = setmetatable({ }, itemSlotAPI)

    self.root = uiParent:NewPanel(vec.New(0, 0), slotSize, 0i, "TexturePanel")
    RootedWidget.Attach(self)
    self.container = bag
    self.slotIndex = bagIndex
    self.itemID = 0i
    self.defaultTexture = texture
    self.cursorCommitted = false
    self.useItemCommitted = false

    self.root:SetAnchor(vec.New(0.5, 0.5))
    self.root:SetRelativePoint(vec.New(0.5, 0.5))
    self.root:SetBackground(texture)
    self.root:SetEnabled(true)

    self.selectionBorder = self.root:NewPanel(vec.New(0, 0), slotSize, 1i, "DebugDarkGrey")
    self.selectionBorder:SetAnchor(vec.New(0.5, 0.5))
    self.selectionBorder:SetRelativePoint(vec.New(0.5, 0.5))
    self.selectionBorder:SetAlpha(0.0)
    self.selectionBorder:SetBorderColor(SELECTION_BORDER_COLOR)
    self.selectionBorder:SetBorderSize(SELECTION_BORDER_SIZE)
    self.selectionBorder:SetInteractable(false)
    self.selectionBorder:SetEnabled(false)

    self.root:SetOnMouseDown(function(eventID, widget, button, mouseX, mouseY)
        if button == 0i then
            self.cursorCommitted = true
        elseif button == 1i then
            self.useItemCommitted = true
        end
    end)

    self.root:SetOnMouseUp(function(eventID, widget, button, mouseX, mouseY)
        if button == 0i then
            if self.cursorCommitted then
                HandleCursorCommit(self)
            end
        elseif button == 1i and self.useItemCommitted then
            if self:UseItem() then
                GameTooltip.ClearLines()
                if GameTooltip.IsShown() then
                    GameTooltip.Hide()
                end

                self.useItemCommitted = false
            end
        end
    end)

    self.root:SetOnHoverBegin(function(eventID, widget)
        if self.itemID == 0i then
            return
        end

        GameTooltip.SetItem(self.itemID)
        if not GameTooltip.IsShown() then
            GameTooltip.Show()
        end
    end)

    self.root:SetOnHoverHeld(function(eventID, widget)
        if self.itemID == 0i then
            return
        end

        if GameTooltip.ContainsItem(self.itemID) then
            return
        end

        GameTooltip.SetItem(self.itemID)
        if not GameTooltip.IsShown() then
            GameTooltip.Show()
        end
    end)

    self.root:SetOnHoverEnd(function(eventID, widget)
        if self.cursorCommitted then
            HandleCursorCommit(self)
        end

        if self.itemID == 0i and not self.useItemCommitted then
            return
        end

        GameTooltip.ClearLines()
        if GameTooltip.IsShown() then
            GameTooltip.Hide()
        end

        self.useItemCommitted = false
    end)

    return self
end

-- Methods
function itemSlotAPI:SetPos(position : vec) : ()
    self.root:SetPos(position)
end

function itemSlotAPI:SetAnchor(anchor : vec) : ()
    self.root:SetAnchor(anchor)
end

function itemSlotAPI:SetRelativePoint(point : vec) : ()
    self.root:SetRelativePoint(point)
end

function itemSlotAPI:SetSelected(selected : boolean) : ()
    self.selectionBorder:SetEnabled(selected)
end

function itemSlotAPI:SetItem(itemID : integer) : ()
    if self == nil or self.root == nil or itemID == nil then
        return
    end

    if itemID == 0i or self.itemID == itemID then
        if itemID == 0i then
            self.itemID = itemID
            self.root:SetBackground(self.defaultTexture)
        end

        self:SetSelected(false)
        return
    end

    self.itemID = itemID

    local itemTemplate = Item.GetItemInfo(self.itemID)
    local defaultIcon : Icon = { Texture = "texture/unknown.png" }
    local itemIcon = if itemTemplate then Item.GetIconInfo(itemTemplate.IconID) else defaultIcon

    self.root:SetBackground(itemIcon.Texture)
    self:SetSelected(false)
end

function itemSlotAPI:UseItem() : boolean
    if self.itemID == 0i then
        return false
    end

    return self.container:UseItem(self.slotIndex)
end

return itemSlotAPI
