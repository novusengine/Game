export type EquipSlot =
{
    Helm: integer,
    Necklace: integer,
    Shoulders: integer,
    Cloak: integer,
    Chest: integer,
    Shirt: integer,
    Tabard: integer,
    Bracers: integer,
    Gloves: integer,
    Belt: integer,
    Pants: integer,
    Boots: integer,
    Ring1: integer,
    Ring2: integer,
    Trinket1: integer,
    Trinket2: integer,
    MainHand: integer,
    OffHand: integer,
    Ranged: integer,
    Ammo: integer
}

export type EquipType =
{
    Helm: integer,
    Necklace: integer,
    Shoulders: integer,
    Cloak: integer,
    Chest: integer,
    Shirt: integer,
    Tabard: integer,
    Bracers: integer,
    Gloves: integer,
    Belt: integer,
    Pants: integer,
    Boots: integer,
    Ring: integer,
    Trinket: integer,
    Weapon: integer,
    OffHand: integer,
    Ranged: integer,
    Ammo: integer
}

export type Category =
{
    Miscellaneous: integer,
    TradeGoods: integer,
    Consumable: integer,
    Reagent: integer,
    Container: integer,
    Quest: integer,
    Armor: integer,
    Weapon: integer
}

export type CategoryType =
{
    Miscellaneous:
    {
        Miscellaneous: integer,
        Reagent: integer,
        Pet: integer,
        Mount: integer,
        Junk: integer
    },
    TradeGoods:
    {
        TradeGoods: integer,
        Meat: integer,
        Cloth: integer,
        Leather: integer,
        MetalStone: integer,
        Herb: integer,
        Materials: integer
    },
    Consumable:
    {
        Consumable: integer,
        FoodDrink: integer,
        Bandage: integer,
        Flask: integer,
        Elixir: integer,
        Potion: integer,
        Scroll: integer
    },
    Reagent:
    {
        Reagent: integer
    },
    Container:
    {
        Bag: integer
    },
    Quest:
    {
        Quest: integer
    },
    Armor:
    {
        Armor: integer,
        Cloth: integer,
        Leather: integer,
        Mail: integer,
        Plate: integer,
        Shield: integer,
        Libram: integer,
        Idol: integer,
        Totem: integer
    },
    Weapon:
    {
        Weapon: integer,
        Sword_1H: integer,
        Sword_2H: integer,
        Mace_1H: integer,
        Mace_2H: integer,
        Axe_1H: integer,
        Axe_2H: integer,
        Dagger: integer,
        FistWeapon: integer,
        Polearm: integer,
        Staff: integer,
        Bow: integer,
        Crossbow: integer,
        Gun: integer,
        Wand: integer,
        Miscellaneous_Tool: integer,
        FishingPole: integer
    }
}

export type Rarity =
{
    Poor: integer,
    Common: integer,
    Uncommon: integer,
    Rare: integer,
    Epic: integer,
    Legendary: integer,
    Artifact: integer,
    Unique: integer
}

export type BindType =
{
    None: integer,
    BindOnPickup: integer,
    BindOnEquip: integer,
    BindOnUse: integer
}

export type WeaponStyle =
{
    Unspecified: integer,
    OneHand: integer,
    TwoHand: integer,
    MainHand: integer,
    OffHand: integer,
    Ranged: integer,
    Wand: integer,
    Tool: integer
}

export type ItemUtilAPI =
{
    EquipSlot: EquipSlot,
    EquipType: EquipType,
    EquipTypeNames: {string},
    EquipTypeTextures: {string},
    GetEquipTypeForEquipSlot: (equipSlot: integer) -> integer,
    GetEquipSlotForEquipType: (equipType: integer) -> integer,
    GetItemEquipType: (itemTemplate: table) -> integer,
    GetEquipTypeText: (equipType: integer) -> string,
    GetEquipTypeTexture: (equipType: integer) -> string,
    Rarity: Rarity,
    RarityNames: {string},
    RarityColors: {Vector3},
    GetRarityText: (rarity: integer) -> string,
    GetRarityColor: (rarity: integer) -> Vector3,
    Category: Category,
    CategoryType: CategoryType,
    CategoryNames: {string},
    CategoryTypeNames: {{string}},
    WeaponStyle: WeaponStyle,
    WeaponStyleNames: {string},
    GetCategoryText: (category: integer) -> string,
    GetCategoryTypeText: (category: integer, type: integer) -> string,
    GetWeaponStyleText: (style: integer) -> string,
    GetWeaponStyleTextFromWeaponCategoryType: (categoryType: integer) -> string,
    StatNames: {string},
    GetStatText: (statType: integer) -> string,
    ItemEffectTypePrefix: {string},
    GetItemEffectTypePrefix: (effectType: integer) -> string
}
local itemUtilAPI : ItemUtilAPI = { }

