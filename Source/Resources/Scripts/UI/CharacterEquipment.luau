local ItemUtil = require("@src/API/Game/ItemUtil")
local Bag = require("@src/API/Game/UI/Bag")
local ItemSlot = require("@src/API/Game/UI/ItemSlot")
local PlayerContainers = require("@src/API/Game/PlayerContainers")
local FrameManager = require("@src/API/Game/FrameManager")
local Backpack = require("@src/API/Game/UI/Backpack")
local InputAPI = require("@src/API/Input/Input")

type Bag = Bag.Bag

type SlotConfigChild =
{
    slot : integer,
    type : integer
}

type SlotConfigParent =
{
    slot : integer,
    type : integer,
    position : vec,
    anchorPoint : vec,
    relativePoint : vec,
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
local CLASS_ID_TO_NAME =
{
    [1] = "Warrior",
    [2] = "Paladin",
    [3] = "Hunter",
    [4] = "Rogue",
    [5] = "Priest",
    [7] = "Shaman",
    [8] = "Mage",
    [9] = "Warlock",
    [11] = "Druid",
}

-- Constants for UI layout
local UI_CONSTANTS =
{
    PANEL_POS_X = 20,
    PANEL_POS_Y = 350,
    PANEL_WIDTH = 300,
    PANEL_HEIGHT = 450,
    PANEL_LAYER = 0i,
    MAIN_PANEL_TEMPLATE = "DialogBox",
    UNIT_INFO_PANEL_TEMPLATE = "CharacterEquipmentUnitInfoPanel",
    STAT_INFO_PANEL_TEMPLATE = "CharacterEquipmentStatInfoPanel",

    SLOT_SPACING = 5,
    SIDE_MARGIN = 5,
    WEAPON_SLOT_OFFSET = 20
}

type CharacterSortedStat = { kind : integer, name : string }

type CharacterInfo = 
{
    infoPanel : Panel,
    nameWidget : TextWidget,

    statsPanel : Panel,
    sortedStatTypes : { [number]: CharacterSortedStat },
    stats : { [integer]: CharacterStat },
}
type CharacterStat =
{
    kind : integer,
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
        { kind = StatTypeEnum.Armor, name = "Armor" },
        { kind = StatTypeEnum.Stamina, name = "Stamina" },
        { kind = StatTypeEnum.Strength, name = "Strength" },
        { kind = StatTypeEnum.Agility, name = "Agility" },
        { kind = StatTypeEnum.Intellect, name = "Intellect" },
        { kind = StatTypeEnum.Spirit, name = "Spirit" },
        { kind = StatTypeEnum.AttackPower, name = "Attack Power" },
        { kind = StatTypeEnum.SpellPower, name = "Spell Power" },
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
        position = vec.New(UI_CONSTANTS.SIDE_MARGIN, -(UI_CONSTANTS.SLOT_SPACING * 3)),
        anchorPoint = vec.New(0.0, 1.0),
        relativePoint = vec.New(0.0, 1.0),
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
        position = vec.New(-UI_CONSTANTS.SIDE_MARGIN, -(UI_CONSTANTS.SLOT_SPACING * 3)),
        anchorPoint = vec.New(1.0, 1.0),
        relativePoint = vec.New(1.0, 1.0),
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
        position = vec.New(UI_CONSTANTS.SLOT_SPACING + UI_CONSTANTS.WEAPON_SLOT_OFFSET, UI_CONSTANTS.SLOT_SPACING * 3),
        anchorPoint = vec.New(0.0, 0.0),
        relativePoint = vec.New(0.0, 0.0),
        children =
        {
            { slot = ItemEquipSlots.OffHand,    type = ItemEquipTypes.OffHand },
            { slot = ItemEquipSlots.Ranged,     type = ItemEquipTypes.Ranged }
        }
    }
}

local function RegisterTemplates()
    UI.RegisterPanelTemplate(UI_CONSTANTS.UNIT_INFO_PANEL_TEMPLATE,
    {
        color = vec.New(5.0/255.0, 5.0/255.0, 5.0/255.0),
        cornerRadius = 3.0
    });

    UI.RegisterPanelTemplate(UI_CONSTANTS.STAT_INFO_PANEL_TEMPLATE,
    {
        color = vec.New(0.03, 0.035, 0.045),
        alpha = 0.94,
        cornerRadius = 3.0,
        borderColor = vec.New(0.18, 0.21, 0.26),
        borderSize = 1.0,
    });

    UI.RegisterTextTemplate("CharacterEquipmentStatsText",
    {
        font = "Data/Fonts/OpenSans-Regular.ttf", -- Obviously we want a different font
        size = 10.0,
        color = vec.New(255.0/255.0, 255.0/255.0, 255.0/255.0),
        borderSize = 0.2,
        borderColor = vec.New(0.0, 0.0, 0.0)
    });
