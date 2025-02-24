export type EquipSlot =
{
    Helm: number,
    Necklace: number,
    Shoulders: number,
    Cloak: number,
    Chest: number,
    Shirt: number,
    Tabard: number,
    Bracers: number,
    Gloves: number,
    Belt: number,
    Pants: number,
    Boots: number,
    Ring1: number,
    Ring2: number,
    Trinket1: number,
    Trinket2: number,
    MainHand: number,
    OffHand: number,
    Ranged: number,
    Ammo: number
}

export type EquipType =
{
    Helm: number,
    Necklace: number,
    Shoulders: number,
    Cloak: number,
    Chest: number,
    Shirt: number,
    Tabard: number,
    Bracers: number,
    Gloves: number,
    Belt: number,
    Pants: number,
    Boots: number,
    Ring: number,
    Trinket: number,
    Weapon: number,
    OffHand: number,
    Ranged: number,
    Ammo: number
}

export type Category =
{
    Miscellaneous: number,
    TradeGoods: number,
    Consumable: number,
    Reagent: number,
    Container: number,
    Quest: number,
    Armor: number,
    Weapon: number
}

export type CategoryType =
{
    Miscellaneous:
    {
        Miscellaneous: number,
        Reagent: number,
        Pet: number,
        Mount: number,
        Junk: number
    },
    TradeGoods:
    {
        TradeGoods: number,
        Meat: number,
        Cloth: number,
        Leather: number,
        MetalStone: number,
        Herb: number,
        Materials: number
    },
    Consumable:
    {
        Consumable: number,
        FoodDrink: number,
        Bandage: number,
        Flask: number,
        Elixir: number,
        Potion: number,
        Scroll: number
    },
    Reagent:
    {
        Reagent: number
    },
    Container:
    {
        Bag: number
    },
    Quest:
    {
        Quest: number
    },
    Armor:
    {
        Armor: number,
        Cloth: number,
        Leather: number,
        Mail: number,
        Plate: number,
        Shield: number,
        Libram: number,
        Idol: number,
        Totem: number
    },
    Weapon:
    {
        Weapon: number,
        Sword_1H: number,
        Sword_2H: number,
        Mace_1H: number,
        Mace_2H: number,
        Axe_1H: number,
        Axe_2H: number,
        Dagger: number,
        FistWeapon: number,
        Polearm: number,
        Staff: number,
        Bow: number,
        Crossbow: number,
        Gun: number,
        Wand: number,
        Miscellaneous_Tool: number,
        FishingPole: number
    }
}

export type Rarity =
{
    Poor: number,
    Common: number,
    Uncommon: number,
    Rare: number,
    Epic: number,
    Legendary: number,
    Artifact: number,
    Unique: number
}

export type WeaponStyle =
{
    Unspecified: number,
    OneHand: number,
    TwoHand: number,
    MainHand: number,
    OffHand: number,
    Ranged: number,
    Wand: number,
    Tool: number
}

export type ItemUtilAPI =
{
    EquipSlot: EquipSlot,
    EquipType: EquipType,
    EquipTypeNames: {string},
    EquipTypeTextures: {string},
    GetEquipTypeForEquipSlot: (equipSlot: number) -> number,
    GetItemEquipType: (itemTemplate: table) -> number,
    GetEquipTypeText: (equipType: number) -> string,
    GetEquipTypeTexture: (equipType: number) -> string,
    Rarity: Rarity,
    RarityNames: {string},
    RarityColors: {Vector3},
    GetRarityText: (rarity: number) -> string,
    GetRarityColor: (rarity: number) -> Vector3,
    Category: Category,
    CategoryType: CategoryType,
    CategoryNames: {string},
    CategoryTypeNames: {{string}},
    WeaponStyle: WeaponStyle,
    WeaponStyleNames: {string},
    GetCategoryText: (category: number) -> string,
    GetCategoryTypeText: (category: number, type: number) -> string,
    GetWeaponStyleText: (style: number) -> string,
    GetWeaponStyleTextFromWeaponCategoryType: (categoryType: number) -> string,
    StatNames: {string},
    GetStatText: (statType: number) -> string,
    ItemEffectTypePrefix: {string},
    GetItemEffectTypePrefix: (effectType: number) -> string
}
local itemUtilAPI : ItemUtilAPI = { }