itemUtilAPI.EquipSlot =
{
    Helm        = 1i,
    Necklace    = 2i,
    Shoulders   = 3i,
    Cloak       = 4i,
    Chest       = 5i,
    Shirt       = 6i,
    Tabard      = 7i,
    Bracers     = 8i,
    Gloves      = 9i,
    Belt        = 10i,
    Pants       = 11i,
    Boots       = 12i,
    Ring1       = 13i,
    Ring2       = 14i,
    Trinket1    = 15i,
    Trinket2    = 16i,
    MainHand    = 17i,
    OffHand     = 18i,
    Ranged      = 19i,
    Ammo        = 20i
}

itemUtilAPI.EquipType =
{
    Helm        = 1i,
    Necklace    = 2i,
    Shoulders   = 3i,
    Cloak       = 4i,
    Chest       = 5i,
    Shirt       = 6i,
    Tabard      = 7i,
    Bracers     = 8i,
    Gloves      = 9i,
    Belt        = 10i,
    Pants       = 11i,
    Boots       = 12i,
    Ring        = 13i,
    Trinket     = 14i,
    Weapon      = 15i,
    OffHand     = 16i,
    Ranged      = 17i,
    Ammo        = 18i,
    Bag         = 19i
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
    "texture/ui/icons/itemslots/slot_helm_empty.png",
    "texture/ui/icons/itemslots/slot_neck_empty.png",
    "texture/ui/icons/itemslots/slot_shoulders_empty.png",
    "texture/ui/icons/itemslots/slot_cloak_empty.png",
    "texture/ui/icons/itemslots/slot_chest_empty.png",
    "texture/ui/icons/itemslots/slot_shirt_empty.png",
    "texture/ui/icons/itemslots/slot_tabard_empty.png",
    "texture/ui/icons/itemslots/slot_bracers_empty.png",
    "texture/ui/icons/itemslots/slot_gloves_empty.png",
    "texture/ui/icons/itemslots/slot_belt_empty.png",
    "texture/ui/icons/itemslots/slot_pants_empty.png",
    "texture/ui/icons/itemslots/slot_boots_empty.png",
    "texture/ui/icons/itemslots/slot_ring_empty.png",
    "texture/ui/icons/itemslots/slot_trinket_empty.png",
    "texture/ui/icons/itemslots/slot_weapon_empty.png",
    "texture/ui/icons/itemslots/slot_shield_empty.png",
    "texture/ui/icons/itemslots/slot_bow_empty.png",
    "texture/ui/icons/unknown.png",
    "texture/ui/icons/itemslots/slot_bag_empty.png"
}

itemUtilAPI.GetEquipTypeForEquipSlot = function(equipSlot : integer) : integer
    local equipType = 0i

    if integer.ge(equipSlot, itemUtilAPI.EquipSlot.Helm) and integer.le(equipSlot, itemUtilAPI.EquipSlot.Boots) then
        equipType = equipSlot
    elseif equipSlot == itemUtilAPI.EquipSlot.Ring1 or equipSlot == itemUtilAPI.EquipSlot.Ring2 then
        equipType = itemUtilAPI.EquipType.Ring
    elseif equipSlot == itemUtilAPI.EquipSlot.Trinket1 or equipSlot == itemUtilAPI.EquipSlot.Trinket2 then
        equipType = itemUtilAPI.EquipType.Trinket
    elseif integer.ge(equipSlot, itemUtilAPI.EquipSlot.MainHand) and integer.le(equipSlot, itemUtilAPI.EquipSlot.Ammo) then
        equipType = integer.sub(equipSlot, 2i)
    end

    return equipType
end

itemUtilAPI.GetEquipSlotForEquipType = function(equipType : integer) : integer
    local equipSlot = 0i

    if integer.ge(equipType, itemUtilAPI.EquipType.Helm) and integer.le(equipType, itemUtilAPI.EquipType.Boots) then
        equipSlot = equipType
    elseif equipType == itemUtilAPI.EquipType.Ring then
        equipSlot = itemUtilAPI.EquipSlot.Ring1
    elseif equipType == itemUtilAPI.EquipType.Trinket then
        equipSlot = itemUtilAPI.EquipSlot.Trinket1
    elseif integer.ge(equipType, itemUtilAPI.EquipType.Weapon) and integer.le(equipType, itemUtilAPI.EquipType.Ammo) then
        equipSlot = integer.add(equipType, 2i)
    end

    return equipSlot
end

itemUtilAPI.GetItemEquipType = function(itemTemplate : table) : integer
    local equipType = 0i

    if itemTemplate.ArmorTemplateID ~= 0i then
        local armorTemplate = Item.GetItemArmorInfo(itemTemplate.ArmorTemplateID)
        equipType = armorTemplate.EquipType
    elseif itemTemplate.WeaponTemplateID ~= 0i then
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
    elseif itemTemplate.ShieldTemplateID ~= 0i then
        equipType = itemUtilAPI.EquipType.OffHand
    end

    return equipType
