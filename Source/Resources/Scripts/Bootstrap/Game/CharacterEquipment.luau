local ItemUtil = require("@src/API/Game/ItemUtil")
local Bag = require("@src/API/Game/Bag")
local ItemSlot = require("@src/API/UI/ItemSlot")
local PlayerContainers = require("@src/API/Game/PlayerContainers")
local CursorInfo = require("@src/API/UI/CursorInfo")

type Bag = Bag.Bag

type UISlot =
{
    itemSlot : ItemSlot.UIItemSlot,
    panel : Panel
}

type BagData = 
{
    container : Bag?,
    uiSlots : { [number]: UISlot }
}

type SlotConfigChild =
{
    slot : number,
    type : number
}

type SlotConfigParent =
{
    slot : number,
    type : number,
    position : vector,
    anchorPoint : vector,
    relativePoint : vector,
    children : { [number]: SlotConfigChild }
}

type SlotConfig =
{
    LEFT_SIDE   : SlotConfigParent,
    RIGHT_SIDE  : SlotConfigParent,
    BOTTOM      : SlotConfigParent
}

-- Document the module's purpose
--[[ CharacterEquipment.luau
    Handles the character equipment UI panel and slot management.
    Responsible for:
    - Creating and managing equipment slots
    - Handling item equip/unequip events
    - Validating equipment slot rules
]]

local ItemEquipSlots = ItemUtil.EquipSlot
local ItemEquipTypes = ItemUtil.EquipType
local GetItemInfo = Item.GetItemInfo

-- Constants for UI layout
local UI_CONSTANTS =
{
    PANEL_POS_X = 20,
    PANEL_POS_Y = 350,
    PANEL_WIDTH = 300,
    PANEL_HEIGHT = 450,
    PANEL_LAYER = 0,
    PANEL_TEMPLATE = "DialogBoxRound",
    MAIN_PANEL_TEMPLATE = "CharacterEquipmentMainPanel",
    UNIT_INFO_PANEL_TEMPLATE = "CharacterEquipmentUnitInfoPanel",
    STAT_INFO_PANEL_TEMPLATE = "CharacterEquipmentStatInfoPanel",

    SLOT_SPACING = 5,
    SIDE_MARGIN = 5,
    WEAPON_SLOT_OFFSET = 20
}

type CharacterSortedStat = { kind : number, name : string }

type CharacterInfo = 
{
    infoPanel : Panel,
    nameWidget : TextWidget,

    statsPanel : Panel,
    sortedStatTypes : { [number]: CharacterSortedStat },
    stats : { [number]: CharacterStat },
}
type CharacterStat =
{
    kind : number,
    base : number,
    current : number,

    nameWidget : TextWidget,
    valueWidget : TextWidget
}
local characterInfo : CharacterInfo = 
{
    nameWidget = nil,
    statsPanel = nil,
    sortedStatTypes = {
        { kind = StatType.Armor, name = "Armor" },
        { kind = StatType.Stamina, name = "Stamina" },
        { kind = StatType.Strength, name = "Strength" },
        { kind = StatType.Agility, name = "Agility" },
        { kind = StatType.Intellect, name = "Intellect" },
        { kind = StatType.Spirit, name = "Spirit" },
        { kind = StatType.AttackPower, name = "Attack Power" },
        { kind = StatType.SpellPower, name = "Spell Power" },
    },
    stats = {}
}

