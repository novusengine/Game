local ItemSlot = require("@src/API/Game/UI/ItemSlot")
local DragContext = require("@src/API/Game/UI/DragContext")
local ItemUtil = require("@src/API/Game/ItemUtil")
local PlayerContainers = require("@src/API/Game/PlayerContainers")
local RootedWidget = require("@src/API/UI/RootedWidget")

UI.RegisterPanelTemplate("BagBackgroundPanel",
{
    color = vec.New(0.055, 0.065, 0.078),
    alpha = 0.98,
    cornerRadius = 5.0,
    borderColor = vec.New(0.20, 0.23, 0.28),
    borderSize = 1.0,
});

export type Event = number
export type Events =
{
    CanFitInSlot : number,
    AddedItem : number,
    RemovedItem : number,
    UseItem : number,
}

export type Bag =
{
    totalSlots : number,
    freeSlots : number,
    itemSlots : { BagSlot },
    uiSlots : { UIBagSlot },
    root : Panel | nil,
    panelData : { },
    containerIndex : integer,

    IsValidSlot : (container : Bag, slotIndex : number) -> boolean,

    GetItemID : (self : Bag, slotIndex : number) -> integer,
    GetItemCount : (self : Bag, slotIndex : number) -> number,

    SetLockedItem : (self : Bag, slotIndex : number, isLocked : boolean) -> boolean,
    LockItem : (self : Bag, slotIndex : number, isLocked : boolean) -> boolean,
    UnlockItem : (self : Bag, slotIndex : number) -> boolean,

    CanFitInSlot : (container : Bag, itemID : integer, count : number, slotIndex : number) -> boolean,
    AddToSlot : (container : Bag, itemID : integer, count : number, slotIndex : number) -> boolean,
    RemoveFromSlot : (container : Bag, itemID : integer, count : number, slotIndex : number) -> boolean,
    SwapSlots : (container : Bag, srcSlot : number, destSlot : number) -> boolean,

    UseItem : (container : Bag, slotIndex : number) -> boolean,

    HandleCursorCommit : (container : Bag, slotIndex : number) -> boolean,
}
export type BagAPI =
{
    Events : Events,
    New : (parent : (Canvas | Widget)?, slots : number, slotsPerRow : number?, setupCallback : SetupCallback?, eventCallback : EventCallback?) -> Bag,
    IsValidSlot : (container : Bag, slotIndex : number) -> boolean,
}
export type SetupCallback = (container : Bag) -> ()
export type EventCallback = (container : Bag, event : number, ...any) -> boolean

type BagSlot = 
{
    itemID : integer,
    count : number
}

type UIBagSlot =
{
    itemSlot : ItemSlot.UIItemSlot,
    isLocked : boolean
}

local DEFAULT_NUM_SLOTS_PER_ROW = 4

local bagAPI : BagAPI & { __index : any } =
{
    Events =
    {
        CanFitInSlot = 1,
        AddedItem = 2,
        RemovedItem = 3,
        UseItem = 4
    }
} :: any
bagAPI.__index = bagAPI

-- Static Helper Functions
local function AddItemToSlot(container : Bag, itemID : integer, count : number, slotIndex : number)
    local slotWasEmpty = container.itemSlots[slotIndex].itemID == 0i

    container.itemSlots[slotIndex] =
    {
        itemID = itemID,
        count = count
    }
    
    local uiBagSlot = container.uiSlots[slotIndex]
    uiBagSlot.itemSlot:SetItem(itemID)

    if slotWasEmpty then
        container.freeSlots -= 1
    end

    if container.eventCallback then
        container:eventCallback(bagAPI.Events.AddedItem, itemID, count, slotIndex)
    end
end

