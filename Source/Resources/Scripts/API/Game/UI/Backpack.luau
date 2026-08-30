local Bag = require("@src/API/Game/UI/Bag");
local ItemSlot = require("@src/API/Game/UI/ItemSlot");
local ItemUtil = require("@src/API/Game/ItemUtil");
local PlayerContainers = require("@src/API/Game/PlayerContainers");
local FrameManager = require("@src/API/Game/FrameManager");
local InputAPI = require("@src/API/Input/Input");

export type BackpackConfig =
{
    slotSize : number,
    slotSpacing : number,
    panelPadding : number,
    columns : number,
    headerHeight : number,

    bagIconSize : number,
    bagIconSpacing : number,
    bagBarPadding : number,
    bagBarBottomOffset : number,

    panelGap : number,
    screenMargin : number,
    maxBagCount : number,
}

export type BagContainer = Bag.Bag;

export type BackpackAPI =
{
    Config : BackpackConfig,
    Initialize : () -> (),
    AttachEquipmentContainer : (container : BagContainer) -> (),
    AttachStorageContainer : (container : BagContainer) -> (),
    Refresh : () -> (),
    Toggle : () -> boolean,
}

-- These values are intentionally centralized. They are read when the UI is
-- constructed, so changing them currently requires a script reload.
local CONFIG : BackpackConfig =
{
    slotSize = 40,
    slotSpacing = 0,
    panelPadding = 5,
    columns = 10,
    headerHeight = 24,

    bagIconSize = 40,
    bagIconSpacing = 5,
    bagBarPadding = 5,
    bagBarBottomOffset = 74,

    panelGap = 8,
    screenMargin = 24,
    maxBagCount = 5,
}

local MAIN_BAG_SLOT_INDEX = 20;
local FRAME_NAME = "Backpack";
local PANEL_TEMPLATE = "BackpackPanel";

UI.RegisterPanelTemplate(PANEL_TEMPLATE,
{
    color = vec.New(0.055, 0.065, 0.078),
    alpha = 0.98,
    cornerRadius = 3.0,
    borderColor = vec.New(0.20, 0.23, 0.28),
    borderSize = 1.0,
});

local backpackAPI : BackpackAPI = { Config = CONFIG } :: any;
local storagePanel : Panel? = nil;
local headerPanel : Panel? = nil;
local bagBarPanel : Panel? = nil;
local equipmentContainer : BagContainer? = nil;
local attachedStorageContainers : { [BagContainer] : boolean } = { };
local initialized = false;
local displayedSlotCount = 0;

local function ClampStoragePosition(panel : Panel, position : vec) : vec
    return vec.New(
        math.clamp(position.x, math.min(-1920 + panel:GetWidth(), 0), 0),
        math.clamp(position.y, 0, math.max(1080 - panel:GetHeight(), 0))
    );
end

local function IsBagEquipped(bagIndex : number) : boolean
    local equipment = equipmentContainer;
    if equipment == nil then
        return false;
    end

    if bagIndex == 1 then
        return true;
    end

    local equipmentSlot = MAIN_BAG_SLOT_INDEX + bagIndex - 1;
    return equipment.itemSlots[equipmentSlot].itemID ~= 0i;
end

local function SetStorageSlotsEnabled(container : BagContainer, enabled : boolean)
    for slotIndex = 1, container.totalSlots do
        local uiSlot = container.uiSlots[slotIndex];
        if uiSlot.itemSlot ~= nil then
            uiSlot.itemSlot.root:SetEnabled(enabled);
        end
    end
end