-- Add slot configuration table for easier maintenance
local SLOT_CONFIG : SlotConfig =
{
    LEFT_SIDE =
    {
        slot = ItemEquipSlots.Helm,
        type = ItemEquipTypes.Helm,
        position = vector.create(UI_CONSTANTS.SIDE_MARGIN, -(UI_CONSTANTS.SLOT_SPACING * 3)),
        anchorPoint = vector.create(0.0, 1.0),
        relativePoint = vector.create(0.0, 1.0),
        children =
        {
            { slot = ItemEquipSlots.Necklace,   type = ItemEquipTypes.Necklace },
            { slot = ItemEquipSlots.Shoulders,  type = ItemEquipTypes.Shoulders },
            { slot = ItemEquipSlots.Cloak,      type = ItemEquipTypes.Cloak },
            { slot = ItemEquipSlots.Chest,      type = ItemEquipTypes.Chest },
            { slot = ItemEquipSlots.Shirt,      type = ItemEquipTypes.Shirt },
            { slot = ItemEquipSlots.Tabard,     type = ItemEquipTypes.Tabard },
            { slot = ItemEquipSlots.Bracers,    type = ItemEquipTypes.Bracers }
        }
    },

    RIGHT_SIDE =
    {
        slot = ItemEquipSlots.Gloves,
        type = ItemEquipTypes.Gloves,
        position = vector.create(-UI_CONSTANTS.SIDE_MARGIN, -(UI_CONSTANTS.SLOT_SPACING * 3)),
        anchorPoint = vector.create(1.0, 1.0),
        relativePoint = vector.create(1.0, 1.0),
        children =
        {
            { slot = ItemEquipSlots.Belt,       type = ItemEquipTypes.Belt },
            { slot = ItemEquipSlots.Pants,      type = ItemEquipTypes.Pants },
            { slot = ItemEquipSlots.Boots,      type = ItemEquipTypes.Boots },
            { slot = ItemEquipSlots.Ring1,      type = ItemEquipTypes.Ring },
            { slot = ItemEquipSlots.Ring2,      type = ItemEquipTypes.Ring },
            { slot = ItemEquipSlots.Trinket1,   type = ItemEquipTypes.Trinket },
            { slot = ItemEquipSlots.Trinket2,   type = ItemEquipTypes.Trinket }
        }
    },

    BOTTOM =
    {
        slot = ItemEquipSlots.MainHand,
        type = ItemEquipTypes.Weapon,
        position = vector.create(UI_CONSTANTS.SLOT_SPACING + UI_CONSTANTS.WEAPON_SLOT_OFFSET, UI_CONSTANTS.SLOT_SPACING * 3),
        anchorPoint = vector.create(0.0, 0.0),
        relativePoint = vector.create(0.0, 0.0),
        children =
        {
            { slot = ItemEquipSlots.OffHand,    type = ItemEquipTypes.OffHand },
            { slot = ItemEquipSlots.Ranged,     type = ItemEquipTypes.Ranged }
        }
    }
}

local function RegisterTemplates()
    UI.RegisterPanelTemplate(UI_CONSTANTS.MAIN_PANEL_TEMPLATE,
    {
        background = "Data/Texture/interface/framegeneral/ui-background-marble.dds",
        --color = vector.create(100.0/255.0, 100.0/255.0, 100.0/255.0),
        cornerRadius = 5.0
    });

    UI.RegisterPanelTemplate(UI_CONSTANTS.UNIT_INFO_PANEL_TEMPLATE,
    {
        color = vector.create(5.0/255.0, 5.0/255.0, 5.0/255.0),
        cornerRadius = 3.0
    });

    UI.RegisterPanelTemplate(UI_CONSTANTS.STAT_INFO_PANEL_TEMPLATE,
    {
        background = "Data/Texture/interface/dialogframe/ui-dialogbox-background.dds",
        cornerRadius = 3.0
    });

    UI.RegisterTextTemplate("CharacterEquipmentStatsText",
    {
        font = "Data/Fonts/OpenSans-Regular.ttf", -- Obviously we want a different font
        size = 10.0,
        color = vector.create(255.0/255.0, 255.0/255.0, 255.0/255.0),
        borderSize = 1.0,
        borderColor = vector.create(0.0, 0.0, 0.0)
    });
end

local function CreateSlot(parent : Panel, container : Bag, config : SlotConfigChild, position: vector)
    local slot = container.uiSlots[config.slot]
    slot.itemSlot = ItemSlot.Create(parent, container, config.slot, config.type)
    slot.itemSlot:SetPos(position.x, position.y)

    return slot.itemSlot
end

local function CreateLeftSideSlots(mainPanel : Panel, container : Bag)
    local sideConfig = SLOT_CONFIG.LEFT_SIDE
    
    local configParent = container.uiSlots[sideConfig.slot]
    configParent.itemSlot = ItemSlot.Create(mainPanel, container, sideConfig.slot, sideConfig.type)
    configParent.itemSlot:SetPos(sideConfig.position.x, sideConfig.position.y)
    configParent.itemSlot:SetAnchor(sideConfig.anchorPoint.x, sideConfig.anchorPoint.y)
    configParent.itemSlot:SetRelativePoint(sideConfig.relativePoint.x, sideConfig.relativePoint.y)

    for index, child in sideConfig.children do
        local slot = container.uiSlots[child.slot]
        local prevChild = sideConfig.children[index - 1]
        local parent = if index == 1 then configParent else container.uiSlots[prevChild.slot]

        slot.itemSlot = CreateSlot(parent.itemSlot.panel, container, child, vector.create(0.0, -parent.itemSlot.panel:GetHeight() - UI_CONSTANTS.SLOT_SPACING))
    end