end

itemUtilAPI.GetEquipTypeText = function(equipType : integer) : string
    local numEquipTypeNames = #itemUtilAPI.EquipTypeNames
    local equipSlotIndex = integer.tonumber(equipType)
    if equipSlotIndex <= 0 or equipSlotIndex > numEquipTypeNames then
        equipSlotIndex = integer.tonumber(itemUtilAPI.EquipType.Helm)
    end

    return itemUtilAPI.EquipTypeNames[equipSlotIndex]
end

itemUtilAPI.GetEquipTypeTexture = function(equipType : integer) : string
    local numEquipTypeTextures = #itemUtilAPI.EquipTypeTextures
    local equipTypeIndex = integer.tonumber(equipType)
    if equipTypeIndex <= 0 or equipTypeIndex > numEquipTypeTextures then
        equipTypeIndex = integer.tonumber(itemUtilAPI.EquipType.Helm)
    end

    return itemUtilAPI.EquipTypeTextures[equipTypeIndex]
end

itemUtilAPI.Rarity =
{
    Poor      = 1i,
    Common    = 2i,
    Uncommon  = 3i,
    Rare      = 4i,
    Epic      = 5i,
    Legendary = 6i,
    Artifact  = 7i,
    Unique    = 8i
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
    vec.New(0x9d/255.0, 0x9d/255.0, 0x9d/255.0),
    vec.New(0xff/255.0, 0xff/255.0, 0xff/255.0),
    vec.New(0x1e/255.0, 0xff/255.0, 0x00/255.0),
    vec.New(0x00/255.0, 0x70/255.0, 0xdd/255.0),
    vec.New(0xa3/255.0, 0x35/255.0, 0xee/255.0),
    vec.New(0xff/255.0, 0x80/255.0, 0x00/255.0),
    vec.New(0xe6/255.0, 0xcc/255.0, 0x80/255.0),
    vec.New(0x00/255.0, 0xcc/255.0, 0xff/255.0)
}

itemUtilAPI.GetRarityText = function(rarity : integer) : string
    local numRarityNames = #itemUtilAPI.RarityNames
    local rarityIndex = integer.tonumber(rarity)
    if rarityIndex <= 0 or rarityIndex > numRarityNames then
        rarityIndex = integer.tonumber(itemUtilAPI.Rarity.Poor)
    end

    return itemUtilAPI.RarityNames[rarityIndex]
end

itemUtilAPI.GetRarityColor = function(rarity : integer) : Vector3
    local numRarityColors = #itemUtilAPI.RarityColors
    local rarityIndex = integer.tonumber(rarity)
    if rarityIndex <= 0 or rarityIndex > numRarityColors then
        rarityIndex = integer.tonumber(itemUtilAPI.Rarity.Poor)
    end

    return itemUtilAPI.RarityColors[rarityIndex]
end

itemUtilAPI.BindType =
{
    None            = 0i,
    BindOnPickup    = 1i,
    BindOnEquip     = 2i,
    BindOnUse       = 3i
}

itemUtilAPI.BindTypeNames =
{
    "None",
    "Binds when picked up",
    "Binds when equipped",
    "Binds when used"
}

itemUtilAPI.GetBindTypeText = function(bindType : integer) : string
    local numBindTypeNames = #itemUtilAPI.BindTypeNames
    local bindTypeIndex = integer.tonumber(bindType)
    bindTypeIndex = if bindTypeIndex > 0 and bindTypeIndex <= numBindTypeNames then bindTypeIndex + 1 else 1

    return itemUtilAPI.BindTypeNames[bindTypeIndex]
end

itemUtilAPI.Category = 
{
    Miscellaneous   = 1i,
    TradeGoods      = 2i,
    Consumable      = 3i,
    Reagent         = 4i,
    Container       = 5i,
    Quest           = 6i,
    Armor           = 7i,
    Weapon          = 8i
}