local function RemoveItemFromSlot(container : Bag, count : number, slotIndex : number)
    local itemSlot = container.itemSlots[slotIndex]
    
    if itemSlot.itemID == 0i then
        return
    end

    local numToRemove = math.min(itemSlot.count, count)
    itemSlot.count += numToRemove
    if itemSlot.count > 0 then
        return
    end

    local itemID = itemSlot.itemID

    itemSlot.itemID = 0i
    local uiBagSlot = container.uiSlots[slotIndex]
    uiBagSlot.itemSlot:SetItem(0i)
    uiBagSlot.isLocked = false
    container.freeSlots += 1;

    if container.eventCallback then
        container:eventCallback(bagAPI.Events.RemovedItem, itemID, count, slotIndex)
    end
end

-- Constructor
function bagAPI.New(parent : (Canvas | Widget)?, slots : number, slotsPerRow : number?, setupCallback : SetupCallback?, eventCallback : EventCallback?) : Bag
    local numSlots = if slots and slots > 0 then slots else 1
    local uiParent = parent or UI.GetCanvas("Bag", vec.New(0, 0), vec.New(1920, 1080));
    local numSlotsPerRow = if slotsPerRow and slotsPerRow > 0 then slotsPerRow else DEFAULT_NUM_SLOTS_PER_ROW

    local self = setmetatable({}, bagAPI)
    RootedWidget.Attach(self)

    self.totalSlots = numSlots
    self.freeSlots = numSlots
    self.itemSlots = { }
    self.uiSlots = { }
    self.root = nil
    self.panelData = {}
    self.containerIndex = 0i
    self.eventCallback = eventCallback

    for i = 1, numSlots do
        self.itemSlots[i] =
        {
            itemID = 0i,
            count = 0
        }

        self.uiSlots[i] = 
        {
            itemSlot = nil,
            isLocked = false
        }
    end

    if setupCallback ~= nil then
        setupCallback(self)
    else
        local numSlotsForWidth = math.min(slots, numSlotsPerRow)
        local numRows = math.ceil(slots / numSlotsPerRow)
        local width = 5 + numSlotsForWidth * (48 + 5)
        local height = 5 + numRows * (48 + 5)

        self.root = uiParent:NewPanel(vec.New(5, 5), vec.New(width, height), 100i, "BagBackgroundPanel")

        for i = 1, self.totalSlots do
            self.itemSlots[i] =
            {
                itemID = 0i,
                count = 0
            }

            local uiItemSlot = nil

            local index = i - 1
            local column = math.fmod(index, numSlotsPerRow)
            local row = math.floor(index / numSlotsPerRow)

            uiItemSlot = ItemSlot.New(self.root, self, i, nil)

            local posX = 5 + column * (48 + 5)
            local posY = 5 + row * (48 + 5)

            uiItemSlot:SetPos(vec.New(posX, -posY))
            uiItemSlot:SetAnchor(vec.New(0.0, 1.0))
            uiItemSlot:SetRelativePoint(vec.New(0.0, 1.0))

            self.uiSlots[i] = 
            {
                itemSlot = uiItemSlot,
                isLocked = false
            }
        end
    end

    if self.root then
        self.panelData =
        {
            dragOffsetX = 0,
            dragOffsetY = 0
        }

        self.root:SetOnMouseDown(function(eventID, widget, button, mouseX, mouseY)
            self.panelData.dragOffsetX = mouseX - widget:GetWorldPosX()
            self.panelData.dragOffsetY = mouseY - widget:GetWorldPosY()
        end);
        
        self.root:SetOnMouseHeld(function(eventID, widget, mouseX, mouseY)
            local offsetX = self.panelData.dragOffsetX or 0
            local offsetY = self.panelData.dragOffsetY or 0
        
            local posX = math.clamp(mouseX - offsetX, 0, 1920 - widget:GetWidth())
            local posY = math.clamp(mouseY - offsetY, 0, 1080 - widget:GetHeight())
        
            widget:SetWorldPos(vec.New(posX, posY))
        end);
    end

    return self
end

-- Method
function bagAPI.IsValidSlot(self : Bag, slotIndex : number) : boolean
    return slotIndex >= 1 and slotIndex <= self.totalSlots