end

local function CreateRightSideSlots(mainPanel: Panel, container: Bag)
    local sideConfig = SLOT_CONFIG.RIGHT_SIDE
    
    local configParent = container.uiSlots[sideConfig.slot]
    configParent.itemSlot = ItemSlot.Create(mainPanel, container, sideConfig.slot, sideConfig.type)
    configParent.itemSlot:SetPos(sideConfig.position.x, sideConfig.position.y)
    configParent.itemSlot:SetAnchor(sideConfig.anchorPoint.x, sideConfig.anchorPoint.y)
    configParent.itemSlot:SetRelativePoint(sideConfig.relativePoint.x, sideConfig.relativePoint.y)

    for index, child in sideConfig.children do
        local slot = container.uiSlots[child.slot]
        local prevChild = sideConfig.children[index - 1]
        local parent = if index == 1 then configParent else container.uiSlots[prevChild.slot]

        slot.itemSlot = CreateSlot(parent.itemSlot.panel, container, child, vector.create(0.0, -parent.itemSlot.panel:GetHeight() - UI_CONSTANTS.SLOT_SPACING))
    end
end

local function CreateBottomSlots(mainPanel: Panel, container: Bag)
    local sideConfig = SLOT_CONFIG.BOTTOM
    
    local configParent = container.uiSlots[sideConfig.slot]
    local bracers = container.uiSlots[ItemEquipSlots.Bracers]

    configParent.itemSlot = ItemSlot.Create(mainPanel, container, sideConfig.slot, sideConfig.type)
    configParent.itemSlot:SetPos(sideConfig.position.x + bracers.itemSlot.panel:GetWidth(), sideConfig.position.y)
    configParent.itemSlot:SetAnchor(sideConfig.anchorPoint.x, sideConfig.anchorPoint.y)
    configParent.itemSlot:SetRelativePoint(sideConfig.relativePoint.x, sideConfig.relativePoint.y)

    for index, child in sideConfig.children do
        local slot = container.uiSlots[child.slot]
        local prevChild = sideConfig.children[index - 1]
        local parent = if index == 1 then configParent else container.uiSlots[prevChild.slot]

        slot.itemSlot = CreateSlot(parent.itemSlot.panel, container, child, vector.create(parent.itemSlot.panel:GetWidth() + UI_CONSTANTS.SLOT_SPACING, 0.0))
    end
end

local MIN_CONTAINER_INDEX = 1
local MAX_CONTAINER_INDEX = 6
local MAIN_BAG_SLOT_INDEX = 20
local LAST_BAG_SLOT_INDEX = 24

local function OnBagEvent(container : Bag, eventID : Bag.Event, ... : number)
    assert(container ~= nil, "Bag parameter cannot be nil")
    
    if eventID == Bag.Events.CanFitInSlot then
        local itemID, count, slotIndex = ...
        if count > 1 then
            return false
        end

        local itemTemplate = GetItemInfo(itemID)

        if slotIndex > MAIN_BAG_SLOT_INDEX and slotIndex <= LAST_BAG_SLOT_INDEX then
            local isBag = itemTemplate.Category == ItemUtil.Category.Container
            return isBag
        else
            local itemEquipType = ItemUtil.GetItemEquipType(itemTemplate)
            local requiredEquipType = ItemUtil.GetEquipTypeForEquipSlot(slotIndex)

            if itemEquipType == requiredEquipType then
                return true
            end
    
            if itemEquipType == ItemEquipTypes.Weapon and requiredEquipType == ItemEquipTypes.OffHand then
                return true
            end
    
            return false
        end
    elseif eventID == Bag.Events.AddedItem then
        local itemID, _, slotIndex = ...
        if slotIndex > MAIN_BAG_SLOT_INDEX and slotIndex <= LAST_BAG_SLOT_INDEX then
        else
            EquipItem(itemID, slotIndex)
        end
        return true
    elseif eventID == Bag.Events.RemovedItem then
        local _, _, slotIndex = ...
        
        if slotIndex > MAIN_BAG_SLOT_INDEX and slotIndex <= LAST_BAG_SLOT_INDEX then
        else
            UnEquipItem(slotIndex)
        end
        return true
    elseif eventID == Bag.Events.UseItem then
        local _, slotIndex = ...
        
        if slotIndex >= MAIN_BAG_SLOT_INDEX and slotIndex <= LAST_BAG_SLOT_INDEX then
            local bagItemID = container:GetItemID(slotIndex)
            if bagItemID == 0 then
                return false
            end

            local bagIndex = (slotIndex - MAIN_BAG_SLOT_INDEX) + 1
            local bag = PlayerContainers.GetBag(bagIndex)
            if bag == nil then
                return false
            end

            local uiBagSlot = container.uiSlots[slotIndex]
            if bag.panel:IsVisible() then
                uiBagSlot.itemSlot.panel:SetForeground("")
                bag.panel:SetVisible(false)
            else
                uiBagSlot.itemSlot.panel:SetForeground("Data/Texture/interface/fullscreentextures/outofcontrol.dds")
                bag.panel:SetVisible(true)
            end
        end
    end

    return false