local function RefreshStorageLayout()
    local panel = storagePanel;
    if panel == nil then
        return;
    end

    for container in attachedStorageContainers do
        SetStorageSlotsEnabled(container, false);
    end

    local slotStep = CONFIG.slotSize + CONFIG.slotSpacing;
    local globalSlotIndex = 0;

    for bagIndex = 1, CONFIG.maxBagCount do
        local container = PlayerContainers.GetBag(bagIndex) :: BagContainer?;
        if container == nil or not IsBagEquipped(bagIndex) then
            continue;
        end

        for slotIndex = 1, container.totalSlots do
            local uiItemSlot = container.uiSlots[slotIndex].itemSlot;
            if uiItemSlot == nil then
                continue;
            end

            local column = math.fmod(globalSlotIndex, CONFIG.columns);
            local row = math.floor(globalSlotIndex / CONFIG.columns);
            uiItemSlot:SetPos(vec.New(
                CONFIG.panelPadding + column * slotStep,
                -(CONFIG.headerHeight + CONFIG.panelPadding + row * slotStep)
            ));
            uiItemSlot.root:SetEnabled(true);
            globalSlotIndex += 1;
        end
    end

    displayedSlotCount = globalSlotIndex;
    local visibleColumns = math.min(displayedSlotCount, CONFIG.columns);
    local rows = math.ceil(displayedSlotCount / CONFIG.columns);
    local horizontalSpacing = math.max(visibleColumns - 1, 0) * CONFIG.slotSpacing;
    local verticalSpacing = math.max(rows - 1, 0) * CONFIG.slotSpacing;
    local width = CONFIG.panelPadding * 2 + visibleColumns * CONFIG.slotSize + horizontalSpacing;
    local height = CONFIG.headerHeight + CONFIG.panelPadding * 2 + rows * CONFIG.slotSize + verticalSpacing;
    panel:SetSize(vec.New(math.max(width, 1), math.max(height, 1)));
    panel:SetPos(ClampStoragePosition(panel, panel:GetPos()));
    local header = headerPanel;
    if header ~= nil then
        header:SetWidth(math.max(width, 1));
    end

    if displayedSlotCount == 0 and FrameManager.IsOpen(FRAME_NAME) then
        FrameManager.Close(FRAME_NAME);
    end
end

local function CreatePanels()
    local canvas = UI.GetCanvas("Backpack", vec.New(0, 0), vec.New(1920, 1080));
    local bagBarWidth = CONFIG.bagBarPadding * 2
        + CONFIG.maxBagCount * CONFIG.bagIconSize
        + math.max(CONFIG.maxBagCount - 1, 0) * CONFIG.bagIconSpacing;
    local bagBarHeight = CONFIG.bagBarPadding * 2 + CONFIG.bagIconSize;

    local createdBagBarPanel = canvas:NewPanel(
        vec.New(-CONFIG.screenMargin, CONFIG.bagBarBottomOffset),
        vec.New(bagBarWidth, bagBarHeight),
        100i,
        PANEL_TEMPLATE
    );
    createdBagBarPanel:SetAnchor(vec.New(1.0, 0.0));
    createdBagBarPanel:SetRelativePoint(vec.New(1.0, 0.0));
    createdBagBarPanel:SetVisible(false);
    bagBarPanel = createdBagBarPanel;

    local createdStoragePanel = canvas:NewPanel(
        vec.New(-CONFIG.screenMargin, CONFIG.bagBarBottomOffset + bagBarHeight + CONFIG.panelGap),
        vec.New(1, 1),
        100i,
        PANEL_TEMPLATE
    );
    createdStoragePanel:SetAnchor(vec.New(1.0, 0.0));
    createdStoragePanel:SetRelativePoint(vec.New(1.0, 0.0));
    createdStoragePanel:SetVisible(false);
    storagePanel = createdStoragePanel;

    local createdHeaderPanel = createdStoragePanel:NewPanel(
        vec.New(0, 0),
        vec.New(1, CONFIG.headerHeight),
        1i,
        PANEL_TEMPLATE
    );
    createdHeaderPanel:SetAnchor(vec.New(0.0, 1.0));
    createdHeaderPanel:SetRelativePoint(vec.New(0.0, 1.0));
    createdHeaderPanel:SetColor(vec.New(0.085, 0.100, 0.120));
    createdHeaderPanel:SetBorderSize(1.0);
    headerPanel = createdHeaderPanel;

    local title = createdHeaderPanel:NewText("Backpack", vec.New(0, 0), 1i, "DefaultButtonText");
    title:SetAnchor(vec.New(0.5, 0.5));
    title:SetRelativePoint(vec.New(0.5, 0.5));
    title:SetFontSize(14)
    title:SetInteractable(false);

    local mouseStart = vec.New(0, 0);
    local positionStart = vec.New(0, 0);
    local isDragging = false;
    createdHeaderPanel:SetOnMouseDown(function(_, _, button)
        if button ~= 0i then
            return;
        end

        mouseStart = UI.GetMousePos();
        positionStart = createdStoragePanel:GetPos();
        isDragging = true;
    end);
    createdHeaderPanel:SetOnMouseUp(function()
        isDragging = false;
    end);
    createdHeaderPanel:SetOnHoverEnd(function()
        isDragging = false;
    end);
    createdHeaderPanel:SetOnMouseHeld(function()
        if not isDragging then
            return;
        end

        local mousePosition = UI.GetMousePos();
        local position = positionStart + (mousePosition - mouseStart);
        createdStoragePanel:SetPos(ClampStoragePosition(createdStoragePanel, position));
    end);

    FrameManager.Register(FRAME_NAME,
    {
        root = createdStoragePanel,
        role = "Auxiliary",
        onOpen = function()
            createdStoragePanel:SetPos(ClampStoragePosition(createdStoragePanel, createdStoragePanel:GetPos()));
            createdStoragePanel:SetEnabled(true);
            createdStoragePanel:SetVisible(true);
        end,
        onClose = function()
            createdStoragePanel:SetVisible(false);
            createdStoragePanel:SetEnabled(false);
        end,
    });
