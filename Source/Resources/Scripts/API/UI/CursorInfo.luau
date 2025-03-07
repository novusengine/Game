type CursorInfo =
{
    itemSlot : {} | nil,
}

type CursorInfoModule =
{
    cursorPanel : Panel,
    cursorInfo : CursorInfo,

    HasItemInHand : (self : CursorInfoModule) -> boolean,
    GetItem : (self : CursorInfoModule) -> number,
    GiveItem : (self : CursorInfoModule, itemSlot : {}, itemID : number?) -> (),
    TakeItem : (self : CursorInfoModule, itemSlot : {}) -> (),

    ClearHand : (self : CursorInfoModule) -> ()
}

--local uiParent = UI.GetCanvas("Cursor", 0, 0, 1920, 1080);
local cursorInfoAPI : CursorInfoModule =
{
    cursorPanel = Cursor:NewPanel(0, 0, 48, 48, 0, "DialogBox"),
    cursorInfo =
    {
        itemSlot = nil
    }
}

local function SetItem(cursorInfoModule : CursorInfoModule, itemID : number)
    if itemID == 0 or itemID == cursorInfoModule:GetItem() then
        return
    end

    cursorInfoModule.cursorInfo.hasItemInHand = true
    cursorInfoModule.cursorInfo.itemID = itemID

    local itemTemplate = Item.GetItemInfo(itemID)

    local defaultIcon = { texture = "Data/Texture/interface/icons/inv_misc_questionmark.dds" }
    local itemIcon = if itemTemplate then Item.GetIconInfo(itemTemplate.IconID) else defaultIcon

    cursorInfoModule.cursorPanel:SetBackground(itemIcon.Texture)
    cursorInfoModule.cursorPanel:SetEnabled(true)
end

function cursorInfoAPI.HasItemInHand(self : CursorInfoModule) : boolean
    return self.cursorInfo.itemSlot ~= nil
end

function cursorInfoAPI.GetItem(self : CursorInfoModule) : number
    local itemID = if self.cursorInfo.itemSlot then self.cursorInfo.itemSlot.itemID else 0
    return itemID
end

function cursorInfoAPI.GiveItem(self : CursorInfoModule, itemSlot : table, itemID : number?)
    if not self:HasItemInHand() then
        if itemSlot.itemID == 0 then
            return
        end

        SetCursor("item")
        SetItem(self, itemSlot.itemID)
        self.cursorInfo.itemSlot = itemSlot
    end
end

function cursorInfoAPI.TakeItem(self : CursorInfoModule, itemSlot : table)
    local srcBag = self.cursorInfo.itemSlot.container
    local destBag = itemSlot.container

    local srcItemIndex = self.cursorInfo.itemSlot.slotIndex
    local destItemIndex = itemSlot.slotIndex

    local sentRequest = false

    local isSameSlot = srcBag == destBag and srcItemIndex == destItemIndex
    if not isSameSlot then
        local srcItemID = self.cursorInfo.itemSlot.itemID
        local srcItemCount = srcBag:GetItemCount(srcItemIndex)
        local destItemID = itemSlot.itemID
        local destItemCount = destBag:GetItemCount(destItemIndex)

        local canFitSrcItem = destBag:CanFitInSlot(srcItemID, srcItemCount, destItemIndex)
        local canFitDestItem = if destItemID ~= 0 then srcBag:CanFitInSlot(destItemID, destItemCount, srcItemIndex) else true

        if canFitSrcItem and canFitDestItem then
            if Container.RequestSwapSlots(srcBag.containerIndex, destBag.containerIndex, srcItemIndex, destItemIndex) then
                srcBag:LockItem(srcItemIndex)
                destBag:LockItem(destItemIndex)
                sentRequest = true
            end
        end
    end
        
    if not sentRequest then
        self.cursorInfo.itemSlot.panel:SetForeground("")
        itemSlot.panel:SetForeground("")
    end

    self:ClearHand()
end

function cursorInfoAPI.ClearHand(self : CursorInfoModule)
    if not self:HasItemInHand() then
        return
    end

    self.cursorInfo.itemSlot = nil
    self.cursorPanel:SetEnabled(false)
    SetCursor("point")
end

cursorInfoAPI.cursorPanel:SetEnabled(false)
cursorInfoAPI.cursorPanel:SetAnchor(0.0, 0.0)
cursorInfoAPI.cursorPanel:SetRelativePoint(0.05, 0.95)
cursorInfoAPI.cursorPanel:SetOnMouseUp(function(event, widget)
    if not cursorInfoAPI:HasItemInHand() then
        return
    end

    local itemID = cursorInfoAPI.cursorInfo.itemSlot.itemID
    cursorInfoAPI.cursorInfo.itemSlot:SetItem(itemID) -- Used to clear the foreground
    cursorInfoAPI:ClearHand()
    SetCursor("point")
end)

return cursorInfoAPI