end

local function CreateBagSlots(mainPanel: Panel, container: Bag)
    local bagSlotsPanel = mainPanel:NewPanel(0.0, -60.0, 255.0, 55.0, UI_CONSTANTS.PANEL_LAYER, UI_CONSTANTS.PANEL_TEMPLATE)
    container.bagSlotsPanel = bagSlotsPanel
    container.bagSlotsPanel:SetEnabled(true)
    container.bagSlotsPanel:SetAnchor(0.0, 0.0);
    container.bagSlotsPanel:SetRelativePoint(0.0, 0.0);

    local mainBagSlot = container.uiSlots[MAIN_BAG_SLOT_INDEX]
    mainBagSlot.itemSlot = ItemSlot.Create(bagSlotsPanel, container, MAIN_BAG_SLOT_INDEX, ItemEquipTypes.Bag)
    mainBagSlot.itemSlot:SetPos(2.5, 0.0)
    mainBagSlot.itemSlot.panel:SetAnchor(0.0, 0.5);
    mainBagSlot.itemSlot.panel:SetRelativePoint(0.0, 0.5);

    for i = 1, 4 do
        local bagSlotIndex = MAIN_BAG_SLOT_INDEX + i
        local parentSlot = container.uiSlots[bagSlotIndex - 1]
        local slot = container.uiSlots[bagSlotIndex]
        slot.itemSlot = ItemSlot.Create(parentSlot.itemSlot.panel, container, bagSlotIndex, ItemEquipTypes.Bag)
        slot.itemSlot:SetPos(slot.itemSlot.panel:GetWidth() + 2.5, 0.0)
        
        slot.itemSlot.panel:SetAnchor(0.0, 0.5);
        slot.itemSlot.panel:SetRelativePoint(0.0, 0.5);
    end
end

local function CreateUnitInfoPanel(mainPanel : Panel, unitID : number)
    characterInfo.infoPanel = mainPanel:NewPanel(74.0, -14.0, 152.0, 25.0, UI_CONSTANTS.PANEL_LAYER, UI_CONSTANTS.UNIT_INFO_PANEL_TEMPLATE)
    characterInfo.infoPanel:SetEnabled(true)
    characterInfo.infoPanel:SetAlpha(0.75)
    characterInfo.infoPanel:SetAnchor(0.0, 1.0);
    characterInfo.infoPanel:SetRelativePoint(0.0, 1.0);

    local name = `{Unit.GetName(unitID)} {60} Warrior` -- TODO: Replace with actual name when available
    characterInfo.nameWidget = characterInfo.infoPanel:NewText(name, 0, 2, 1, "CharacterEquipmentStatsText")
    characterInfo.nameWidget:SetAnchor(0.5, 0.5)
    characterInfo.nameWidget:SetRelativePoint(0.5, 0.5)
end