end

local function CreateSlot(parent : Panel, container : Bag, config : SlotConfigChild, position: vec)
    local slotIndex = integer.tonumber(config.slot)
    local slot = container.uiSlots[slotIndex]
    slot.itemSlot = ItemSlot.New(parent, container, slotIndex, config.type)
    slot.itemSlot:SetPos(position)

    return slot.itemSlot
end

local function CreateLeftSideSlots(mainPanel : Panel, container : Bag)
    local sideConfig = SLOT_CONFIG.LEFT_SIDE
    
    local configParentSlot = integer.tonumber(sideConfig.slot)
    local configParent = container.uiSlots[configParentSlot]
    configParent.itemSlot = ItemSlot.New(mainPanel, container, configParentSlot, sideConfig.type)
    configParent.itemSlot:SetPos(sideConfig.position)
    configParent.itemSlot:SetAnchor(vec.New(sideConfig.anchorPoint.x, sideConfig.anchorPoint.y))
    configParent.itemSlot:SetRelativePoint(vec.New(sideConfig.relativePoint.x, sideConfig.relativePoint.y))

    for index, child in sideConfig.children do
        local slot = container.uiSlots[integer.tonumber(child.slot)]
        local prevChild = sideConfig.children[index - 1]
        local parent = if index == 1 then configParent else container.uiSlots[integer.tonumber(prevChild.slot)]

        slot.itemSlot = CreateSlot(parent.itemSlot.root, container, child, vec.New(0.0, -parent.itemSlot.root:GetHeight() - UI_CONSTANTS.SLOT_SPACING))
    end
end

local function CreateRightSideSlots(mainPanel: Panel, container: Bag)
    local sideConfig = SLOT_CONFIG.RIGHT_SIDE
    
    local configParentSlot = integer.tonumber(sideConfig.slot)
    local configParent = container.uiSlots[configParentSlot]
    configParent.itemSlot = ItemSlot.New(mainPanel, container, configParentSlot, sideConfig.type)
    configParent.itemSlot:SetPos(sideConfig.position)
    configParent.itemSlot:SetAnchor(vec.New(sideConfig.anchorPoint.x, sideConfig.anchorPoint.y))
    configParent.itemSlot:SetRelativePoint(vec.New(sideConfig.relativePoint.x, sideConfig.relativePoint.y))

    for index, child in sideConfig.children do
        local slot = container.uiSlots[integer.tonumber(child.slot)]
        local prevChild = sideConfig.children[index - 1]
        local parent = if index == 1 then configParent else container.uiSlots[integer.tonumber(prevChild.slot)]

        slot.itemSlot = CreateSlot(parent.itemSlot.root, container, child, vec.New(0.0, -parent.itemSlot.root:GetHeight() - UI_CONSTANTS.SLOT_SPACING))
    end
end

local function CreateBottomSlots(mainPanel: Panel, container: Bag)
    local sideConfig = SLOT_CONFIG.BOTTOM
    
    local configParentSlot = integer.tonumber(sideConfig.slot)
    local configParent = container.uiSlots[configParentSlot]
    local bracers = container.uiSlots[integer.tonumber(ItemEquipSlots.Bracers)]

    configParent.itemSlot = ItemSlot.New(mainPanel, container, configParentSlot, sideConfig.type)
    configParent.itemSlot:SetPos(vec.New(sideConfig.position.x + bracers.itemSlot.root:GetWidth(), sideConfig.position.y))
    configParent.itemSlot:SetAnchor(vec.New(sideConfig.anchorPoint.x, sideConfig.anchorPoint.y))
    configParent.itemSlot:SetRelativePoint(vec.New(sideConfig.relativePoint.x, sideConfig.relativePoint.y))

    for index, child in sideConfig.children do
        local slot = container.uiSlots[integer.tonumber(child.slot)]
        local prevChild = sideConfig.children[index - 1]
        local parent = if index == 1 then configParent else container.uiSlots[integer.tonumber(prevChild.slot)]

        slot.itemSlot = CreateSlot(parent.itemSlot.root, container, child, vec.New(parent.itemSlot.root:GetWidth() + UI_CONSTANTS.SLOT_SPACING, 0.0))
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

        if slotIndex >= MAIN_BAG_SLOT_INDEX and slotIndex <= LAST_BAG_SLOT_INDEX then
            local isBag = itemTemplate.Category == ItemUtil.Category.Container
            return isBag
        else
            local itemEquipType = ItemUtil.GetItemEquipType(itemTemplate)
            local requiredEquipType = ItemUtil.GetEquipTypeForEquipSlot(integer.create(slotIndex))

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
        if slotIndex >= MAIN_BAG_SLOT_INDEX and slotIndex <= LAST_BAG_SLOT_INDEX then
            Backpack.Refresh()
        else
            EquipItem(itemID, integer.create(slotIndex))
        end
        return true
    elseif eventID == Bag.Events.RemovedItem then
        local _, _, slotIndex = ...

        if slotIndex >= MAIN_BAG_SLOT_INDEX and slotIndex <= LAST_BAG_SLOT_INDEX then
            Backpack.Refresh()
        else
            UnEquipItem(integer.create(slotIndex))
        end
        return true
    elseif eventID == Bag.Events.UseItem then
        local _, slotIndex = ...

        if slotIndex >= MAIN_BAG_SLOT_INDEX and slotIndex <= LAST_BAG_SLOT_INDEX then
            local bagItemID = container:GetItemID(slotIndex)
            if bagItemID == 0i then
                return false
            end

            return Backpack.Toggle()
        end
    end

    return false
