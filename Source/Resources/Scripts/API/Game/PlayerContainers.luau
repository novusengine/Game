type Bag =
{
    Events : Event,

    totalSlots : number,
    freeSlots : number,
    itemSlots : { BagSlot },
    uiSlots : { UIBagSlot },
    panel : Panel | nil,
    panelData : { },

    new : (parent : Canvas | Widget, slots : number, setupCallback : SetupCallback | nil, eventCallback : EventCallback | nil) -> Bag,

    CanAdd : (container : Bag, itemID : number, count : number) -> boolean,
    CanAddToSlot : (container : Bag, itemID : number, count : number, slotIndex : number, force : boolean) -> boolean,

    Add : (container : Bag, itemID : number, count : number) -> boolean,
    AddToSlot : (container : Bag, itemID : number, count : number, slotIndex : number, force : boolean) -> boolean,

    CanRemoveFromSlot : (container : Bag, itemID : number, count : number, slotIndex : number, force : boolean) -> boolean,
    RemoveFromSlot : (container : Bag, itemID : number, count : number, slotIndex : number, force : boolean) -> boolean,

    CanMoveToSlot : (container : Bag, srcSlot : number, destSlot : number) -> boolean,
    MoveToSlot : (container : Bag, srcSlot : number, destSlot : number) -> boolean,

    CanSwapSlots : (container : Bag, srcSlot : number, destSlot : number) -> boolean,
    SwapSlots : (container : Bag, srcSlot : number, destSlot : number) -> boolean,

    HandleCursorCommit : (container : Bag, slotIndex : number) -> boolean,
    UseItem : (container : Bag, slotIndex : number) -> boolean,
}

type PlayerContainersData =
{
    equipment : Bag,
    bags : { Bag },
    bank : { Bag },
    mail : Bag,
}

type PlayerContainersAPI =
{
    containerData : PlayerContainersData,

    GetEquipmentBag : () -> Bag,
    GetBags : () -> { Bag },
    GetBag : (bagIndex : number) -> Bag,
    GetBankBag : () -> Bag,
    GetMailBag : () -> Bag,

    SetEquipmentBag : (bag : Bag) -> (),
    SetBag : (bagIndex : number, bag : Bag) -> (),
    RemoveBag : (bagIndex : number) -> (),
    SetBankBag : (bag : Bag) -> (),
    SetMailBag : (bag : Bag) -> ()
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

function playerContainersAPI.SetEquipmentBag(bag : Bag)
    playerContainersAPI.containerData.equipment = bag
end

function playerContainersAPI.SetBag(bagIndex : number, bag : Bag)
    playerContainersAPI.containerData.bags[bagIndex] = bag
end

function playerContainersAPI.RemoveBag(bagIndex : number)
    playerContainersAPI.containerData.bags[bagIndex] = nil
end

function playerContainersAPI.SetBankBag(bag : Bag)
    playerContainersAPI.containerData.bank = bag
end

function playerContainersAPI.SetMailBag(bag : Bag)
    playerContainersAPI.containerData.mail = bag
end

return playerContainersAPI