itemUtilAPI.EquipSlot =
{
    Helm        = 1,
    Necklace    = 2,
    Shoulders   = 3,
    Cloak       = 4,
    Chest       = 5,
    Shirt       = 6,
    Tabard      = 7,
    Bracers     = 8,
    Gloves      = 9,
    Belt        = 10,
    Pants       = 11,
    Boots       = 12,
    Ring1       = 13,
    Ring2       = 14,
    Trinket1    = 15,
    Trinket2    = 16,
    MainHand    = 17,
    OffHand     = 18,
    Ranged      = 19,
    Ammo        = 20
}

itemUtilAPI.EquipType =
{
    Helm        = 1,
    Necklace    = 2,
    Shoulders   = 3,
    Cloak       = 4,
    Chest       = 5,
    Shirt       = 6,
    Tabard      = 7,
    Bracers     = 8,
    Gloves      = 9,
    Belt        = 10,
    Pants       = 11,
    Boots       = 12,
    Ring        = 13,
    Trinket     = 14,
    Weapon      = 15,
    OffHand     = 16,
    Ranged      = 17,
    Ammo        = 18,
    Bag         = 19
}

itemUtilAPI.EquipTypeNames =
{
    "Helm",
    "Necklace",
    "Shoulders",
    "Cloak",
    "Chest",
    "Shirt",
    "Tabard",
    "Bracers",
    "Gloves",
    "Belt",
    "Pants",
    "Boots",
    "Ring",
    "Trinket",
    "Weapon",
    "OffHand",
    "Ranged",
    "Ammo",
    "Bag"
}

itemUtilAPI.EquipTypeTextures =
{
    "Data/Texture/interface/paperdoll/ui-paperdoll-slot-head.dds",
    "Data/Texture/interface/paperdoll/ui-paperdoll-slot-neck.dds",
    "Data/Texture/interface/paperdoll/ui-paperdoll-slot-shoulder.dds",
    "Data/Texture/interface/paperdoll/ui-paperdoll-slot-chest.dds",
    "Data/Texture/interface/paperdoll/ui-paperdoll-slot-chest.dds",
    "Data/Texture/interface/paperdoll/ui-paperdoll-slot-shirt.dds",
    "Data/Texture/interface/paperdoll/ui-paperdoll-slot-tabard.dds",
    "Data/Texture/interface/paperdoll/ui-paperdoll-slot-wrists.dds",
    "Data/Texture/interface/paperdoll/ui-paperdoll-slot-hands.dds",
    "Data/Texture/interface/paperdoll/ui-paperdoll-slot-waist.dds",
    "Data/Texture/interface/paperdoll/ui-paperdoll-slot-legs.dds",
    "Data/Texture/interface/paperdoll/ui-paperdoll-slot-feet.dds",
    "Data/Texture/interface/paperdoll/ui-paperdoll-slot-finger.dds",
    "Data/Texture/interface/paperdoll/ui-paperdoll-slot-trinket.dds",
    "Data/Texture/interface/paperdoll/ui-paperdoll-slot-mainhand.dds",
    "Data/Texture/interface/paperdoll/ui-paperdoll-slot-secondaryhand.dds",
    "Data/Texture/interface/paperdoll/ui-paperdoll-slot-ranged.dds",
    "Data/Texture/interface/paperdoll/ui-paperdoll-slot-ammo.dds",
    "Data/Texture/interface/paperdoll/ui-paperdoll-slot-bag.dds"
}

itemUtilAPI.GetEquipTypeForEquipSlot = function(equipSlot : number) : number
    local equipType = 0

    if equipSlot >= itemUtilAPI.EquipSlot.Helm and equipSlot <= itemUtilAPI.EquipSlot.Boots then
        equipType = equipSlot
    elseif equipSlot == itemUtilAPI.EquipSlot.Ring1 or equipSlot == itemUtilAPI.EquipSlot.Ring2 then
        equipType = itemUtilAPI.EquipType.Ring
    elseif equipSlot == itemUtilAPI.EquipSlot.Trinket1 or equipSlot == itemUtilAPI.EquipSlot.Trinket2 then
        equipType = itemUtilAPI.EquipType.Trinket
    elseif equipSlot >= itemUtilAPI.EquipSlot.MainHand and equipSlot <= itemUtilAPI.EquipSlot.Ammo then
        equipType = equipSlot - 2
    end

    return equipType
end