end

local function ToggleBackpack(phase : integer) : integer
    if phase ~= InputPhase.Pressed then
        return InputReply.Ignored;
    end

    return if backpackAPI.Toggle() then InputReply.Consumed else InputReply.Ignored;
end

function backpackAPI.Initialize()
    if initialized then
        return;
    end

    assert(CONFIG.slotSize > 0, "Backpack slotSize must be positive");
    assert(CONFIG.columns >= 1, "Backpack columns must be at least one");
    assert(CONFIG.headerHeight > 0, "Backpack headerHeight must be positive");
    assert(CONFIG.maxBagCount >= 1, "Backpack maxBagCount must be at least one");

    initialized = true;
    CreatePanels();
    InputAPI.Bind("ToggleBackpack", "Toggle Backpack", "Interface",
        InputAPI.Keyboard(InputAPI.KeyValues.B, InputModifier.None, InputModifierMatch.Exact),
        ToggleBackpack,
        { defaultReply = InputReply.Ignored });
end

function backpackAPI.AttachEquipmentContainer(container : BagContainer)
    backpackAPI.Initialize();

    if equipmentContainer == container then
        backpackAPI.Refresh();
        return;
    end

    local previousEquipment = equipmentContainer;
    if previousEquipment ~= nil then
        for bagIndex = 1, CONFIG.maxBagCount do
            local oldSlot = previousEquipment.uiSlots[MAIN_BAG_SLOT_INDEX + bagIndex - 1];
            if oldSlot.itemSlot ~= nil then
                oldSlot.itemSlot.root:SetEnabled(false);
            end
        end
    end

    equipmentContainer = container;
    local bagBar = bagBarPanel;
    assert(bagBar ~= nil, "Backpack bag bar was not initialized");

    local slotSize = vec.New(CONFIG.bagIconSize, CONFIG.bagIconSize);
    for bagIndex = 1, CONFIG.maxBagCount do
        local slotIndex = MAIN_BAG_SLOT_INDEX + bagIndex - 1;
        local uiSlot = container.uiSlots[slotIndex];
        local itemSlot = ItemSlot.New(bagBar, container, slotIndex, ItemUtil.EquipType.Bag, slotSize);
        itemSlot:SetPos(vec.New(
            CONFIG.bagBarPadding + (bagIndex - 1) * (CONFIG.bagIconSize + CONFIG.bagIconSpacing),
            CONFIG.bagBarPadding
        ));
        itemSlot:SetAnchor(vec.New(0.0, 0.0));
        itemSlot:SetRelativePoint(vec.New(0.0, 0.0));
        uiSlot.itemSlot = itemSlot;
    end

    bagBar:SetVisible(IsInWorld());
    bagBar:SetEnabled(IsInWorld());
    backpackAPI.Refresh();
end

function backpackAPI.AttachStorageContainer(container : BagContainer)
    backpackAPI.Initialize();
    if attachedStorageContainers[container] then
        return;
    end

    attachedStorageContainers[container] = true;
    local storageRoot = storagePanel;
    assert(storageRoot ~= nil, "Backpack storage panel was not initialized");

    local slotSize = vec.New(CONFIG.slotSize, CONFIG.slotSize);
    for slotIndex = 1, container.totalSlots do
        local uiSlot = container.uiSlots[slotIndex];
        local itemSlot = ItemSlot.New(storageRoot, container, slotIndex, nil, slotSize);
        itemSlot:SetAnchor(vec.New(0.0, 1.0));
        itemSlot:SetRelativePoint(vec.New(0.0, 1.0));
        itemSlot.root:SetEnabled(false);
        uiSlot.itemSlot = itemSlot;
    end
end

function backpackAPI.Refresh()
    if not initialized then
        return;
    end

    if bagBarPanel ~= nil then
        local showBagBar = equipmentContainer ~= nil and IsInWorld();
        bagBarPanel:SetVisible(showBagBar);
        bagBarPanel:SetEnabled(showBagBar);
    end

    RefreshStorageLayout();
end

function backpackAPI.Toggle() : boolean
    if not initialized or displayedSlotCount == 0 then
        return false;
    end

    return FrameManager.Toggle(FRAME_NAME);
end

PlayerContainers.Subscribe(function()
    backpackAPI.Refresh();
end);

return backpackAPI;