itemUtilAPI.CategoryType =
{
    Miscellaneous =
    {
        Miscellaneous   = 1i,
        Reagent         = 2i,
        Pet             = 3i,
        Mount           = 4i,
        Junk            = 5i
    },
    
    TradeGoods =
    {
        TradeGoods  = 1i,
        Meat        = 2i,
        Cloth       = 3i,
        Leather     = 4i,
        MetalStone  = 5i,
        Herb        = 6i,
        Materials   = 7i
    },
    
    Consumable =
    {
        Consumable  = 1i,
        FoodDrink   = 2i,
        Bandage     = 3i,
        Flask       = 4i,
        Elixir      = 5i,
        Potion      = 6i,
        Scroll      = 7i
    },
    
    Reagent =
    {
        Reagent = 1i
    },
    
    Container =
    {
        Bag = 1i
    },
    
    Quest =
    {
        Quest   = 1i
    },
    
    Armor =
    {
        Armor   = 1i,
        Cloth   = 2i,
        Leather = 3i,
        Mail    = 4i,
        Plate   = 5i,
        Shield  = 6i,
        Libram  = 7i,
        Idol    = 8i,
        Totem   = 9i
    },
    
    Weapon =
    {
        Weapon              = 1i,
        Sword_1H            = 2i,
        Sword_2H            = 3i,
        Mace_1H             = 4i,
        Mace_2H             = 5i,
        Axe_1H              = 6i,
        Axe_2H              = 7i,
        Dagger              = 8i,
        FistWeapon          = 9i,
        Polearm             = 10i,
        Staff               = 11i,
        Bow                 = 12i,
        Crossbow            = 13i,
        Gun                 = 14i,
        Wand                = 15i,
        Miscellaneous_Tool  = 16i,
        FishingPole         = 17i
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
    Unspecified = 1i,
    OneHand     = 2i,
    TwoHand     = 3i,
    MainHand    = 4i,
    OffHand     = 5i,
    Ranged      = 6i,
    Wand        = 7i,
    Tool        = 8i
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

itemUtilAPI.GetCategoryText = function(category : integer) : string
    local numCategoryNames = #itemUtilAPI.CategoryNames
    local categoryIndex = integer.tonumber(category)
    if categoryIndex <= 0 or categoryIndex > numCategoryNames then
        categoryIndex = integer.tonumber(itemUtilAPI.Category.Miscellaneous)
    end

    return itemUtilAPI.CategoryNames[categoryIndex]
end

itemUtilAPI.GetCategoryTypeText = function(category : integer, type : integer) : string
    local numCategoryNames = #itemUtilAPI.CategoryNames
    local categoryIndex = integer.tonumber(category)
    if categoryIndex <= 0 or categoryIndex > numCategoryNames then
        categoryIndex = integer.tonumber(itemUtilAPI.Category.Miscellaneous)
    end

    local numCategoryTypeNames = #itemUtilAPI.CategoryTypeNames[categoryIndex]
    local typeIndex = integer.tonumber(type)
    if typeIndex <= 0 or typeIndex > numCategoryTypeNames then
        typeIndex = integer.tonumber(itemUtilAPI.CategoryType.Miscellaneous.Miscellaneous)
    end

    return itemUtilAPI.CategoryTypeNames[categoryIndex][typeIndex]
end

itemUtilAPI.GetWeaponStyleText = function(style : integer) : string
    local numWeaponStyleNames = #itemUtilAPI.WeaponStyleNames
    local weaponStyleIndex = integer.tonumber(style)
    if weaponStyleIndex <= 0 or weaponStyleIndex > numWeaponStyleNames then
        weaponStyleIndex = integer.tonumber(itemUtilAPI.WeaponStyle.Unspecified)
    end

    return itemUtilAPI.WeaponStyleNames[weaponStyleIndex]
end

itemUtilAPI.GetWeaponStyleTextFromWeaponCategoryType = function(categoryType : integer) : string
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
    local numericWeaponStyleIndex = integer.tonumber(weaponStyleIndex)
    if numericWeaponStyleIndex <= 0 or numericWeaponStyleIndex > numWeaponStyleNames then
        numericWeaponStyleIndex = integer.tonumber(itemUtilAPI.WeaponStyle.Unspecified)
    end

    return itemUtilAPI.WeaponStyleNames[numericWeaponStyleIndex]
end

itemUtilAPI.StatNames =
{
    "Health",
    "Stamina",
    "Strength",
    "Agility",
    "Intellect",
    "Spirit"
}

itemUtilAPI.GetStatText = function(statType : integer) : string
    local numStatNames = #itemUtilAPI.StatNames
    local statTypeIndex = integer.tonumber(statType)
    statTypeIndex = if statTypeIndex > 0 and statTypeIndex <= numStatNames then statTypeIndex else 1

    return itemUtilAPI.StatNames[statTypeIndex]
end

itemUtilAPI.ItemEffectTypePrefix =
{
    "Equip: ",
    "Use: ",
    "Chance on Hit: ",
    "None"
}
itemUtilAPI.GetItemEffectTypePrefix = function(effectType : integer) : string
    local numItemEffectTypePrefix = #itemUtilAPI.ItemEffectTypePrefix
    local effectTypePrefixIndex = integer.tonumber(effectType)
    effectTypePrefixIndex = if effectTypePrefixIndex > 0 and effectTypePrefixIndex <= numItemEffectTypePrefix then effectTypePrefixIndex else 4

    return itemUtilAPI.ItemEffectTypePrefix[effectTypePrefixIndex]
end

return itemUtilAPI