itemUtilAPI.GetEquipSlotForEquipType = function(equipType : number) : number
    local equipSlot = 0

    if equipType >= itemUtilAPI.EquipType.Helm and equipType <= itemUtilAPI.EquipType.Boots then
        equipSlot = equipType
    elseif equipType == itemUtilAPI.EquipType.Ring then
        equipSlot = itemUtilAPI.EquipSlot.Ring1
    elseif equipType == itemUtilAPI.EquipType.Trinket then
        equipSlot = itemUtilAPI.EquipSlot.Trinket1
    elseif equipType >= itemUtilAPI.EquipType.Weapon and equipType <= itemUtilAPI.EquipType.Ammo then
        equipSlot = equipType + 2
    end

    return equipSlot
end

itemUtilAPI.GetItemEquipType = function(itemTemplate : table) : number
    local equipType = 0

    if itemTemplate.ArmorTemplateID > 0 then
        local armorTemplate = Item.GetItemArmorInfo(itemTemplate.ArmorTemplateID)
        equipType = armorTemplate.EquipType
    elseif itemTemplate.WeaponTemplateID > 0 then
        local weaponTemplate = Item.GetItemWeaponInfo(itemTemplate.WeaponTemplateID)
        if weaponTemplate.WeaponStyle == itemUtilAPI.WeaponStyle.OneHand or
           weaponTemplate.WeaponStyle == itemUtilAPI.WeaponStyle.MainHand or
           weaponTemplate.WeaponStyle == itemUtilAPI.WeaponStyle.TwoHand then
            equipType = itemUtilAPI.EquipType.Weapon
        elseif weaponTemplate.WeaponStyle == itemUtilAPI.WeaponStyle.OffHand then
            equipType = itemUtilAPI.EquipType.OffHand
        elseif weaponTemplate.WeaponStyle == itemUtilAPI.WeaponStyle.Ranged or
                weaponTemplate.WeaponStyle == itemUtilAPI.WeaponStyle.Wand then
            equipType = itemUtilAPI.EquipType.Ranged
        else
            equipType = itemUtilAPI.EquipType.Weapon
        end
    elseif itemTemplate.ShieldTemplateID > 0 then
        equipType = itemUtilAPI.EquipType.OffHand
    end

    return equipType
end

itemUtilAPI.GetEquipTypeText = function(equipType : number) : string
    local numEquipTypeNames = #itemUtilAPI.EquipTypeNames
    local equipSlotIndex = if equipType > 0 and equipType <= numEquipTypeNames then equipType else itemUtilAPI.EquipType.Helm

    return itemUtilAPI.EquipTypeNames[equipSlotIndex]
end

itemUtilAPI.GetEquipTypeTexture = function(equipType : number) : string
    local numEquipTypeTextures = #itemUtilAPI.EquipTypeTextures
    local equipTypeIndex = if equipType > 0 and equipType <= numEquipTypeTextures then equipType else itemUtilAPI.EquipType.Helm

    return itemUtilAPI.EquipTypeTextures[equipTypeIndex]
end

itemUtilAPI.Rarity =
{
    Poor      = 1,
    Common    = 2,
    Uncommon  = 3,
    Rare      = 4,
    Epic      = 5,
    Legendary = 6,
    Artifact  = 7,
    Unique    = 8
}

itemUtilAPI.RarityNames =
{
    "Poor",
    "Common",
    "Uncommon",
    "Rare",
    "Epic",
    "Legendary",
    "Artifact",
    "Unique"
}
itemUtilAPI.RarityColors =
{
    vector.create(0x9d/255.0, 0x9d/255.0, 0x9d/255.0),
    vector.create(0xff/255.0, 0xff/255.0, 0xff/255.0),
    vector.create(0x1e/255.0, 0xff/255.0, 0x00/255.0),
    vector.create(0x00/255.0, 0x70/255.0, 0xdd/255.0),
    vector.create(0xa3/255.0, 0x35/255.0, 0xee/255.0),
    vector.create(0xff/255.0, 0x80/255.0, 0x00/255.0),
    vector.create(0xe6/255.0, 0xcc/255.0, 0x80/255.0),
    vector.create(0x00/255.0, 0xcc/255.0, 0xff/255.0)
}

itemUtilAPI.GetRarityText = function(rarity : number) : string
    local numRarityNames = #itemUtilAPI.RarityNames
    local rarityIndex = if rarity > 0 and rarity <= numRarityNames then rarity else itemUtilAPI.Rarity.Poor

    return itemUtilAPI.RarityNames[rarityIndex]
end