end

function bagAPI:GetItemID(slotIndex : number) : integer
    if not self:IsValidSlot(slotIndex) then
        return 0i
    end

    return self.itemSlots[slotIndex].itemID
end
function bagAPI:GetItemCount(slotIndex : number) : number
    if not self:IsValidSlot(slotIndex) then
        return 0
    end

    return self.itemSlots[slotIndex].count
end

function bagAPI:SetLockedItem(slotIndex : number, isLocked : boolean) : boolean
    if not self:IsValidSlot(slotIndex) then
        return false
    end

    self.uiSlots[slotIndex].isLocked = isLocked
    return true
end
function bagAPI:LockItem(slotIndex : number, isLocked : boolean) : boolean
    return self:SetLockedItem(slotIndex, true)
end
function bagAPI:UnlockItem(slotIndex : number) : boolean
    return self:SetLockedItem(slotIndex, false)
end

function bagAPI:CanFitInSlot(itemID : integer, count : number, slotIndex : number) : boolean
    if not self:IsValidSlot(slotIndex) then
        return false
    end

    local isSlotLocked = self.uiSlots[slotIndex].isLocked
    if isSlotLocked then
        return false
    end

    if self.eventCallback then
        return self:eventCallback(bagAPI.Events.CanFitInSlot, itemID, count, slotIndex)
    end

    return true
end

function bagAPI:ClearSlot(slotIndex : number) : boolean
    if not self:IsValidSlot(slotIndex) then
        return false
    end

    local itemSlot = self.itemSlots[slotIndex]
    if (itemSlot.itemID == 0i or itemSlot.count == 0) then
        return false
    end

    local count = -itemSlot.count
    RemoveItemFromSlot(self, count, slotIndex)
    return true
end

function bagAPI:AddToSlot(itemID : integer, count : number, slotIndex : number) : boolean
    local slot = if slotIndex > 0 then slotIndex else 1
    local countToAdd = if count ~= 0 then count else 1

    if not self:IsValidSlot(slot) then
        return false
    end

    if countToAdd > 0 then
        AddItemToSlot(self, itemID, countToAdd, slot)
    else
        RemoveItemFromSlot(self, countToAdd, slot)
    end

    return true
end
function bagAPI:SwapSlots(destBag : Bag, srcSlot : number, destSlot : number) : boolean
    if not self:IsValidSlot(srcSlot) or not destBag:IsValidSlot(destSlot) then
        return false
    end

    local srcItemID = self.itemSlots[srcSlot].itemID
    local srcItemCount = self.itemSlots[srcSlot].count
    local destItemID = destBag.itemSlots[destSlot].itemID
    local destItemCount = destBag.itemSlots[destSlot].count

    local srcSlotEmpty = srcItemID == 0i
    local destSlotEmpty = destItemID == 0i

    self.itemSlots[srcSlot], destBag.itemSlots[destSlot] = destBag.itemSlots[destSlot], self.itemSlots[srcSlot]

    self.uiSlots[srcSlot].itemSlot:SetItem(destItemID)
    destBag.uiSlots[destSlot].itemSlot:SetItem(srcItemID)

    if srcSlotEmpty and not destSlotEmpty then
        self.freeSlots -= 1;
        destBag.freeSlots += 1;
    elseif not srcSlotEmpty and destSlotEmpty then
        self.freeSlots += 1;
        destBag.freeSlots -= 1;
    end

    if self.eventCallback then
        if not srcSlotEmpty then
            self:eventCallback(bagAPI.Events.RemovedItem, srcItemID, srcItemCount, srcSlot)
        end

        if not destSlotEmpty then
            self:eventCallback(bagAPI.Events.AddedItem, destItemID, destItemCount, srcSlot)
        end
    end

    if destBag.eventCallback then
        if not destSlotEmpty then
            destBag:eventCallback(bagAPI.Events.RemovedItem, destItemID, destItemCount, destSlot)
        end

        if not srcSlotEmpty then
            destBag:eventCallback(bagAPI.Events.AddedItem, srcItemID, srcItemCount, destSlot)
        end
    end

    return true