local function RefreshStatsPanel()
    local posY = -1;

    for i, stat in characterInfo.sortedStatTypes do
        local statData = characterInfo.stats[stat.kind]
        if statData == nil then
            continue
        end

        if statData.base == 0 and statData.current == 0 then
            if statData.nameWidget ~= nil then
                statData.nameWidget:SetEnabled(false)
            end

            if statData.valueWidget ~= nil then
                statData.valueWidget:SetEnabled(false)
            end

            continue
        end

        if statData.nameWidget == nil then
            statData.nameWidget = characterInfo.statsPanel:NewText(stat.name, 4, posY, 1, "CharacterEquipmentStatsText")
            statData.nameWidget:SetAnchor(0.0, 1.0)
            statData.nameWidget:SetRelativePoint(0.0, 1.0)
        else
            statData.nameWidget:SetPos(4, posY)
            statData.nameWidget:SetText(stat.name)
            statData.nameWidget:SetEnabled(true)
        end

        local statValueText = if statData.current > 0 then "+" else "";
        statValueText ..= tostring(statData.current)

        if statData.valueWidget == nil then
            statData.valueWidget = characterInfo.statsPanel:NewText(statValueText, -2, posY, 1, "CharacterEquipmentStatsText")
            statData.valueWidget:SetAnchor(1.0, 1.0)
            statData.valueWidget:SetRelativePoint(1.0, 1.0)
        else
            statData.valueWidget:SetPos(-2, posY)
            statData.valueWidget:SetText(statValueText)
            statData.valueWidget:SetEnabled(true)
        end

        if (statData.current > statData.base) then
            statData.valueWidget:SetColor(vector.create(0.0, 1.0, 0.0))
        elseif (statData.current < statData.base) then
            statData.valueWidget:SetColor(vector.create(1.0, 0.0, 0.0))
        else
            statData.valueWidget:SetColor(vector.create(1.0, 1.0, 1.0))
        end
        
        posY = posY - 12
    end
end

local function AddOrUpdateStat(statType : number, baseValue : number, currentValue : number)
    local stat = characterInfo.stats[statType]
    if stat == nil then
        stat = {
            kind = statType,
            base = baseValue,
            current = currentValue,
            nameWidget = nil,
            valueWidget = nil
        }
        characterInfo.stats[statType] = stat
    else
        stat.base = baseValue
        stat.current = currentValue
    end
end

local function CreateStatsPanel(mainPanel : Panel, unitID : number)
    characterInfo.statsPanel = mainPanel:NewPanel(74.0, 72.0, 152.0, 100.0, UI_CONSTANTS.PANEL_LAYER, UI_CONSTANTS.STAT_INFO_PANEL_TEMPLATE)
    characterInfo.statsPanel:SetEnabled(true)

    -- Get Stats from Unit
    for i, stat in characterInfo.sortedStatTypes do
        local current, base = Unit.GetStat(unitID, stat.kind)
        AddOrUpdateStat(stat.kind, base, current)
    end

    RefreshStatsPanel()
end

local function CreateEquipmentBag(numSlots : number, containerItems : ContainerItemEventData, isBaseContainer : boolean)
    local canvas = UI.GetCanvas("CharacterEquipment", 0, 0, 1920, 1080); -- TODO: We don't want to hardcode the reference resolution here

    local equipmentBag = Bag.new(nil, numSlots, nil, function(container : Bag)
        local mainPanel = canvas:NewPanel(UI_CONSTANTS.PANEL_POS_X, UI_CONSTANTS.PANEL_POS_Y, UI_CONSTANTS.PANEL_WIDTH, UI_CONSTANTS.PANEL_HEIGHT, UI_CONSTANTS.PANEL_LAYER, UI_CONSTANTS.MAIN_PANEL_TEMPLATE)
        container.panel = mainPanel

        container.panel:SetAnchor(0.0, 0.0);
        container.panel:SetRelativePoint(0.0, 0.0);

        container.containerIndex = MIN_CONTAINER_INDEX

        local unitID = Unit.GetLocal()
        CreateLeftSideSlots(mainPanel, container)
        CreateRightSideSlots(mainPanel, container)
        CreateBottomSlots(mainPanel, container)
        CreateBagSlots(mainPanel, container)

        if (isBaseContainer) then
            CreateUnitInfoPanel(mainPanel, unitID)
            CreateStatsPanel(mainPanel, unitID)
        end
    end, OnBagEvent)

    if containerItems ~= nil then
        for k, item in containerItems do
            equipmentBag:AddToSlot(item.itemID, item.count, item.slot, true)
        end
    end

    equipmentBag:LockItem(MAIN_BAG_SLOT_INDEX, true)
    PlayerContainers.SetEquipmentBag(equipmentBag)
end