itemUtilAPI.GetRarityColor = function(rarity : number) : number
    local numRarityColors = #itemUtilAPI.RarityColors
    local rarityIndex = if rarity > 0 and rarity <= numRarityColors then rarity else itemUtilAPI.Rarity.Poor

    return itemUtilAPI.RarityColors[rarityIndex]
end

itemUtilAPI.Category = 
{
    Miscellaneous   = 1,
    TradeGoods      = 2,
    Consumable      = 3,
    Reagent         = 4,
    Container       = 5,
    Quest           = 6,
    Armor           = 7,
    Weapon          = 8
}

itemUtilAPI.CategoryType =
{
    Miscellaneous =
    {
        Miscellaneous   = 1,
        Reagent         = 2,
        Pet             = 3,
        Mount           = 4,
        Junk            = 5
    },
    
    TradeGoods =
    {
        TradeGoods  = 1,
        Meat        = 2,
        Cloth       = 3,
        Leather     = 4,
        MetalStone  = 5,
        Herb        = 6,
        Materials   = 7
    },
    
    Consumable =
    {
        Consumable  = 1,
        FoodDrink   = 2,
        Bandage     = 3,
        Flask       = 4,
        Elixir      = 5,
        Potion      = 6,
        Scroll      = 7
    },
    
    Reagent =
    {
        Reagent = 1
    },
    
    Container =
    {
        Bag = 1
    },
    
    Quest =
    {
        Quest   = 1
    },
    
    Armor =
    {
        Armor   = 1,
        Cloth   = 2,
        Leather = 3,
        Mail    = 4,
        Plate   = 5,
        Shield  = 6,
        Libram  = 7,
        Idol    = 8,
        Totem   = 9
    },
    
    Weapon =
    {
        Weapon              = 1,
        Sword_1H            = 2,
        Sword_2H            = 3,
        Mace_1H             = 4,
        Mace_2H             = 5,
        Axe_1H              = 6,
        Axe_2H              = 7,
        Dagger              = 8,
        FistWeapon          = 9,
        Polearm             = 10,
        Staff               = 11,
        Bow                 = 12,
        Crossbow            = 13,
        Gun                 = 14,
        Wand                = 15,
        Miscellaneous_Tool  = 16,
        FishingPole         = 17
    }
}

itemUtilAPI.CategoryNames =
{
    "Miscellaneous",
    "Trade Goods",
    "Consumable",
    "Reagent",
    "Container",
    "Quest",
    "Armor",
    "Weapon"
}

itemUtilAPI.CategoryTypeNames =
{
    -- Miscellaneous
    {
        "Miscellaneous",
        "Reagent",
        "Pet",
        "Mount",
        "Junk"
    },

    -- Trade Goods
    {
        "Trade Goods",
        "Meat",
        "Cloth",
        "Leather",
        "Metal & Stone",
        "Herb",
        "Materials"
    },

    -- Consumable
    {
        "Consumable",
        "Food & Drink",
        "Bandage",
        "Flask",
        "Elixir",
        "Potion",
        "Scroll"
    },

    -- Reagent
    {
        "Reagent"
    },

    -- Container
    {
        "Bag"
    },

    -- Quest
    {
        "Quest"
    },

    -- Armor
    {
        "Armor",
        "Cloth",
        "Leather",
        "Mail",
        "Plate",
        "Shield",
        "Libram",
        "Idol",
        "Totem"
    },

    -- Weapon
    {
        "Weapon",
        "Sword",
        "Sword",
        "Mace",
        "Mace",
        "Axe",
        "Axe",
        "Dagger",
        "Fist Weapon",
        "Polearm",
        "Staff",
        "Bow",
        "Crossbow",
        "Gun",
        "Wand",
        "Miscellaneous (Tools)",
        "Fishing Pole"
    }
}

itemUtilAPI.WeaponStyle =
{
    Unspecified = 1,
    OneHand     = 2,
    TwoHand     = 3,
    MainHand    = 4,
    OffHand     = 5,
    Ranged      = 6,
    Wand        = 7,
    Tool        = 8
}
itemUtilAPI.WeaponStyleNames =
{
    "Unspecified",
    "One-Hand",
    "Two-Hand",
    "Main Hand",
    "Off Hand",
    "Ranged",
    "Wand",
    "Tool"
}

itemUtilAPI.GetCategoryText = function(category : number) : string
    local numCategoryNames = #itemUtilAPI.CategoryNames
    local categoryIndex = if category > 0 and category <= numCategoryNames then category else itemUtilAPI.Category.Miscellaneous

    return itemUtilAPI.CategoryNames[categoryIndex]