end

function bagAPI:UseItem(slotIndex : number) : boolean
    if not self:IsValidSlot(slotIndex) then
        return false
    end

    local itemID = self:GetItemID(slotIndex)
    if itemID == 0i then
        return false
    end

    local equipmentBag = PlayerContainers.GetEquipmentBag()
    if self == equipmentBag then
        if self.eventCallback then
            return self:eventCallback(bagAPI.Events.UseItem, itemID, slotIndex)
        else
            return false
        end
    else
        local itemTemplate = Item.GetItemInfo(itemID)
        if itemTemplate.Category == ItemUtil.Category.Consumable then
            return false -- TODO : Implement consumable use
        elseif itemTemplate.Category == ItemUtil.Category.Quest then
            return false -- TODO : Implement quest item use
        elseif itemTemplate.Category == ItemUtil.Category.Currency then
            return false -- TODO : Implement currency use
        elseif itemTemplate.Category == ItemUtil.Category.Container then
            return false -- TODO : Implement container use
        elseif itemTemplate.Category == ItemUtil.Category.Armor or itemTemplate.Category == ItemUtil.Category.Weapon then        
            local equipType = ItemUtil.GetItemEquipType(itemTemplate)
            local equipSlot = ItemUtil.GetEquipSlotForEquipType(equipType)

            if equipSlot == ItemUtil.EquipSlot.Ring1 then
                local ring1ItemID = GetEquippedItem(ItemUtil.EquipSlot.Ring1)
                local ring2ItemID = GetEquippedItem(ItemUtil.EquipSlot.Ring2)

                if ring1ItemID ~= 0i and ring2ItemID == 0i then
                    equipSlot = ItemUtil.EquipSlot.Ring2
                end
            elseif equipSlot == ItemUtil.EquipSlot.Trinket1 then
                local trinket1ItemID = GetEquippedItem(ItemUtil.EquipSlot.Trinket1)
                local trinket2ItemID = GetEquippedItem(ItemUtil.EquipSlot.Trinket2)
                
                if trinket1ItemID ~= 0i and trinket2ItemID == 0i then
                    equipSlot = ItemUtil.EquipSlot.Trinket2
                end
            elseif equipSlot == ItemUtil.EquipSlot.MainHand and equipType == ItemUtil.EquipType.Weapon then
                local mainHandItemID = GetEquippedItem(ItemUtil.EquipSlot.MainHand)
                local offHandItemID = GetEquippedItem(ItemUtil.EquipSlot.OffHand)

                if mainHandItemID ~= 0i and offHandItemID == 0i then
                    equipSlot = ItemUtil.EquipSlot.OffHand
                end
            end
            
            local srcUIBagSlot = self.uiSlots[slotIndex]
            local destUIBagSlot = equipmentBag.uiSlots[integer.tonumber(equipSlot)]

            DragContext:BeginInventoryMove(srcUIBagSlot.itemSlot)
            DragContext:CommitInventoryMove(destUIBagSlot.itemSlot)
            return true
        end
    end

    return false
end

function bagAPI:HandleCursorCommit(slotIndex : number) : boolean
    if slotIndex < 1 or slotIndex > self.totalSlots then
        return false
    end

    local uiBagSlot = self.uiSlots[slotIndex]

    if DragContext:HasOperation() then
        DragContext:CommitInventoryMove(uiBagSlot.itemSlot)
    else
        if (uiBagSlot.itemSlot.itemID == 0i or uiBagSlot.isLocked) then
            return false
        end

        uiBagSlot.itemSlot:SetSelected(true)
        DragContext:BeginInventoryMove(uiBagSlot.itemSlot)
    end

    return true
end

return bagAPI;