local function OnLocalMoverChanged(eventID : number, data : any)
    RegisterTemplates()

    local numSlots, baseContainerItems = Container.GetContainerItems(MIN_CONTAINER_INDEX)
    if (numSlots == nil or baseContainerItems == nil) then
        return
    end

    CreateEquipmentBag(numSlots, baseContainerItems, true)

    for i = MAIN_BAG_SLOT_INDEX, LAST_BAG_SLOT_INDEX do
        local containerItem = baseContainerItems[i]
        if (containerItem == nil) then
            continue
        end

        local containerIndex = (i - MAIN_BAG_SLOT_INDEX) + 2
        local bag = Bag.new(nil, containerItem.durability)
        bag.containerIndex = containerIndex
        bag.panel:SetVisible(false)

        local _, containerItems = Container.GetContainerItems(bag.containerIndex)
        for k, item in containerItems do
            bag:AddToSlot(item.itemID, item.count, item.slot, true)
        end

        PlayerContainers.SetBag(bag.containerIndex - 1, bag)
    end
end
RegisterEvent(GameEvent.LocalMoverChanged, OnLocalMoverChanged)

local function OnStatUpdate(eventID : number, data : any)
    local unitID = data.unitID
    if unitID ~= Unit.GetLocal() then
        return
    end

    AddOrUpdateStat(data.statType, data.base, data.current)
    
    if characterInfo.statsPanel ~= nil then
    RefreshStatsPanel()
    end
end
RegisterEvent(UnitEvent.StatUpdate, OnStatUpdate)

local function OnContainerCreate(eventID : number, data : any)
    if data.index < MIN_CONTAINER_INDEX or data.index > MAX_CONTAINER_INDEX then
        return
    end
    -- TODO : Check if we are overriding an existing container, in which case we need to clean it up

    if data.index == MIN_CONTAINER_INDEX then
        local equipmentBag = PlayerContainers.GetEquipmentBag()
        if (equipmentBag == nil) then
            CreateEquipmentBag(data.numSlots, nil, false)
        else
            for i = 1, equipmentBag.totalSlots do
                equipmentBag:ClearSlot(i)
            end
        end
    else
        local bag = PlayerContainers.GetBag(data.index - 1)
        if (bag == nil) then
            bag = Bag.new(nil, data.numSlots)
            bag.containerIndex = data.index
            bag.panel:SetVisible(false)

            PlayerContainers.SetBag(bag.containerIndex - 1, bag)
        else
            for i = 1, bag.totalSlots do
                bag:ClearSlot(i)
            end
        end
    end
end
RegisterEvent(ContainerEvent.Add, OnContainerCreate)

local function OnContainerAddToSlot(eventID : number, data : any)
    if data.containerIndex < MIN_CONTAINER_INDEX or data.containerIndex > MAX_CONTAINER_INDEX then
        return
    end

    local container = if data.containerIndex == MIN_CONTAINER_INDEX then PlayerContainers.GetEquipmentBag() else PlayerContainers.GetBag(data.containerIndex - 1)
    container:AddToSlot(data.itemID, data.count, data.slotIndex)
end
RegisterEvent(ContainerEvent.AddToSlot, OnContainerAddToSlot)

local function OnContainerRemoveFromSlot(eventID : number, data : any)
    if data.containerIndex < MIN_CONTAINER_INDEX or data.containerIndex > MAX_CONTAINER_INDEX then
        return
    end

    local container = if data.containerIndex == MIN_CONTAINER_INDEX then PlayerContainers.GetEquipmentBag() else PlayerContainers.GetBag(data.containerIndex - 1)
    container:AddToSlot(0, -1, data.slotIndex)
end
RegisterEvent(ContainerEvent.RemoveFromSlot, OnContainerRemoveFromSlot)

local function OnContainerSwapSlots(eventID : number, data : any)
    if data.srcContainerIndex < MIN_CONTAINER_INDEX or data.srcContainerIndex > MAX_CONTAINER_INDEX then
        return
    end

    if data.destContainerIndex < MIN_CONTAINER_INDEX or data.destContainerIndex > MAX_CONTAINER_INDEX then
        return
    end

    local srcContainer = if data.srcContainerIndex == MIN_CONTAINER_INDEX then PlayerContainers.GetEquipmentBag() else PlayerContainers.GetBag(data.srcContainerIndex - 1)
    local destContainer = if data.destContainerIndex == MIN_CONTAINER_INDEX then PlayerContainers.GetEquipmentBag() else PlayerContainers.GetBag(data.destContainerIndex - 1)

    srcContainer:UnlockItem(data.srcSlotIndex)
    destContainer:UnlockItem(data.destSlotIndex)
    srcContainer:SwapSlots(destContainer, data.srcSlotIndex, data.destSlotIndex)
end
RegisterEvent(ContainerEvent.SwapSlots, OnContainerSwapSlots)