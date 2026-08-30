local PlayerContainers = require("@src/API/Game/PlayerContainers");
local GameTooltip = require("@src/API/Game/UI/GameTooltip");

export type ItemAction =
{
    itemID : integer,
    GetIcon : (self : ItemAction) -> string,
    IsAvailable : (self : ItemAction) -> boolean,
    Activate : (self : ItemAction) -> boolean,
    ShowTooltip : (self : ItemAction) -> (),
    HideTooltip : (self : ItemAction) -> (),
}
export type ItemActionAPI = { New : (itemID : integer) -> ItemAction }
local itemActionAPI : ItemActionAPI = { };

local function FindItem(itemID : integer)
    local bags = { PlayerContainers.GetEquipmentBag() };
    for _, bag in PlayerContainers.GetBags() do table.insert(bags, bag); end
    for _, bag in bags do
        if bag ~= nil then
            for slotIndex = 1, bag.totalSlots do
                if bag:GetItemID(slotIndex) == itemID and bag:GetItemCount(slotIndex) > 0 then
                    return bag, slotIndex;
                end
            end
        end
    end
    return nil, 0;
end

function itemActionAPI.New(itemID : integer)
    local self = { itemID = itemID };
    function self:GetIcon()
        local item = Item.GetItemInfo(self.itemID);
        local icon = item and Item.GetIconInfo(item.IconID) or nil;
        return icon and icon.Texture or "texture/unknown.png";
    end
    function self:IsAvailable()
        local bag = FindItem(self.itemID);
        return bag ~= nil;
    end
    function self:Activate()
        local bag, slotIndex = FindItem(self.itemID);
        return bag ~= nil and bag:UseItem(slotIndex);
    end
    function self:ShowTooltip()
        GameTooltip.SetItem(self.itemID);
        GameTooltip.Show();
    end
    function self:HideTooltip()
        GameTooltip.ClearLines();
        GameTooltip.Hide();
    end
    return self;
end

return itemActionAPI;