end

local function SetUnitInfo(unitID : integer)
    if characterInfo.nameWidget == nil then
        return
    end

    local unitName = Unit.GetName(unitID) or "Unknown"
    local unitLevel = Unit.GetLevel(unitID)
    local className = CLASS_ID_TO_NAME[integer.tonumber(Unit.GetClass(unitID))] or "Unknown"
    local unitInfoText = `{unitName} {unitLevel} {className}`
    characterInfo.nameWidget:SetText(unitInfoText)
end

local function CreateUnitInfoPanel(mainPanel : Panel, unitID : integer)
    characterInfo.infoPanel = mainPanel:NewPanel(vec.New(74.0, -14.0), vec.New(152.0, 25.0), UI_CONSTANTS.PANEL_LAYER, UI_CONSTANTS.UNIT_INFO_PANEL_TEMPLATE)
    characterInfo.infoPanel:SetEnabled(true)
    characterInfo.infoPanel:SetAlpha(0.75)
    characterInfo.infoPanel:SetAnchor(vec.New(0.0, 1.0));
    characterInfo.infoPanel:SetRelativePoint(vec.New(0.0, 1.0));

    characterInfo.nameWidget = characterInfo.infoPanel:NewText("", vec.New(0, 0), 1i, "CharacterEquipmentStatsText")
    characterInfo.nameWidget:SetAnchor(vec.New(0.5, 0.5))
    characterInfo.nameWidget:SetRelativePoint(vec.New(0.5, 0.5))

    SetUnitInfo(unitID)
end

local function RefreshStatsPanel()
    local posY = -4;

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
            statData.nameWidget = characterInfo.statsPanel:NewText(stat.name, vec.New(8, posY), 1i, "CharacterEquipmentStatsText")
            statData.nameWidget:SetAnchor(vec.New(0.0, 1.0))
            statData.nameWidget:SetRelativePoint(vec.New(0.0, 1.0))
        else
            statData.nameWidget:SetPos(vec.New(8, posY))
            statData.nameWidget:SetText(stat.name)
            statData.nameWidget:SetEnabled(true)
        end

        local statValueText = if statData.current > 0 then "+" else "";
        statValueText ..= tostring(statData.current)

        if statData.valueWidget == nil then
            statData.valueWidget = characterInfo.statsPanel:NewText(statValueText, vec.New(-8, posY), 1i, "CharacterEquipmentStatsText")
            statData.valueWidget:SetAnchor(vec.New(1.0, 1.0))
            statData.valueWidget:SetRelativePoint(vec.New(1.0, 1.0))
        else
            statData.valueWidget:SetPos(vec.New(-8, posY))
            statData.valueWidget:SetText(statValueText)
            statData.valueWidget:SetEnabled(true)
        end

        if (statData.current > statData.base) then
            statData.valueWidget:SetColor(vec.New(0.0, 1.0, 0.0))
        elseif (statData.current < statData.base) then
            statData.valueWidget:SetColor(vec.New(1.0, 0.0, 0.0))
        else
            statData.valueWidget:SetColor(vec.New(1.0, 1.0, 1.0))
        end
        
        posY = posY - 12
    end
end

local function AddOrUpdateStat(statType : integer, baseValue : number, currentValue : number)
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