end

itemUtilAPI.GetCategoryTypeText = function(category : number, type : number) : string
    local numCategoryNames = #itemUtilAPI.CategoryNames
    local categoryIndex = if category > 0 and category <= numCategoryNames then category else itemUtilAPI.Category.Miscellaneous

    local numCategoryTypeNames = #itemUtilAPI.CategoryTypeNames[categoryIndex]
    local typeIndex = if type > 0 and type <= numCategoryTypeNames then type else itemUtilAPI.CategoryType.Miscellaneous.Miscellaneous

    return itemUtilAPI.CategoryTypeNames[categoryIndex][typeIndex]
end

itemUtilAPI.GetWeaponStyleText = function(style : number) : string
    local numWeaponStyleNames = #itemUtilAPI.WeaponStyleNames
    local weaponStyleIndex = if style > 0 and style <= numWeaponStyleNames then style else itemUtilAPI.WeaponStyle.Unspecified

    return itemUtilAPI.WeaponStyleNames[weaponStyleIndex]
end

itemUtilAPI.GetWeaponStyleTextFromWeaponCategoryType = function(categoryType : number) : string
    local weaponStyleIndex = itemUtilAPI.WeaponStyle.Unspecified

    if categoryType == itemUtilAPI.CategoryType.Weapon.Sword_1H or
       categoryType == itemUtilAPI.CategoryType.Weapon.Mace_1H or
       categoryType == itemUtilAPI.CategoryType.Weapon.Axe_1H or
       categoryType == itemUtilAPI.CategoryType.Weapon.Dagger or
       categoryType == itemUtilAPI.CategoryType.Weapon.FistWeapon then
        weaponStyleIndex = itemUtilAPI.WeaponStyle.OneHand
    elseif categoryType == itemUtilAPI.CategoryType.Weapon.Sword_2H or
       categoryType == itemUtilAPI.CategoryType.Weapon.Mace_2H or
       categoryType == itemUtilAPI.CategoryType.Weapon.Axe_2H or
       categoryType == itemUtilAPI.CategoryType.Weapon.Polearm or
       categoryType == itemUtilAPI.CategoryType.Weapon.Staff or
       categoryType == itemUtilAPI.CategoryType.Weapon.FishingPole then
        weaponStyleIndex = itemUtilAPI.WeaponStyle.TwoHand
    elseif categoryType == itemUtilAPI.CategoryType.Weapon.Bow or
       categoryType == itemUtilAPI.CategoryType.Weapon.Crossbow or
       categoryType == itemUtilAPI.CategoryType.Weapon.Gun then
        weaponStyleIndex = itemUtilAPI.WeaponStyle.Ranged
    elseif categoryType == itemUtilAPI.CategoryType.Weapon.Wand then
        weaponStyleIndex = itemUtilAPI.WeaponStyle.Wand
    elseif categoryType == itemUtilAPI.CategoryType.Weapon.Miscellaneous_Tool then
        weaponStyleIndex = itemUtilAPI.WeaponStyle.Tool
    end

    local numWeaponStyleNames = #itemUtilAPI.WeaponStyleNames
    weaponStyleIndex = if weaponStyleIndex > 0 and weaponStyleIndex <= numWeaponStyleNames then weaponStyleIndex else itemUtilAPI.WeaponStyle.Unspecified

    return itemUtilAPI.WeaponStyleNames[weaponStyleIndex]
end

itemUtilAPI.StatNames =
{
    "Health",
    "Mana",
    "Stamina",
    "Strength",
    "Agility",
    "Intellect",
    "Spirit"
}

itemUtilAPI.GetStatText = function(statType : number) : string
    local numStatNames = #itemUtilAPI.StatNames
    local statTypeIndex = if statType > 0 and statType <= numStatNames then statType else 1

    return itemUtilAPI.StatNames[statTypeIndex]
end

itemUtilAPI.ItemEffectTypePrefix =
{
    "Equip: ",
    "Use: ",
    "Chance on Hit: ",
    "None"
}
itemUtilAPI.GetItemEffectTypePrefix = function(effectType : number) : string
    local numItemEffectTypePrefix = #itemUtilAPI.ItemEffectTypePrefix
    local effectTypePrefixIndex = if effectType > 0 and effectType <= numItemEffectTypePrefix then effectType else 4

    return itemUtilAPI.ItemEffectTypePrefix[effectTypePrefixIndex]
end

return itemUtilAPI