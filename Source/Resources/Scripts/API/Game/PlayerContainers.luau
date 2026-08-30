export type Container =
{
    totalSlots : number,
    root : Panel?,
    GetItemID : (self : Container, slotIndex : number) -> integer,
    GetItemCount : (self : Container, slotIndex : number) -> number,
    UseItem : (self : Container, slotIndex : number) -> boolean,
}

export type PlayerContainersData =
{
    equipment : Container?,
    bags : { Container },
    bank : Container?,
    mail : Container?,
}

export type PlayerContainersAPI =
{
    containerData : PlayerContainersData,

    GetEquipmentBag : () -> Container?,
    GetBags : () -> { Container },
    GetBag : (bagIndex : number) -> Container?,
    GetBankBag : () -> Container?,
    GetMailBag : () -> Container?,

    SetEquipmentBag : (bag : Container) -> (),
    SetBag : (bagIndex : number, bag : Container) -> (),
    RemoveBag : (bagIndex : number) -> (),
    SetBankBag : (bag : Container) -> (),
    SetMailBag : (bag : Container) -> (),
    Subscribe : (callback : () -> ()) -> (() -> ()),
}

local playerContainerData =
{
    equipment = nil,
    bags = {},
    bank = nil,
    mail = nil
}
local playerContainersAPI : PlayerContainersAPI =
{
    containerData = playerContainerData
}
local changeCallbacks : { () -> () } = { }

local function NotifyChanged()
    local callbacks = table.clone(changeCallbacks)
    for _, callback in callbacks do
        callback()
    end
end

function playerContainersAPI.GetEquipmentBag()
    return playerContainersAPI.containerData.equipment
end

function playerContainersAPI.GetBags()
    return playerContainersAPI.containerData.bags
end

function playerContainersAPI.GetBag(bagIndex : number)
    return playerContainersAPI.containerData.bags[bagIndex]
end

function playerContainersAPI.GetBankBag()
    return playerContainersAPI.containerData.bank
end

function playerContainersAPI.GetMailBag()
    return playerContainersAPI.containerData.mail
end

function playerContainersAPI.SetEquipmentBag(bag : Container)
    playerContainersAPI.containerData.equipment = bag
    NotifyChanged()
end

function playerContainersAPI.SetBag(bagIndex : number, bag : Container)
    playerContainersAPI.containerData.bags[bagIndex] = bag
    NotifyChanged()
end

function playerContainersAPI.RemoveBag(bagIndex : number)
    playerContainersAPI.containerData.bags[bagIndex] = nil
    NotifyChanged()
end

function playerContainersAPI.SetBankBag(bag : Container)
    playerContainersAPI.containerData.bank = bag
    NotifyChanged()
end

function playerContainersAPI.SetMailBag(bag : Container)
    playerContainersAPI.containerData.mail = bag
    NotifyChanged()
end

function playerContainersAPI.Subscribe(callback : () -> ()) : (() -> ())
    table.insert(changeCallbacks, callback)

    return function()
        local index = table.find(changeCallbacks, callback)
        if index ~= nil then
            table.remove(changeCallbacks, index)
        end
    end
end

return playerContainersAPI