local function CreateStatsPanel(mainPanel : Panel, unitID : integer)
    characterInfo.statsPanel = mainPanel:NewPanel(vec.New(74.0, 72.0), vec.New(152.0, 100.0), UI_CONSTANTS.PANEL_LAYER, UI_CONSTANTS.STAT_INFO_PANEL_TEMPLATE)
    characterInfo.statsPanel:SetEnabled(true)

    -- Get Stats from Unit
    for i, stat in characterInfo.sortedStatTypes do
        local current, base = Unit.GetStat(unitID, stat.kind)
        AddOrUpdateStat(stat.kind, base, current)
    end

    RefreshStatsPanel()
end

local function CreateEquipmentBag(numSlots : number, containerItems : { ContainerItemInfo }?, isBaseContainer : boolean)
    local canvas = UI.GetCanvas("CharacterEquipment", vec.New(0, 0), vec.New(1920, 1080)); -- TODO: We don't want to hardcode the reference resolution here

    local equipmentBag = Bag.New(nil, numSlots, nil, function(container : Bag)
        local mainPanel = canvas:NewPanel(vec.New(UI_CONSTANTS.PANEL_POS_X, UI_CONSTANTS.PANEL_POS_Y), vec.New(UI_CONSTANTS.PANEL_WIDTH, UI_CONSTANTS.PANEL_HEIGHT), UI_CONSTANTS.PANEL_LAYER, UI_CONSTANTS.MAIN_PANEL_TEMPLATE)
        container.root = mainPanel

        container.root:SetAlpha(1.0);
        container.root:SetAnchor(vec.New(0.0, 0.0));
        container.root:SetRelativePoint(vec.New(0.0, 0.0));
        container.root:SetVisible(false);
        FrameManager.Register("CharacterPanel", {
            root = container.root,
            role = "ManagedPanel",
            onOpen = function()
                container.root:SetEnabled(true);
                container.root:SetVisible(true);
            end,
            onClose = function()
                container.root:SetVisible(false);
                container.root:SetEnabled(false);
            end,
        });

        container.containerIndex = integer.create(MIN_CONTAINER_INDEX)

        local unitID = Unit.GetLocal()
        CreateLeftSideSlots(mainPanel, container)
        CreateRightSideSlots(mainPanel, container)
        CreateBottomSlots(mainPanel, container)
        Backpack.AttachEquipmentContainer(container)

        if (isBaseContainer) then
            CreateUnitInfoPanel(mainPanel, unitID)
            CreateStatsPanel(mainPanel, unitID)
        end
    end, OnBagEvent)

    if containerItems ~= nil then
        for k, item in containerItems do
            equipmentBag:AddToSlot(item.itemID, integer.tonumber(item.count), integer.tonumber(item.slot), true)
        end
    end

    equipmentBag:LockItem(MAIN_BAG_SLOT_INDEX, true)
    PlayerContainers.SetEquipmentBag(equipmentBag)
end

local function OnGameLoaded(eventID : integer, data : any)
    RegisterTemplates()

    if (not IsInWorld()) then
        return
    end
    
    local numSlots, baseContainerItems = Container.GetContainerItems(integer.create(MIN_CONTAINER_INDEX))
    if (numSlots == nil or baseContainerItems == nil) then
        return
    end

    CreateEquipmentBag(integer.tonumber(numSlots), baseContainerItems, true)

    for i = MAIN_BAG_SLOT_INDEX, LAST_BAG_SLOT_INDEX do
        local containerItem = baseContainerItems[i]
        if (containerItem == nil) then
            continue
        end

        local containerIndex = (i - MAIN_BAG_SLOT_INDEX) + 2
        local bag = Bag.New(nil, integer.tonumber(containerItem.durability), nil, Backpack.AttachStorageContainer)
        bag.containerIndex = integer.create(containerIndex)

        local _, containerItems = Container.GetContainerItems(bag.containerIndex)
        for k, item in containerItems do
            bag:AddToSlot(item.itemID, integer.tonumber(item.count), integer.tonumber(item.slot), true)
        end

        PlayerContainers.SetBag(containerIndex - 1, bag)
    end
end
RegisterEvent(GameEvent.Loaded, OnGameLoaded)

local function ToggleCharacterPanel(phase : integer) : integer
    if phase ~= InputPhase.Pressed or FrameManager.Get("CharacterPanel") == nil then
        return InputReply.Ignored;
    end

    return if FrameManager.Toggle("CharacterPanel") then InputReply.Consumed else InputReply.Ignored;
