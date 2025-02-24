local ItemUtil = require("@src/API/Game/ItemUtil")
local GameTooltip = require("@src/API/Game/GameTooltip")

export type UIItemSlot =
{
    panel : Panel,
    container : Bag,
    slotIndex : number,
    itemID : number,
    defaultTexture : string,
    cursorCommitted : boolean,

    SetPos : (self : UIItemSlot, posX : number, posY : number) -> (),
    SetAnchor : (self : UIItemSlot, anchorX : number, anchorY : number) -> (),
    SetRelativePoint : (self : UIItemSlot, relPointX : number, relPointY : number) -> (),
    SetItem : (self : UIItemSlot, itemID : number) -> (),

    UseItem : (self : UIItemSlot) -> boolean,
}

local itemSlotAPI = { }
itemSlotAPI.__index = itemSlotAPI

-- Static Helper Function
local function HandleCursorCommit(uiItemSlot : UIItemSlot)
    uiItemSlot.container:HandleCursorCommit(uiItemSlot.slotIndex)
    uiItemSlot.cursorCommitted = false
end

-- Constructor
function itemSlotAPI.Create(parent : Canvas | Widget, bag : Bag, bagIndex : number, equipType : number) : UIItemSlot
    local texture = if equipType then ItemUtil.GetEquipTypeTexture(equipType) else "Data/Texture/interface/paperdoll/ui-backpack-emptyslot.dds"
    local uiParent = parent or UI.GetCanvas("ItemSlot", 0, 0, 1920, 1080)

    local self = setmetatable({ }, itemSlotAPI)

    self.panel = uiParent:NewPanel(0, 0, 48, 48, 0, "DialogBox")
    self.container = bag
    self.slotIndex = bagIndex
    self.itemID = 0
    self.defaultTexture = texture
    self.cursorCommitted = false
    self.useItemCommitted = false

    self.panel:SetAnchor(0.5, 0.5)
    self.panel:SetRelativePoint(0.5, 0.5)
    self.panel:SetBackground(texture)
    self.panel:SetEnabled(true)

    self.panel:SetOnMouseDown(function(eventID, widget, button, mouseX, mouseY)
        if button == 0 then
            self.cursorCommitted = true
        elseif button == 1 then
            self.useItemCommitted = true
        end
    end)

    self.panel:SetOnMouseUp(function(eventID, widget, button, mouseX, mouseY)
        if button == 0 then
            if self.cursorCommitted then
                HandleCursorCommit(self)
            end
        elseif button == 1 and self.useItemCommitted then
            if self:UseItem() then
                GameTooltip.ClearLines()
                if GameTooltip.IsShown() then
                    GameTooltip.Hide()
                end
                
                self.useItemCommitted = false
            end
        end
    end)

    self.panel:SetOnHoverBegin(function(eventID, widget)
        if self.itemID == 0 then
            return
        end

        GameTooltip.SetItem(self.itemID)
        if not GameTooltip.IsShown() then
            GameTooltip.Show()
        end
    end)

    self.panel:SetOnHoverHeld(function(eventID, widget)
        if self.itemID == 0 then
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

    self.panel:SetOnHoverEnd(function(eventID, widget)
        if self.cursorCommitted then
            HandleCursorCommit(self)
        end

        if self.itemID == 0 and not self.useItemCommitted then
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
function itemSlotAPI:SetPos(posX : number, posY : number) : ()
    self.panel:SetPos(posX, posY)
end

function itemSlotAPI:SetAnchor(anchorX : number, anchorY : number) : ()
    self.panel:SetAnchor(anchorX, anchorY)
end

function itemSlotAPI:SetRelativePoint(relPointX : number, relPointY : number) : ()
    self.panel:SetRelativePoint(relPointX, relPointY)
end

function itemSlotAPI:SetItem(itemID : number) : ()
    if self == nil or self.panel == nil or itemID == nil then
        return
    end

    if itemID == 0 or self.itemID == itemID then
        if itemID == 0 then
            self.itemID = itemID
            self.panel:SetBackground(self.defaultTexture)
        end

        self.panel:SetForeground("")
        return
    end

    self.itemID = itemID

    local itemTemplate = Item.GetItemInfo(self.itemID)
    local defaultIcon : Icon = { Texture = "Data/Texture/interface/icons/inv_misc_questionmark.dds" }
    local itemIcon = if itemTemplate then Item.GetIconInfo(itemTemplate.IconID) else defaultIcon

    self.panel:SetBackground(itemIcon.Texture)
    self.panel:SetForeground("")
end

function itemSlotAPI:UseItem() : boolean
    if self.itemID == 0 then
        return false
    end

    return self.container:UseItem(self.slotIndex)
end

return itemSlotAPI