end

InputAPI.Bind("ToggleCharacterPanel", "Toggle Character Panel", "Interface",
    InputAPI.Keyboard(InputAPI.KeyValues.C, InputModifier.Control, InputModifierMatch.Exact),
    ToggleCharacterPanel,
    { defaultReply = InputReply.Ignored });

local function OnLocalMoverChanged(eventID : integer, data : any)
    local unitID = Unit.GetLocal()
    if unitID == INVALID_UNIT_ID then
        return
    end

    SetUnitInfo(unitID)
end
RegisterEvent(GameEvent.LocalMoverChanged, OnLocalMoverChanged)

local function OnStatUpdate(eventID : integer, data : any)
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

local function OnContainerCreate(eventID : integer, data : any)
    local containerIndex = integer.tonumber(data.index)
    local numSlots = integer.tonumber(data.numSlots)
    if containerIndex < MIN_CONTAINER_INDEX or containerIndex > MAX_CONTAINER_INDEX then
        return
    end
    if containerIndex == MIN_CONTAINER_INDEX then
        local equipmentBag = PlayerContainers.GetEquipmentBag()
        if (equipmentBag == nil) then
            CreateEquipmentBag(numSlots, nil, true)
        else
            for i = 1, equipmentBag.totalSlots do
                equipmentBag:ClearSlot(i)
            end

            -- TODO : Clear Stats?
        end
    else
        local bag = PlayerContainers.GetBag(containerIndex - 1)
        if bag == nil or bag.totalSlots ~= numSlots then
            bag = Bag.New(nil, numSlots, nil, Backpack.AttachStorageContainer)
            bag.containerIndex = data.index

            PlayerContainers.SetBag(containerIndex - 1, bag)
        else
            for i = 1, bag.totalSlots do
                bag:ClearSlot(i)
            end
        end
    end
end
RegisterEvent(ContainerEvent.Add, OnContainerCreate)

local function OnContainerAddToSlot(eventID : integer, data : any)
    local containerIndex = integer.tonumber(data.containerIndex)
    if containerIndex < MIN_CONTAINER_INDEX or containerIndex > MAX_CONTAINER_INDEX then
        return
    end

    local container = if containerIndex == MIN_CONTAINER_INDEX then PlayerContainers.GetEquipmentBag() else PlayerContainers.GetBag(containerIndex - 1)
    container:AddToSlot(data.itemID, integer.tonumber(data.count), integer.tonumber(data.slotIndex))
end
RegisterEvent(ContainerEvent.AddToSlot, OnContainerAddToSlot)

local function OnContainerRemoveFromSlot(eventID : integer, data : any)
    local containerIndex = integer.tonumber(data.containerIndex)
    if containerIndex < MIN_CONTAINER_INDEX or containerIndex > MAX_CONTAINER_INDEX then
        return
    end

    local container = if containerIndex == MIN_CONTAINER_INDEX then PlayerContainers.GetEquipmentBag() else PlayerContainers.GetBag(containerIndex - 1)
    container:AddToSlot(0i, -1, integer.tonumber(data.slotIndex))
end
RegisterEvent(ContainerEvent.RemoveFromSlot, OnContainerRemoveFromSlot)

local function OnContainerSwapSlots(eventID : integer, data : any)
    local srcContainerIndex = integer.tonumber(data.srcContainerIndex)
    local destContainerIndex = integer.tonumber(data.destContainerIndex)
    if srcContainerIndex < MIN_CONTAINER_INDEX or srcContainerIndex > MAX_CONTAINER_INDEX then
        return
    end

    if destContainerIndex < MIN_CONTAINER_INDEX or destContainerIndex > MAX_CONTAINER_INDEX then
        return
    end

    local srcContainer = if srcContainerIndex == MIN_CONTAINER_INDEX then PlayerContainers.GetEquipmentBag() else PlayerContainers.GetBag(srcContainerIndex - 1)
    local destContainer = if destContainerIndex == MIN_CONTAINER_INDEX then PlayerContainers.GetEquipmentBag() else PlayerContainers.GetBag(destContainerIndex - 1)
    local srcSlotIndex = integer.tonumber(data.srcSlotIndex)
    local destSlotIndex = integer.tonumber(data.destSlotIndex)

    srcContainer:UnlockItem(srcSlotIndex)
    destContainer:UnlockItem(destSlotIndex)
    srcContainer:SwapSlots(destContainer, srcSlotIndex, destSlotIndex)
end
RegisterEvent(ContainerEvent.SwapSlots, OnContainerSwapSlots)
